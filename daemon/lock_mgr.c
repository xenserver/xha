// TBD: excluded flag

//
//++
//
//  $Revision: $
//
//      Copyright (c) Stratus Technologies Bermuda Ltd., 2008.
//      All Rights Reserved. Unpublished rights reserved
//      under the copyright laws of the United States.
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU Lesser General Public License as published
//      by the Free Software Foundation; version 2.1 only. with the special
//      exception on linking described in file LICENSE.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU Lesser General Public License for more details.
//
//
//  DESCRIPTION:
//
//      This file implement lock manager over State-File of
//      XenServer HA.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: February 29, 2008
//
//  DESIGN ISSUES:
//
//  REVISION HISTORY: Inserted automatically
//
//      $Log: lock_mgr.h $
//      
//  
//--
//


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "log.h"
#include "config.h"
#include "com.h"
#include "sm.h"
#include "statefile.h"
#include "mtcerrno.h"
#include "heartbeat.h"


//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//


//
//
//  L O C A L   D E F I N I T I O N S
//
//

#define INVALID_NODE    -1

#define mssleep(X) \
{ \
    struct timespec sleep_ts = mstots(X), ts_rem; \
    ts_rem = sleep_ts; \
    while (nanosleep(&sleep_ts, &ts_rem)) sleep_ts = ts_rem; \
}



MTC_STATIC void
lm_sf_updated(
    HA_COMMON_OBJECT_HANDLE Handle,
    void *Buffer);

MTC_STATIC void
lm_sm_updated(
    HA_COMMON_OBJECT_HANDLE Handle,
    void *Buffer);


// Referenced objects

static HA_COMMON_OBJECT_HANDLE
        sf_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        sm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;

static struct {
    HA_COMMON_OBJECT_HANDLE     *handle;
    PMTC_S8                     name;
    HA_COMMON_OBJECT_CALLBACK   callback;
} objects[] =
{
    {&sf_object,        COM_ID_SF,      lm_sf_updated},
    {&sm_object,        COM_ID_SM,      lm_sm_updated},
    {NULL,              NULL,           NULL}
};


static struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    MTC_BOOLEAN     pending_request;
    MTC_BOOLEAN     cancel_request;
    MTC_BOOLEAN     terminate;
    MTC_BOOLEAN     first_cleanup_done;
    COM_DATA_SF     sf;
    COM_DATA_SM     sm;
} lmvar = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .pending_request = FALSE,
    .cancel_request = FALSE,
    .terminate = FALSE,
    .first_cleanup_done = FALSE,
};


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC MTC_S32
lm_open_objects();

MTC_STATIC void
lm_cleanup_objects();

MTC_STATIC void *
lock_mgr(
    void *ignore);

MTC_STATIC MTC_BOOLEAN
is_equal_or_prior_to_this_node(
    MTC_S32 node_index);

MTC_STATIC MTC_BOOLEAN
is_online(
    MTC_S32 node_index);

MTC_STATIC MTC_BOOLEAN
am_i_ready_to_request();

MTC_STATIC MTC_S32
get_lock_node();

MTC_STATIC MTC_BOOLEAN
is_locked(
    MTC_S32 node_index);

MTC_STATIC MTC_BOOLEAN
is_requesting(
    MTC_S32 node_index);

MTC_STATIC void
write_master_uuid(
    MTC_UUID uuidMaster);


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//

//
// Global functions (Lock manager interfaces)
//

// lm_initialize
//
//  Initialize lock manager.
//
//
//  paramaters
//
//  return value
//    0: success
//    not 0: fail
//

