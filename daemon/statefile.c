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
//      This module is responsible for managing the State-File
//      utilized for XenServer HA.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      March 12, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/syscall.h>

//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "com.h"
#include "config.h"
#include "sm.h"
#include "xapi_mon.h"
#include "watchdog.h"
#include "xha.h"
#include "statefile.h"
#include "lock_mgr.h"
#include "fist.h"

//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

//
//
//  L O C A L   D E F I N I T I O N S
//
//

#define SF_IOATTEMPTS   3       //  I/O retries
#define SLEEP_INTERVAL  500     //  500ms
#define ACCELERATED_ACCESS_INTERVAL (SLEEP_INTERVAL - 100)

//  State-File buffer

static STATE_FILE StateFile __attribute__ ((aligned (IOALIGN)));

//  Referenced objects

MTC_STATIC void
sf_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

static HA_COMMON_OBJECT_HANDLE
        hb_object =      HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        sf_object =      HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        xapimon_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        sm_object =      HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;

static struct {
    PHA_COMMON_OBJECT_HANDLE    phandle;
    PMTC_S8                     name;
    HA_COMMON_OBJECT_CALLBACK   callback;
} objects[] = {
    {&hb_object,        COM_ID_HB,      NULL},
    {&sf_object,        COM_ID_SF,      sf_sf_updated},
    {&xapimon_object,   COM_ID_XAPIMON, NULL},
    {&sm_object,        COM_ID_SM,      NULL},
};

//
//  Internal data for this module
//

static struct {
    int                 sfdesc;                 //  File descriptor for the State-File
    MTC_BOOLEAN         terminate;
    MTC_U32             sequence;
    pthread_t sf_thread;

    struct {
        pthread_spinlock_t  lock;               // spinlock to serialize accesses
                                                // to this structure
        MTC_BOOLEAN         SF_access;
        WATCHDOG_HANDLE     watchdog;           // watchdog handle
        struct {
            MTC_CLOCK       ms;                 // SF access interval in ms.
            MTC_U32         accelerate_count;   // number of stacked acceleration
        } interval;
    };
    struct {
        MTC_BOOLEAN readonce;
        MTC_U32     sequence;   
        MTC_CLOCK   readclock;      // clock(ms) as of the last read
        MTC_CLOCK   updateclock;    // clock(ms) as of when the local host noticed
                                    // an update in sequence number
    } hoststat[MAX_HOST_NUM];
    struct _latency {
        MTC_S32 last;
        MTC_S32 max;
        MTC_S32 min;
    } readlatency, writelatency;
} sfvar = { 0 };

//  lock
#define sf_lock()   ((void)pthread_spin_lock(&sfvar.lock))
#define sf_unlock() ((void)pthread_spin_unlock(&sfvar.lock))

//
//  FIST points
//

#define FIST_global_read() \
( \
    (fist_on("sf.ioerror.sticky") || fist_on("sf.ioerror.once")) \
        ?  MTC_ERROR_SF_IO_ERROR\
        :  (fist_on("sf.checksum.sticky") || fist_on("sf.checksum.once")) \
            ? MTC_ERROR_SF_CORRUPTION \
            : fist_on("sf.version") \
                ? MTC_ERROR_SF_VERSION_MISMATCH \
                : fist_on("sf.genuuid") \
                    ? MTC_ERROR_SF_GEN_UUID \
                    : MTC_SUCCESS \
)

#define FIST_global_write() \
( \
    (fist_on("sf.ioerror.sticky") || fist_on("sf.ioerror.once")) \
        ? MTC_ERROR_SF_IO_ERROR \
        : MTC_SUCCESS \
)

#define FIST_hostspecific_read() \
( \
    (fist_on("sf.ioerror.sticky") || fist_on("sf.ioerror.once")) \
        ? MTC_ERROR_SF_IO_ERROR \
        : (fist_on("sf.checksum.sticky") || fist_on("sf.checksum.once")) \
            ? MTC_ERROR_SF_CORRUPTION \
            : MTC_SUCCESS \
)

#define FIST_hostspecific_write() \
( \
    (fist_on("sf.ioerror.sticky") || fist_on("sf.ioerror.once")) \
        ? MTC_ERROR_SF_IO_ERROR \
        : MTC_SUCCESS \
)