MTC_S32
lm_initialize(
    MTC_S32  phase)
{
    static pthread_t    lm_thread = 0;
    MTC_S32             ret;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "LM: lm_initialize(0).\n");

        memset(&lmvar.sf, 0, sizeof(lmvar.sf));
        memset(&lmvar.sm, 0, sizeof(lmvar.sm));

        // open common objects
        ret = lm_open_objects();
        if (ret)
        {
            goto error;
        }
        ret = MTC_SUCCESS;
        break;

    case 1:
        log_message(MTC_LOG_INFO, "LM: lm_initialize(1).\n");

        // start lock manager thread
        ret = pthread_create(&lm_thread, xhad_pthread_attr, lock_mgr, NULL);
        if (ret)
        {
            log_internal(MTC_LOG_ERR, "LM: cannot create pthread. (sys %d)\n", ret);
            ret = MTC_ERROR_LM_PTHREAD;
            goto error;
        }
        else
        {
            lmvar.terminate = FALSE;
            ret = MTC_SUCCESS;
        }
        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "LM: lm_initialize(-1).\n");

        if (lm_thread)
        {
            pthread_mutex_lock(&lmvar.mutex);
            lmvar.pending_request = FALSE;
            lmvar.cancel_request = TRUE;
            lmvar.terminate = TRUE;
            pthread_cond_broadcast(&lmvar.cond);
            pthread_mutex_unlock(&lmvar.mutex);

#if 0
            // wait for thread_termination
            ret = pthread_join(lm_thread, NULL);
            if (ret)
            {
                ret = pthread_kill(lm_thread, SIGKILL);
            }
#endif
        }

        // cleanup common object handlers
        lm_cleanup_objects();
        ret = MTC_SUCCESS;
        break;
    }
    return ret;

error:
    lm_cleanup_objects();
    return ret;
}


//
//  NAME:
//
//      lm_open_objects
//
//  DESCRIPTION:
//
//      The cleanup routine of heartbeat.
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//      Success - zero
//      Failure - nonzero
//
//
//  ENVIRONMENT:
//
//

MTC_STATIC MTC_S32
lm_open_objects()
{
    MTC_S32     index, ret;

    for (index = 0; objects[index].handle != NULL; index++)
    {
        // open common objects
        if (*(objects[index].handle) == HA_COMMON_OBJECT_INVALID_HANDLE_VALUE)
        {
            ret = com_open(objects[index].name, objects[index].handle);
            if (ret)
            {
                log_internal(MTC_LOG_ERR,
                             "LM: cannot open COM object (name = %s). (%d)\n",
                             objects[index].name, ret);
                objects[index].handle = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
                return ret;
            }
        }


        // register callbacks
        if (objects[index].callback)
        {
            ret = com_register_callback(*(objects[index].handle),
                                        objects[index].callback);
            if (ret)
            {
                log_internal(MTC_LOG_ERR,
                             "LM: cannot register callback to %s. (%d)\n",
                             objects[index].name, ret);
                return ret;
            }
        }
    }

    return MTC_SUCCESS;
}


//
//  NAME:
//
//      lm_cleanup_objects
//
//  DESCRIPTION:
//
//      The cleanup routine of heartbeat.
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//
//  ENVIRONMENT:
//
//

MTC_STATIC void
lm_cleanup_objects()
{
#if 0
    MTC_S32 index;

    for (index = 0; objects[index].handle; index++)
    {
        if (objects[index].callback)
        {
            com_deregister_callback(*(objects[index].handle), objects[index].callback);
        }

        if (*(objects[index].handle) != HA_COMMON_OBJECT_INVALID_HANDLE_VALUE)
        {
            if (!com_close(*(objects[index].handle)))
            {
                objects[index].handle = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
            }
        }
    }

    return;
#endif
}


// lm_initialize_lm_fields
//
//  Initialize lock manager fields in State-File.
//
//
//  paramaters
//
//  return value
//
//

void
lm_initialize_lm_fields(
    PCOM_DATA_SF psf)
{
    MTC_S32 index;

    for (index = 0; index < MAX_HOST_NUM; index++)
    {
        psf->lm[index].request = FALSE;
        MTC_HOSTMAP_INIT_RESET(psf->lm[index].grant);
    }

    psf->lm[_my_index].request = FALSE;
    MTC_HOSTMAP_INIT_SET(psf->lm[_my_index].grant);

    return;
}


// lm_request_lock
//
//  Try to acquire the lock.  If the calling host successfully acquires
//  the lock, then this function returns TRUE.  If one of other nodes
//  acquires it, this function returns FALSE.
//  If the lock has been already acquired the calling host when this
//  function is called, this function returns TRUE.
//
//
//  paramaters
//    uuidMaster: UUID of this host.  If this host acquires the lock,
//                this UUID is writen in the Master field of State-File.
//
//  return value
//    TRUE: Locked by this host
//    FALSE: Locked by other host
//