//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC  MTC_STATUS
sf_initialize0();

MTC_STATIC  MTC_STATUS
sf_initialize1();

MTC_STATIC  void *
sfthread(
    void *ignore);

MTC_STATIC  void
sf_cleanup_objects();

MTC_STATIC  MTC_STATUS
readsf();

MTC_STATIC  MTC_STATUS
write_hostspecific();

MTC_STATIC  void
sf_wakeupthread();

MTC_STATIC  int
sf_rand();

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//


MTC_STATUS
sf_initialize(
    MTC_S32  phase)
{
    MTC_STATUS status = MTC_SUCCESS;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        status = sf_initialize0();
        break;

    case 1:
        status = sf_initialize1();
        break;

    case -1:
        if (sfvar.sf_thread)
        {
            sf_lock();
            if (sfvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
            {
                status = watchdog_close(sfvar.watchdog);
                sfvar.watchdog = INVALID_WATCHDOG_HANDLE_VALUE;
            }
            sf_unlock();

#if 0   // do not do a graceful thread shutdown for fast termination
            sfvar.terminate = TRUE;

            // wait for thread termination
            if ((status = pthread_join(sfvar.sf_thread, NULL)))
            {
                status = pthread_kill(sfvar.sf_thread, SIGKILL);
            }
#endif
        }

        // this cleanup may close objects that will
        // still be used by the thread (note we are not killig thread any more)

        // cleanup common object handlers
        sf_cleanup_objects();
        break;

    default:
        assert(FALSE);
    }

    return status;
}


MTC_STATIC  MTC_STATUS
sf_initialize0()
{
    int index, host, host2;
    MTC_STATUS  status;
    int syscall_status;
    COM_DATA_SF sfobj;

    log_message(MTC_LOG_INFO,
                "SF: phase 0 initialization...\n");

    //  Initialize sfvar

    sfvar.sfdesc = -1,
    sfvar.watchdog = INVALID_WATCHDOG_HANDLE_VALUE,
    sfvar.terminate = FALSE,

    sfvar.readlatency.max = sfvar.writelatency.max = -1;
    sfvar.readlatency.min = sfvar.writelatency.min = -1;

    sfvar.SF_access = FALSE;
    sfvar.interval.ms = _t2 * 1000;

    for (host = 0; host < MAX_HOST_NUM; host++)
    {
        sfvar.hoststat[host].readonce = FALSE;
        sfvar.hoststat[host].readclock = -1;
        sfvar.hoststat[host].updateclock = -1;
    }

    if ((syscall_status = pthread_spin_init(&sfvar.lock, PTHREAD_PROCESS_PRIVATE)) != 0)
    {
        log_internal(MTC_LOG_ERR, "SF: cannot initialize spinlock (sys %d).\n", syscall_status);
        status = MTC_ERROR_SF_PTHREAD;
        goto error;
    }

    // initialize sfobj, which is
    // used as a template to initialize SF object

    sfobj.modified_mask = SF_MODIFIED_MASK_NONE;
    sfobj.ctl.enable_SF_write = FALSE;
    sfobj.ctl.enable_SF_read = FALSE;
    for (host = 0; host < MAX_HOST_NUM; host++)
    {
        sfobj.time_last_SF[host] = -1;
    }
    lm_initialize_lm_fields(&sfobj);
    bzero(&sfobj.master, sizeof(sfobj.master));
    sfobj.SF_access = FALSE;
    sfobj.SF_corrupted = FALSE;
    MTC_HOSTMAP_INIT_RESET(sfobj.excluded);
    MTC_HOSTMAP_INIT_RESET(sfobj.sfdomain);
    MTC_HOSTMAP_INIT_RESET(sfobj.starting);
    sfobj.pool_state = SF_STATE_NONE;

    for (host = 0; host < MAX_HOST_NUM; host++)
    {
        MTC_HOSTMAP_INIT_RESET(sfobj.raw[host].current_liveset);
        MTC_HOSTMAP_INIT_RESET(sfobj.raw[host].proposed_liveset);
        MTC_HOSTMAP_INIT_RESET(sfobj.raw[host].hbdomain);
        MTC_HOSTMAP_INIT_RESET(sfobj.raw[host].sfdomain);
        for (host2 = 0; host2 < MAX_HOST_NUM; host2++)
        {
            sfobj.raw[host].time_since_last_HB_receipt[host2] = -1;
            sfobj.raw[host].time_since_last_SF_update[host2] = -1;
        }
        sfobj.raw[host].time_since_xapi_restart = -1;
    }

    sfobj.latency = sfobj.latency_max = sfobj.latency_min = -1;

    sfobj.fencing = FENCING_ARMED;

    // create common object

    status = com_create(COM_ID_SF, &sf_object, sizeof(COM_DATA_SF), &sfobj);

    if (status != MTC_SUCCESS)
    {
        log_internal(MTC_LOG_ERR, "SF: cannot create COM object. (%d)\n", status);
        sf_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
        goto error;
    }

    // open common objects & register callbacks

    for (index = 0; index < sizeof(objects) / sizeof(objects[0]); index++)
    {
        // open common objects

        if (*(objects[index].phandle) == HA_COMMON_OBJECT_INVALID_HANDLE_VALUE)
        {
            status = com_open(objects[index].name, objects[index].phandle);
            if (status != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR,
                            "SF: cannot open COM object (name = %s). (%d)\n", objects[index].name, status);
                *(objects[index].phandle) = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
                goto error;
            }
        }

        // register callbacks
        if (objects[index].callback)
        {
            status = com_register_callback(*(objects[index].phandle), objects[index].callback);
            if (status != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR,
                            "SF: cannot register callback on the COM object (%s). (%d)\n", objects[index].name, status);
                goto error;
            }
        }
    }

    //  Open the State-File

    if ((sfvar.sfdesc = sf_open(_sf_path)) < 0)
    {
        sfvar.sfdesc = -1;
        status = MTC_ERROR_SF_OPEN;
        log_internal(MTC_LOG_ERR,
                    "SF: cannot open the State-File %s.\n", _sf_path);
        goto error;
    }

    return MTC_SUCCESS;