MTC_BOOLEAN
lm_request_lock(
    MTC_UUID uuidMaster)
{
    MTC_S32 index;

    log_maskable_debug_message(LM_TRACE, "LM: enter lm_request_lock.\n");

    // Wait until the first cleanup done
    pthread_mutex_lock(&lmvar.mutex);
    while (!lmvar.first_cleanup_done)
    {
        pthread_cond_wait(&lmvar.cond, &lmvar.mutex);
    }
    pthread_mutex_unlock(&lmvar.mutex);


    // Someone has locked?
    pthread_mutex_lock(&lmvar.mutex);
    index = get_lock_node();
    pthread_mutex_unlock(&lmvar.mutex);

    if (index == _my_index)
    {
        log_maskable_debug_message(LM_TRACE, "LM: I already have the lock.\n");

        // just make sure the master field is filled with this UUID
        write_master_uuid(uuidMaster);
        return TRUE;
    }
    else if (index != INVALID_NODE)
    {
        log_maskable_debug_message(LM_TRACE, "LM: host (%d) has the lock.\n", index);
        return FALSE;
    }

    // request lock
    pthread_mutex_lock(&lmvar.mutex);
    lmvar.pending_request = TRUE;
    pthread_cond_broadcast(&lmvar.cond);
    pthread_mutex_unlock(&lmvar.mutex);

    hb_SF_accelerate();

    // wait until someone acquires the lock or the request is canceled
    pthread_mutex_lock(&lmvar.mutex);
    while ((index = get_lock_node()) == INVALID_NODE)
    {
        // wait until state-file is updated or request status is changed
        pthread_cond_wait(&lmvar.cond, &lmvar.mutex);

        // check if the request is canceled or this node lost SF accessibility
        if (!is_requesting(_my_index) || !lmvar.sf.SF_access)
        {
            log_maskable_debug_message(LM_TRACE, 
                "LM: lock request canceled, (my request, SF_access) = (%s, %s).\n",
                (is_requesting(_my_index))? "T": "F", (lmvar.sf.SF_access)? "T": "F");
            break;
        }
    }
    pthread_mutex_unlock(&lmvar.mutex);

    hb_SF_cancel_accelerate();

    if (index == _my_index)
    {
        log_maskable_debug_message(LM_TRACE, "LM: I have acquired the lock.\n");

        // I got the lock!
        write_master_uuid(uuidMaster);
        return TRUE;
    }
    else
    {
        log_maskable_debug_message(LM_TRACE, "LM: host (%d) has acquired the lock.\n", index);

        // I couldn't get the lock
        pthread_mutex_lock(&lmvar.mutex);
        lmvar.cancel_request = TRUE;
        pthread_cond_broadcast(&lmvar.cond);
        pthread_mutex_unlock(&lmvar.mutex);
        return FALSE;
    }

    // never fall down here
    assert(FALSE);
}


// lm_cancel_lock
//
//  Release the lock.
//
//
//  paramaters
//
//  return value
//
//

void
lm_cancel_lock()
{
    log_maskable_debug_message(LM_TRACE, "LM: enter lm_cancel_lock.\n");

    // cancel request
    pthread_mutex_lock(&lmvar.mutex);
    lmvar.cancel_request = TRUE;
    pthread_cond_broadcast(&lmvar.cond);
    pthread_mutex_unlock(&lmvar.mutex);

    // wait until cancel is acknoledged
    pthread_mutex_lock(&lmvar.mutex);
    while (lmvar.cancel_request)
    {
        pthread_cond_wait(&lmvar.cond, &lmvar.mutex);
    }
    pthread_mutex_unlock(&lmvar.mutex);

    log_maskable_debug_message(LM_TRACE, "LM: cancel acknowledged.\n");

    return;
}


//
// Local functions
//

// lock_mgr
//
//  Lock manager thread
//
//
//  paramaters
//
//  return value
//
//