error:

    sf_cleanup_objects();

    return status;
}


MTC_STATIC  MTC_STATUS
sf_initialize1()
{
    MTC_STATUS status;

    log_message(MTC_LOG_INFO,
                "SF: phase 1 initialization...\n");

    if ((status = watchdog_create("statefile", &sfvar.watchdog)) != MTC_SUCCESS)
    {
        return status;
    }

    // start statefile thread

    sfvar.terminate = FALSE;
    if (pthread_create(&sfvar.sf_thread, xhad_pthread_attr, sfthread, NULL))
    {
        return MTC_ERROR_SF_PTHREAD;
    }

    return MTC_SUCCESS;
}


MTC_STATIC  void
sf_cleanup_objects()
{
#if 0
    int index;

    if (sfvar.sfdesc >= 0)
    {
        sf_close(sfvar.sfdesc);
        sfvar.sfdesc = -1;
    }

    for (index = 0; index < sizeof(objects) / sizeof(objects[0]); index++)

    {
        if (objects[index].callback)
        {
            com_deregister_callback(*(objects[index].phandle), objects[index].callback);
        }

        if (*(objects[index].phandle) != HA_COMMON_OBJECT_INVALID_HANDLE_VALUE)
        {
            if (com_close(*(objects[index].phandle)) == MTC_SUCCESS)
            {
                *(objects[index].phandle) = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
            }
        }
    }
#endif
}

//
//  sfthread -
//
//  Main thread of SF
//

MTC_STATIC  void *
sfthread(
    void *ignore)
{
    MTC_CLOCK last, now, target;
    PCOM_DATA_SF psf;
    MTC_BOOLEAN readable, writable;
    MTC_STATUS status;
    MTC_BOOLEAN read_once = FALSE;

#define PSTATUS_NONE    0
#define PSTATUS_SUCCESS 1
#define PSTATUS_ERROR   2

    log_message(MTC_LOG_INFO, "SF: thread ID: %ld.\n", syscall(SYS_gettid));
    int print_status = PSTATUS_NONE;

    last = _getms();
    do
    {
        now = _getms();
        target = sfvar.interval.ms;

        if (sfvar.interval.accelerate_count == 0)   // lock is not critical here
        {
            target += sf_rand();
            if (target <= 0)
            {
                target = sfvar.interval.ms;
            }
        }

        if (now - last < target)
        {
            sf_sleep(SLEEP_INTERVAL);
            continue;   // reevaluate the eligibility
        }

        //  Refresh watchdog counter to Ws, if the fencing
        //  for the state-file is armed.

        sf_lock();
        if (sfvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
        {
            (void)watchdog_set(sfvar.watchdog, _Ws);
        }
        sf_unlock();

        com_writer_lock(sf_object, (void **) &psf);
        writable = psf->ctl.enable_SF_write;
        readable = psf->ctl.enable_SF_read;
        com_writer_unlock(sf_object);

        //  Now it's time to start this cycle of State-File access

        last = now;

        //  Gather information from HB and SM and write updated
        //  information to the host-specific element

        if (writable && read_once)
        {
            status = write_hostspecific();
            
            if (status != MTC_SUCCESS)
            {
                if (print_status != PSTATUS_ERROR)
                {
                    print_status = PSTATUS_ERROR;
                    log_status(status, "while writing a host-specific element");
                }
            }
        }

        if (fist_on("sf.time.<T2 after write"))
        {
            sf_sleep((_T2 - _t2/4) * 1000);
        }

        //  Read the State-File and update relevant objects

        if (readable)
        {
            status = readsf();

            switch (status)
            {
            case    MTC_SUCCESS:
                read_once = TRUE;
                if (print_status != PSTATUS_SUCCESS)
                {
                    print_status = PSTATUS_SUCCESS;
                    log_message(MTC_LOG_NOTICE,
                                "HA daemon successfuly started acceesing the State-File.\n");
                }
                break;

            case    MTC_ERROR_SF_PENDING_WRITE:
                if ((status = FIST_global_write()) == MTC_SUCCESS)
                {
                    status = sf_writeglobal(sfvar.sfdesc, &StateFile.global);
                }

                if (status != MTC_SUCCESS && print_status != PSTATUS_ERROR)
                {
                    print_status = PSTATUS_ERROR;
                    log_status(status, "while updating the global section");
                }
                break;

            case    MTC_ERROR_SF_VERSION_MISMATCH:
                log_message(MTC_LOG_NOTICE,
                            "SF: State-File version mismatch. Initiating termination process.\n");
                main_terminate(status);
                return NULL;    // terminate this thread

            case    MTC_ERROR_SF_GEN_UUID:
                log_message(MTC_LOG_NOTICE,
                            "SF: State-File generation UUID mismatch. Initiating termination process.\n");
                main_terminate(status);
                return NULL;    // terminate this thread

            default:
                read_once = FALSE;
                if (print_status != PSTATUS_ERROR)
                {
                    print_status = PSTATUS_ERROR;
                    log_status(status, "while reading entire State-File");
                }
                break;
            }
        }
    } while (!sfvar.terminate);

    return NULL;
}

//
//  readsf -
//
//  Read entire State-File and update SF objects accordingly.
//

MTC_STATIC  MTC_STATUS
readsf()
{
    MTC_STATUS status;
    int attempt, host_index, host_index2;
    PCOM_DATA_SM        psm;
    PCOM_DATA_SF        psf;
    MTC_S32 max, min;
    struct {
        MTC_STATUS  global_section;
        MTC_STATUS  host_section[MAX_HOST_NUM];
    } iostatus;

    iostatus.global_section = MTC_ERROR_UNDEFINED;
    for (host_index = 0; host_index < _num_host; host_index++)
    {
        iostatus.host_section[host_index] = MTC_ERROR_UNDEFINED;
    }

    //  Attempto to read entire State-File.
    //  Retry applies.

    status = MTC_ERROR_UNDEFINED;

    for (attempt = 0; attempt < SF_IOATTEMPTS; attempt++)
    {
        if (iostatus.global_section != MTC_SUCCESS)
        {
            if ((iostatus.global_section = FIST_global_read()) == MTC_SUCCESS)
            {
                iostatus.global_section = sf_readglobal(sfvar.sfdesc, &StateFile.global, _gen_UUID);
            }
        }

        for (host_index = 0; host_index < _num_host; host_index++)
        {
            if (iostatus.host_section[host_index] != MTC_SUCCESS)
            {
                if ((iostatus.host_section[host_index] = FIST_hostspecific_read()) == MTC_SUCCESS)
                {
                    iostatus.host_section[host_index] =
                            sf_readhostspecific(sfvar.sfdesc, host_index, &StateFile.host[host_index]);
                }

                if (iostatus.host_section[host_index] == MTC_SUCCESS)
                {
                    //  update hoststat for this host

                    sfvar.hoststat[host_index].readclock = _getms();
                    if (sfvar.hoststat[host_index].readonce == FALSE)
                    {
                        sfvar.hoststat[host_index].readonce = TRUE;
                        sfvar.hoststat[host_index].sequence = StateFile.host[host_index].data.sequence;

                        //  If this is the local host, initialize the
                        //  sequence number used in sf_writehostspecific, which
                        //  surely happens only after the first successful read.

                        if (host_index == _my_index)
                        {
                            sfvar.sequence = sfvar.hoststat[host_index].sequence;
                        }
                    }
                    else if (sfvar.hoststat[host_index].sequence != StateFile.host[host_index].data.sequence)
                    {
                        sfvar.hoststat[host_index].sequence = StateFile.host[host_index].data.sequence;
                        sfvar.hoststat[host_index].updateclock = sfvar.hoststat[host_index].readclock;

                    }
                }
            }
        }

        if ((status = iostatus.global_section) == MTC_SUCCESS)
        {
            for (host_index = 0; _is_configured_host(host_index); host_index++)
            {
                if ((status = iostatus.host_section[host_index]) != MTC_SUCCESS)
                {
                    break;
                }
            }

            if (status == MTC_SUCCESS)
            {
                break;
            }
        }

        sleep(1);
    }
    
    //  State-File is sccessfully read, or the attempt failed after retries.
    //  Update SF objects.

    com_reader_lock(sm_object, (void **) &psm);
    com_writer_lock(sf_object, (void **) &psf);

    if (status == MTC_SUCCESS)
    {
        for (host_index = 0; _is_configured_host(host_index); host_index++)
        {
            //  lock manager data

            if (host_index != _my_index)
            {
                psf->lm[host_index].request = (StateFile.host[host_index].data.lock_request ? TRUE: FALSE);
                MTC_HOSTMAP_COPY(psf->lm[host_index].grant, StateFile.host[host_index].data.lock_grant);
            }

            //  excluded

            if (host_index != _my_index ||
                (psf->modified_mask & SF_MODIFIED_MASK_EXCLUDED) == 0)
            {
                MTC_HOSTMAP_SET_BOOLEAN(psf->excluded,
                                        host_index,
                                        StateFile.host[host_index].data.excluded);
            }

            //  starting

            MTC_HOSTMAP_SET_BOOLEAN(psf->starting,
                                    host_index,
                                    StateFile.host[host_index].data.starting);

            //  raw

            MTC_HOSTMAP_COPY(psf->raw[host_index].current_liveset,
                             StateFile.host[host_index].data.current_liveset);
            MTC_HOSTMAP_COPY(psf->raw[host_index].proposed_liveset,
                             StateFile.host[host_index].data.proposed_liveset);
            MTC_HOSTMAP_COPY(psf->raw[host_index].hbdomain,
                             StateFile.host[host_index].data.hbdomain);
            MTC_HOSTMAP_COPY(psf->raw[host_index].sfdomain,
                             StateFile.host[host_index].data.sfdomain);

            for (host_index2 = 0; _is_configured_host(host_index2); host_index2++)
            {
                psf->raw[host_index].time_since_last_HB_receipt[host_index2] =
                    StateFile.host[host_index].data.since_last_hb_receipt[host_index2];
                psf->raw[host_index].time_since_last_SF_update[host_index2] =
                    StateFile.host[host_index].data.since_last_sf_update[host_index2];
            }
            psf->raw[host_index].time_since_xapi_restart =
                StateFile.host[host_index].data.since_xapi_restart_first_attempted;

            //  time_last_SF

            // if the peer thinks he is not alive but I think he is alive, 
            // the peer must be booting before finishing the fault handler,
            // then let's wait until the fault handler finish its job.
            if (!(!MTC_HOSTMAP_ISON(psf->raw[host_index].current_liveset, host_index) &&
                  MTC_HOSTMAP_ISON(psm->current_liveset, host_index)))
            {
                psf->time_last_SF[host_index] = sfvar.hoststat[host_index].updateclock;
            }

            //  version 1.1 Collect SM-phase and commited weight of the other hosts
            if (host_index == _my_index)
            {
                psf->sm_phase[host_index] = psm->phase;
                psf->weight[host_index] = psm->commited_weight;
            }
            else
            {
                psf->sm_phase[host_index] = StateFile.host[host_index].data.sm_phase;
                psf->weight[host_index] = StateFile.host[host_index].data.weight;
            }
        }

        // latency

        psf->latency = _max(sfvar.readlatency.last, sfvar.writelatency.last);
        
        max = _max(sfvar.readlatency.max, sfvar.writelatency.max);
        min = _min(sfvar.readlatency.min, sfvar.writelatency.min);

        if (max > psf->latency_max)
        {
            psf->latency_max = max;
        }
        
        if (psf->latency_min < 0 || min < psf->latency_min)
        {
            psf->latency_min = min;
        }

        sfvar.readlatency.max = sfvar.writelatency.max = -1;
        sfvar.readlatency.min = sfvar.writelatency.min = -1;

        //  SF_access
        sf_lock();
        psf->SF_access = sfvar.SF_access = TRUE;
        sf_unlock();

        //  master
        if (psf->modified_mask & SF_MODIFIED_MASK_MASTER)
        {
            psf->modified_mask &= ~SF_MODIFIED_MASK_MASTER;
            status = MTC_ERROR_SF_PENDING_WRITE;
            UUID_cpy(StateFile.global.data.master, psf->master);
        }
        else
        {
            UUID_cpy(psf->master, StateFile.global.data.master);
        }

        //  pool state
        if (psf->modified_mask & SF_MODIFIED_MASK_POOL_STATE)
        {
            psf->modified_mask &= ~SF_MODIFIED_MASK_POOL_STATE;
            status = MTC_ERROR_SF_PENDING_WRITE;
            StateFile.global.data.pool_state = psf->pool_state;
        }
        else
        {
            psf->pool_state = StateFile.global.data.pool_state;
        }
    }
    else
    {
        MTC_BOOLEAN corrupted = FALSE;

        if (iostatus.global_section == MTC_ERROR_SF_GEN_UUID)
        {
            status = MTC_ERROR_SF_GEN_UUID;
        }
        else if (iostatus.global_section == MTC_ERROR_SF_VERSION_MISMATCH)
        {
            status = MTC_ERROR_SF_VERSION_MISMATCH;
        }
        else
        {
            if (iostatus.global_section == MTC_ERROR_SF_CORRUPTION)
            {
                corrupted = TRUE;
            }
            else
            {
                for (host_index = 0; _is_configured_host(host_index); host_index++)
                {
                    if (iostatus.host_section[host_index] == MTC_ERROR_SF_CORRUPTION)
                    {
                        corrupted = TRUE;
                        break;
                    }
                }
            }
        }

        sf_lock();
        psf->SF_access = sfvar.SF_access = FALSE;
        sfvar.interval.accelerate_count = 0;
        sfvar.interval.ms = _t2 * 1000;
        sf_unlock();

        psf->SF_corrupted = corrupted;
    }

    com_writer_unlock(sf_object);
    com_reader_unlock(sm_object);

    return status;
}

//
//  write_hostspecific -
//
//  Update the host specific element for the local host
//

MTC_STATIC  MTC_STATUS
write_hostspecific()
{
    PCOM_DATA_SM        psm;
    PCOM_DATA_HB        phb;
    PCOM_DATA_SF        psf;
    PCOM_DATA_XAPIMON   pxapimon;
    PSF_HOST_SPECIFIC_SECTION phost;
    MTC_CLOCK now;
    int host;
    MTC_STATUS status;
    MTC_BOOLEAN excluded_pending_write;

    phost = &StateFile.host[_my_index];

    phost->data.sequence = sfvar.sequence++;
    phost->data.host_index = _my_index;
    UUID_cpy(phost->data.host_uuid, _my_UUID);

    // acquire reader_lock

    com_reader_lock(sm_object, (void **) &psm);
    com_reader_lock(hb_object, (void **) &phb);
    com_writer_lock(sf_object, (void **) &psf);
    com_reader_lock(xapimon_object, (void **) &pxapimon);

    now = _getms();

    // copy from state manager

    excluded_pending_write = ((psf->modified_mask & SF_MODIFIED_MASK_EXCLUDED)? TRUE: FALSE);
    psf->modified_mask &= ~SF_MODIFIED_MASK_EXCLUDED;
    phost->data.excluded = (MTC_HOSTMAP_ISON(psf->excluded, _my_index)? TRUE: FALSE);

    MTC_HOSTMAP_COPY(phost->data.current_liveset, psm->current_liveset);
    MTC_HOSTMAP_COPY(phost->data.proposed_liveset, psm->proposed_liveset);
    MTC_HOSTMAP_COPY(phost->data.hbdomain, phb->hbdomain);
    MTC_HOSTMAP_COPY(phost->data.sfdomain, psf->sfdomain);

    //  version 1.1 Propagate SM-phase and commited weight to
    //  the other hosts
    phost->data.sm_phase = psf->sm_phase[_my_index] = psm->phase;
    phost->data.weight = psf->weight[_my_index] = psm->commited_weight;

    for (host = 0; _is_configured_host(host); host++)
    {
        phost->data.since_last_hb_receipt[host] = (phb->time_last_HB[host] < 0
                                                    ? -1
                                                    : now - phb->time_last_HB[host]);
        phost->data.since_last_sf_update[host] = (psf->time_last_SF[host] < 0
                                                    ? -1
                                                    : now - psf->time_last_SF[host]);
    }
    phost->data.since_xapi_restart_first_attempted = (pxapimon->time_Xapi_restart < 0
                                                        ? -1
                                                        : (now - pxapimon->time_Xapi_restart));

    // psf->lm is kept up-to-date by the lock manager
    // phost->data.lockrequest
    // phost->data.lockgrant
    phost->data.lock_request = psf->lm[_my_index].request? TRUE: FALSE;
    MTC_HOSTMAP_COPY(phost->data.lock_grant, psf->lm[_my_index].grant);


    // starting

    phost->data.starting = psf->ctl.starting;

    // release reader_locks
    com_reader_unlock(xapimon_object);
    com_writer_unlock(sf_object);
    com_reader_unlock(hb_object);
    com_reader_unlock(sm_object);

    if ((status = FIST_hostspecific_write()) == MTC_SUCCESS)
    {
        status = sf_writehostspecific(sfvar.sfdesc, _my_index, phost);
    }

    if (status != MTC_SUCCESS)
    {
        com_writer_lock(sf_object, (void **) &psf);

        if (excluded_pending_write)
        {
            psf->modified_mask |= SF_MODIFIED_MASK_EXCLUDED;
        }

        sf_lock();
        psf->SF_access = sfvar.SF_access = FALSE;
        sfvar.interval.accelerate_count = 0;
        sfvar.interval.ms = _t2 * 1000;
        sf_unlock();

        com_writer_unlock(sf_object);
    }

    return status;
}

//
//  sf_reportlatency -
//
//  Called from State-File access libraries (statefileio.c) to
//  report the latency of the last access (read or write)
//

void
sf_reportlatency(
    MTC_CLOCK latency,
    MTC_BOOLEAN write)
{
    struct _latency *lp;

    assert(latency >= 0);

    lp = (write? &sfvar.writelatency: &sfvar.readlatency);

    lp->last = latency;
    if (latency > lp->max)
    {
        lp->max = latency;
    }
    if (lp->min < 0 || latency < lp->min)
    {
        lp->min= latency;
    }
}


//
//  sf_set_pool_state -
//
//  Modify pool state from init->active or any->invalid.
//  Actual state transition occurs in the statefile thread.
//

MTC_STATUS
sf_set_pool_state(
    MTC_U32 pool_state)
{
    PCOM_DATA_SF    psf;

    com_writer_lock(sf_object, (void **) &psf);
    psf->modified_mask |= SF_MODIFIED_MASK_POOL_STATE;
    psf->pool_state = pool_state;
    com_writer_unlock(sf_object);

    sf_wakeupthread();

    return MTC_SUCCESS;
}

//
//  sf_set_master -
//
//  Modify master UUID in the State-File.
//  Actual modification of the master UUID occurs in the statefile thread.
//

MTC_STATUS
sf_set_master(
    MTC_UUID master)
{
    PCOM_DATA_SF    psf;

    com_writer_lock(sf_object, (void **) &psf);
    psf->modified_mask |= SF_MODIFIED_MASK_MASTER;
    UUID_cpy(psf->master, master);
    com_writer_unlock(sf_object);

    sf_wakeupthread();

    return MTC_SUCCESS;
}

//
//  sf_set_excluded -
//
//  Modify the excluded flag in the State-File.
//  Actual modification of the excluded flag occurs in the statefile thread.
//

MTC_STATUS
sf_set_excluded(
    MTC_BOOLEAN excluded)
{
    PCOM_DATA_SF    psf;

    com_writer_lock(sf_object, (void **) &psf);
    psf->modified_mask |= SF_MODIFIED_MASK_EXCLUDED;
    MTC_HOSTMAP_SET_BOOLEAN(psf->excluded, _my_index, excluded);
    com_writer_unlock(sf_object);

    sf_wakeupthread();

    return MTC_SUCCESS;
}

//
//  sf_sf_updated -
//
//  Called from COM when the SF object is updated. The call is
//  made on the context of write-lock-owner. The write-lock is
//  still retained (for the calling thread).
//

MTC_STATIC  void
sf_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    PCOM_DATA_SF psf = buffer;

    switch (psf->fencing)
    {
    case FENCING_ARMED:
    case FENCING_DISARMED:
        break;

    case FENCING_DISARM_REQUESTED:
        psf->fencing = FENCING_DISARMED;

        sf_lock();
        if (sfvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
        {
            (void)watchdog_close(sfvar.watchdog);
            sfvar.watchdog = INVALID_WATCHDOG_HANDLE_VALUE;
        }
        sf_unlock();
        break;

    case FENCING_NONE:
    default:
            log_internal(MTC_LOG_ERR, "SF: illegal fencing request %d - ignored.\n", psf->fencing);
            assert(FALSE);
            break;
    }
}

//
//  sf_wakeupthread -
//
//  Wakes up the SF thread, if it's blocked now.
//
//  Currently this function does nothing. The SF thread
//  always sleeps for 500ms and it appears to be short enough.
//

MTC_STATIC  void
sf_wakeupthread()
{
}


//
//  sf_accelerate -
//
//  Wakes up the SF thread and accelerates frequency of accesses to the State-File.
//

void
sf_accelerate()
{
    MTC_U32 acceleration = 0;

    sf_lock();
    if (sfvar.SF_access == TRUE)
    {
        acceleration = ++sfvar.interval.accelerate_count;
        sfvar.interval.ms = ACCELERATED_ACCESS_INTERVAL;
    }
    sf_unlock();

    // wakeup SF thread if this is the first acceleration
    
    if (acceleration == 1)
    {
        sf_wakeupthread();
    }
}

//
//  sf_cancel_acceleration -
//
//  Cancel previously accelerated access to the SF.
//

void
sf_cancel_acceleration()
{
    sf_lock();
    if (sfvar.SF_access)
    {
#if 0
        if (sfvar.interval.accelerate_count &&
            --sfvar.interval.accelerate_count == 0)
        {
            sfvar.interval.ms = _t2 * 1000;
        }
#else
        sfvar.interval.accelerate_count = 0;
        sfvar.interval.ms = _t2 * 1000;
#endif
    }
    sf_unlock();
}

//
//  sf_rand -
//
//  Generate a random number in the range of -500:500.
//

MTC_STATIC  int
sf_rand()
{
    static MTC_BOOLEAN first = TRUE;
    int i, r, d;

    if (first)
    {
        int sum = 0;
        first = FALSE;

        for (i = 0; i < sizeof(_my_UUID); i++)
        {
            sum += _my_UUID[i];
        }
        srand(sum);
    }

    r = rand() - RAND_MAX / 2;
    d = RAND_MAX / 1000;
    r /= d;
    return r;
}


//
//  sf_watchdog_set -
//
//  touch watchdog
//

void
sf_watchdog_set()
{
    sf_lock();
    if (sfvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
    {
        (void)watchdog_set(sfvar.watchdog, _Ws);
    }
    sf_unlock();
}