MTC_STATIC void *
lock_mgr(
    void *ignore)
{
    PCOM_DATA_SF    psf;
    MTC_S32         index;

    log_message(MTC_LOG_INFO, "LM: thread ID: %ld.\n", syscall(SYS_gettid));
    while (TRUE)
    {
        // wait until state-file is updated or request status is changed
        pthread_mutex_lock(&lmvar.mutex);
        pthread_cond_wait(&lmvar.cond, &lmvar.mutex);
        if (lmvar.terminate)
        {
            pthread_mutex_unlock(&lmvar.mutex);
            break;
        }

        if (!lmvar.sf.SF_access || !is_online(_my_index))
        {
            pthread_mutex_unlock(&lmvar.mutex);
            continue;
        }

        pthread_mutex_unlock(&lmvar.mutex);

        com_writer_lock(sf_object, (void **) &psf);
        pthread_mutex_lock(&lmvar.mutex);

        // cancelling my grant flags when the request is canceled
        for (index = 0; _is_configured_host(index); index++)
        {
            if (!lmvar.sf.lm[index].request || !is_online(index))
            {
                if (MTC_HOSTMAP_ISON(psf->lm[_my_index].grant, index))
                {
                    log_maskable_debug_message(LM_TRACE,
                        "LM: the GRANT flag for host (%d) has turned to FALSE."
                        " (request, online) = (%s, %s), cancel detected.\n",
                        index,
                        (lmvar.sf.lm[index].request)? "T": "F",
                        (is_online(index))? "T": "F");
                }
                MTC_HOSTMAP_RESET(psf->lm[_my_index].grant, index);
            }
        }

        // processing my requests
        if (lmvar.pending_request && am_i_ready_to_request())
        {
            log_maskable_debug_message(LM_TRACE,
                         "LM: my REQUEST flag has turned to TRUE,"
                         " (request, ready to request) = (%s, %s).\n",
                         (lmvar.pending_request)? "T": "F",
                         (am_i_ready_to_request())? "T": "F");
            psf->lm[_my_index].request = TRUE;
            lmvar.pending_request = FALSE;

            // broadcast to notify that pending_request is updated
            pthread_cond_broadcast(&lmvar.cond);
        }
        if (lmvar.cancel_request)
        {
            log_maskable_debug_message(LM_TRACE, 
                         "LM: my REQUEST flag has turned to FALSE.\n");

            psf->lm[_my_index].request = FALSE;

            lmvar.pending_request = FALSE;
            lmvar.cancel_request = FALSE;

            // broadcast that pending_request and cancel_request are updated
            pthread_cond_broadcast(&lmvar.cond);
        }

        // granting to others
        for (index = 0; _is_configured_host(index); index++)
        {
            if (lmvar.sf.lm[index].request && is_online(index))
            {
                if (!psf->lm[_my_index].request ||
                    is_equal_or_prior_to_this_node(index))
                {
                    if (!MTC_HOSTMAP_ISON(psf->lm[_my_index].grant, index))
                    {
                        log_maskable_debug_message(LM_TRACE, 
                            "LM: the GRANT flag for host (%d) has turned to TRUE,"
                            " (request, online) = (%s, %s), I am granting.\n",
                            index,
                            (lmvar.sf.lm[index].request)? "T": "F",
                            (is_online(index))? "T": "F");
                    }

                    MTC_HOSTMAP_SET(psf->lm[_my_index].grant, index);
                }
            }
        }

        lmvar.first_cleanup_done = TRUE;
        pthread_cond_broadcast(&lmvar.cond);

        lmvar.sf = *psf;
        pthread_mutex_unlock(&lmvar.mutex);
        com_writer_unlock(sf_object);
        mssleep(100);
    }

    return NULL;
}


// lm_sf_updated
//
//  This is callback procedure called when the State-File information
//  is updated.
//
//
//  paramaters
//   Handle: Object handle
//   Buffer: State-File data
//
//  return value
//
//  environment
//   The State-File object must be locked as Writer.
//

MTC_STATIC void
lm_sf_updated(
    HA_COMMON_OBJECT_HANDLE Handle,
    void *Buffer)
{
    assert(Handle == sf_object);

    // state-file is updated, then cache it, and
    // broadcast to notify that state-file is updated

    pthread_mutex_lock(&lmvar.mutex);
    lmvar.sf = *((PCOM_DATA_SF) Buffer);
    pthread_cond_broadcast(&lmvar.cond);
    pthread_mutex_unlock(&lmvar.mutex);

    return;
}


// lm_sm_updated
//
//  This is callback procedure called when the State Manager information
//  is updated.
//
//
//  paramaters
//   Handle: Object handle
//   Buffer: State Manager data
//
//  return value
//
//  environment
//   The State Manager object must be locked as Writer.
//

MTC_STATIC void
lm_sm_updated(
    HA_COMMON_OBJECT_HANDLE Handle,
    void *Buffer)
{
    assert(Handle == sm_object);

    // state-file is updated, then cache it, and
    // broadcast to notify that state-file is updated

    pthread_mutex_lock(&lmvar.mutex);
    lmvar.sm = *((PCOM_DATA_SM) Buffer);
    pthread_cond_broadcast(&lmvar.cond);
    pthread_mutex_unlock(&lmvar.mutex);

    return;
}


// is_equal_or_prior_to_this_node
//
//  This function returns TRUE if the specified host has equal or higher
//  priority than this (calling) host.
//
//
//  paramaters
//   node_index: specify host by node index
//
//  return value
//   TRUE: specified host has equal or higher priority
//   FALSE specified host has lower priority
//
//  environment
//   This function can be called any condition
//

MTC_STATIC MTC_BOOLEAN
is_equal_or_prior_to_this_node(
    MTC_S32 node_index)
{
    return (_my_index >= node_index)? TRUE: FALSE;
}



// is_online
//
//  This function returns TRUE if the specified host is online
//
//
//  paramaters
//   node_index: specify host by node index
//
//  return value
//   TRUE: specified host is online
//   FALSE specified host is offline
//
//  environment
//   lock_mgr_mutex must be locked before calling this function
//

MTC_STATIC MTC_BOOLEAN
is_online(
    MTC_S32 node_index)
{
	return MTC_HOSTMAP_ISON(lmvar.sm.current_liveset, node_index);
}


// am_i_ready_to_request
//
//  This function returns TRUE if calling host is allowed to request the lock
//
//
//  paramaters
//   node_index: specify host by node index
//
//  return value
//   TRUE: the calling host can request the lock
//   FALSE the calling host is not allowed to request the lock
//
//  environment
//   lock_mgr_mutex must be locked before calling this function
//

MTC_STATIC MTC_BOOLEAN
am_i_ready_to_request()
{
    MTC_S32     index;
    MTC_BOOLEAN ret = TRUE;

    // make sure my cancellation is processed
    for (index = 0; _is_configured_host(index); index++)
    {
        ret = ret &&
              (is_online(index)?
                    !MTC_HOSTMAP_ISON(lmvar.sf.lm[index].grant, _my_index): TRUE);
    }

    // make sure I don't grant the lock to lower priority node
    for (index = 0; _is_configured_host(index); index++)
    {
		ret = ret &&
              ((!is_equal_or_prior_to_this_node(index) && is_online(index))?
                    !MTC_HOSTMAP_ISON(lmvar.sf.lm[_my_index].grant, index): TRUE);
    }

    return ret;
}


// get_lock_node
//
//  This function returns host index that has the lock
//
//
//  paramaters
//
//
//  return value
//   Host index: if a host has the lock
//   INVALID_NODE: if no host has the lock
//
//  environment
//   lock_mgr_mutex must be locked before calling this function
//   (since is_locked() requires)
//

MTC_STATIC MTC_S32
get_lock_node()
{
    MTC_S32 index;

    for (index = 0; _is_configured_host(index); index++)
    {
        if (is_locked(index))
        {
            return index;
        }
    }

    // no one has the lock.
    return INVALID_NODE;
}


// is_locked
//
//  This function returns TRUE if the specified host has the lock
//
//
//  paramaters
//
//
//  return value
//   TRUE: The specified host has the lock
//   FALSE: The specified host does not have the lock
//
//  environment
//   lock_mgr_mutex must be locked before calling this function
//

MTC_STATIC MTC_BOOLEAN
is_locked(
    MTC_S32 node_index)
{
    MTC_S32 index;
    MTC_BOOLEAN ret;

    // return true if the specified node is online and
    // the lock is granted by all online nodes.

    ret = is_online(node_index)? TRUE: FALSE;
    for (index = 0; _is_configured_host(index); index++)
    {
		ret = ret &&
              (is_online(index)?
                    MTC_HOSTMAP_ISON(lmvar.sf.lm[index].grant, node_index): TRUE);
    }

    return ret;
}


// is_requesting
//
//  This function returns TRUE if the specified host is requesting the lock
//
//
//  paramaters
//   node_index: specify host by node index
//
//  return value
//   TRUE: the specified host is requesting the lock
//   FALSE the specified host is not requesting the lock
//
//  environment
//   lock_mgr_mutex must be locked before calling this function
//

MTC_STATIC MTC_BOOLEAN
is_requesting(
    MTC_S32 node_index)
{
    return lmvar.pending_request || lmvar.sf.lm[node_index].request;
}


// write_master_uuid
//
//  This function writes Master UUID field of State-File information
//
//
//  paramaters
//   uuidMaster: Master UUID to be written in State-File
//
//  return value
//
//  note
//   This function locks the writer lock of State-File object.
//

MTC_STATIC void
write_master_uuid(
    MTC_UUID uuidMaster)
{
    sf_set_master(uuidMaster);

    return;
}

