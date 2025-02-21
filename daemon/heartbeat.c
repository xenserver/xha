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
//      This is heartbeat module
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: 
//
//      March 05, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "log.h"
#include "mtcerrno.h"
#include "config.h"
#include "heartbeat.h"
#include "com.h"
#include "sm.h"
#include "xapi_mon.h"
#include "watchdog.h"
#include "statefile.h"
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

MTC_STATIC void
hb_hb_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
hb_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
hb_xapimon_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
hb_sm_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);


//
//  Utility macros
//

#define arraycpy(X, Y)  memcpy(X, Y, (sizeof(X) < sizeof(Y))? sizeof(X): sizeof(Y))

#define hb_spin_lock() ({                   \
    MTC_S32 ret;                            \
    ret = pthread_spin_lock(&hbvar.lock);   \
    assert(ret == 0); })

#define hb_spin_unlock() ({                 \
    MTC_S32 ret;                            \
    ret = pthread_spin_unlock(&hbvar.lock); \
    assert(ret == 0); })


//
//
//  L O C A L   D E F I N I T I O N S
//
//


//
// Heartbeat packet definition
//

#define HB_SIG          'hahx'

typedef struct _HB_PACKET {
    MTC_U32     signature;                                  // 4 bytes
    MTC_U32     checksum;                                   // 4 bytes
    MTC_U32     sequence;                                   // 4 bytes
    MTC_U32     size;                                       // 4 bytes
    MTC_UUID    generation_uuid;                            // 32 bytes
    MTC_UUID    host_uuid;                                  // 32 bytes
    MTC_U32     host_index;                                 // 4 bytes

    MTC_HOSTMAP current_liveset;                            // 8 bytes
    MTC_HOSTMAP proposed_liveset;                           // 8 bytes
    MTC_HOSTMAP hbdomain;                                   // 8 bytes
    MTC_HOSTMAP sfdomain;                                   // 8 bytes

    MTC_S32     time_since_last_HB_receipt[MAX_HOST_NUM];   // 256 bytes
    MTC_S32     time_since_last_SF_update[MAX_HOST_NUM];    // 256 bytes
    MTC_S32     time_since_xapi_restart;                    // 4 bytes

    SM_PHASE    sm_phase;                                   // 4 bytes

    MTC_S8      err_string[XAPI_MAX_ERROR_STRING_LEN + 1];  // 101 bytes

    MTC_BOOLEAN SF_access;                                  // 1 byte
    MTC_BOOLEAN SF_corrupted;                               // 1 byte
    MTC_BOOLEAN SF_accelerate;                              // 1 byte
    MTC_BOOLEAN fence_request;                              // 1 byte
    MTC_BOOLEAN SR2;                                        // 1 byte
    MTC_BOOLEAN joining;                                    // 1 byte

    MTC_S8      padding[1];                                 // 1 byte
}   HB_PACKET, *PHB_PACKET;                                 // total 744 bytes

MTC_ASSERT_SIZE(sizeof(HB_PACKET) == 744);


// Referenced objects

static HA_COMMON_OBJECT_HANDLE
        hb_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        sf_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        xapimon_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE,
        sm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;

static struct {
    HA_COMMON_OBJECT_HANDLE     *handle;
    PMTC_S8                     name;
    HA_COMMON_OBJECT_CALLBACK   callback;
} objects[] =
{
    {&hb_object,        COM_ID_HB,      hb_hb_updated},
    {&sf_object,        COM_ID_SF,      hb_sf_updated},
    {&xapimon_object,   COM_ID_XAPIMON, hb_xapimon_updated},
    {&sm_object,        COM_ID_SM,      hb_sm_updated},
    {NULL,              NULL,           NULL}
};


//
//  Internal data for this module
//

static struct {
    MTC_S32             socket;
    socket_address      sa_to[MAX_HOST_NUM];
    WATCHDOG_HANDLE     watchdog;
    MTC_BOOLEAN         terminate;
    pthread_spinlock_t  lock;
    MTC_U32             sequence[MAX_HOST_NUM];
} hbvar = {
    .socket = -1,
    .sa_to = {},
    .watchdog = INVALID_WATCHDOG_HANDLE_VALUE,
    .terminate = FALSE,
    .sequence = {0},
};



#define HB_SFACCELARATION_REQUEST_RETRY (3)


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC MTC_S32
hb_initialize0();

MTC_STATIC  MTC_S32
hb_open_objects();

MTC_STATIC  void
hb_cleanup_objects();

MTC_STATIC  void *
hb(
    void *ignore);

MTC_STATIC  MTC_BOOLEAN
update_hbdomain();

MTC_STATIC  void
send_hb();

MTC_STATIC  void
receive_hb();

MTC_STATIC  MTC_BOOLEAN
is_valid_hb(
    PHB_PACKET p,
    MTC_S32 size);

MTC_STATIC MTC_BOOLEAN
hb_check_fist(
    MTC_S8  fistpoint[]);

MTC_STATIC void
hb_check_fist_sticky();


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//


//
//  NAME:
//
//      hb_initialize
//
//  DESCRIPTION:
//
//      The initialization function of heartbeat, invoked by main.
//
//  FORMAL PARAMETERS:
//
//      phase:
//          0 - phase 0 initialization
//          1 - phase 1 initialization
//          -1 - termination request
//          
//  RETURN VALUE:
//
//      Success - zero
//      Failure - nonzero
//
//  ENVIRONMENT:
//
//

MTC_S32
hb_initialize(
    MTC_S32  phase)
{
    static pthread_t    hb_thread = 0;
    MTC_S32             ret = MTC_SUCCESS;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "HB: hb_initialize(0).\n");

        ret = pthread_spin_init(&hbvar.lock, PTHREAD_PROCESS_PRIVATE);
        if (ret)
        {
            ret = MTC_ERROR_HB_PTHREAD;
            goto error;
        }

        ret = hb_initialize0();

        break;

    case 1:
        log_message(MTC_LOG_INFO, "HB: hb_initialize(1).\n");

        // open watchdog
        ret = watchdog_create("heartbeat", &hbvar.watchdog);
        if (ret != MTC_SUCCESS)
        {
            hbvar.watchdog = INVALID_WATCHDOG_HANDLE_VALUE;
            goto error;
        }

        // start heartbeat thread
        hbvar.terminate = FALSE;
        ret = pthread_create(&hb_thread, xhad_pthread_attr, hb, NULL);
        if (ret)
        {
            ret = MTC_ERROR_HB_PTHREAD;
            goto error;
        }

        ret = MTC_SUCCESS;

        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "HB: hb_initialize(-1).\n");

        if (hb_thread)
        {
#if 0
            hb_spin_lock();
            hbvar.terminate = TRUE;
            hb_spin_unlock();

            // wait for thread termination
            if ((ret = pthread_join(hb_thread, NULL)))
            {
                pthread_kill(hb_thread, SIGKILL);
            }
#endif
        }

        if (hbvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
        {
            watchdog_close(hbvar.watchdog);
        }

        // cleanup common object handlers
        hb_cleanup_objects();

        pthread_spin_destroy(&hbvar.lock);

        ret = MTC_SUCCESS;

        break;
    }

    return ret;

error:
    hb_cleanup_objects();
    return ret;
}


//
//  NAME:
//
//      hb_initialize0
//
//  DESCRIPTION:
//
//      The phase 0 initialization routine of heartbeat.
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//      Success - zero
//      Failure - nonzero
//
//  ENVIRONMENT:
//
//

MTC_STATIC  MTC_S32
hb_initialize0()
{
    MTC_S32     ret;

    // initialize COM_DATA_HB
    {
        MTC_S32     index, index2;
        COM_DATA_HB hb = {
            .ctl.enable_HB_send = FALSE,
            .ctl.enable_HB_receive = FALSE,
            .ctl.join = FALSE,
            .ctl.fence_request = FALSE,
            .latency = -1,
            .latency_max = -1,
            .latency_min = -1,
            .fencing = FENCING_ARMED};

        for (index = 0; index < MAX_HOST_NUM; index++)
        {
            MTC_HOSTMAP_INIT_RESET(hb.raw[index].current_liveset);
            MTC_HOSTMAP_INIT_RESET(hb.raw[index].proposed_liveset);
            MTC_HOSTMAP_INIT_RESET(hb.raw[index].hbdomain);
            MTC_HOSTMAP_INIT_RESET(hb.raw[index].sfdomain);
            hb.sm_phase[index] = SM_PHASE_STARTING;
            hb.time_last_HB[index] = -1;
            for (index2 = 0; index2 < MAX_HOST_NUM; index2++)
            {
                hb.raw[index].time_since_last_HB_receipt[index2] = -1;
                hb.raw[index].time_since_last_SF_update[index2] = -1;
            }
            hb.raw[index].time_since_xapi_restart = -1;
            memset(hb.err_string[index], 0, sizeof(hb.err_string[index]));
        }
        MTC_HOSTMAP_INIT_RESET(hb.hbdomain);
        MTC_HOSTMAP_INIT_RESET(hb.notjoining);

        // create common object
        ret = com_create(COM_ID_HB, &hb_object, sizeof(COM_DATA_HB), &hb);
        if (ret)
        {
            log_internal(MTC_LOG_ERR, "HB: cannot create COM object. (%d)\n", ret);
            hb_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
            goto error;
        }
    }

    // open common objects & register callbacks
    ret = hb_open_objects();
    if (ret)
    {
        goto error;
    }

    // open & bind socket
    {
        size_t sock_len = 0;
        socket_address *ss = &_host_info[_my_index].sock_address;
        switch (ss->sa.sa_family)
        {
            case AF_INET: {
                struct sockaddr_in *sa = &ss->sa_in;
                sa->sin_port = htons(_udp_port);
                sock_len = sizeof *sa;
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 *sa6 = &ss->sa_in6;
                sa6->sin6_port = htons(_udp_port);
                sock_len = sizeof *sa6;
                break;
            }
            default:
                log_internal(MTC_LOG_ERR, "HB: Cannot create socket, invalid socket family (%d).\n", ss->sa.sa_family);
                ret = MTC_ERROR_HB_SOCKET;
                goto error;
        }

        hbvar.socket = socket(ss->sa.sa_family, SOCK_DGRAM, 0);
        if (hbvar.socket < 0)
        {
            log_internal(MTC_LOG_ERR, "HB: cannot create socket. (sys %d)\n", errno);
            ret = MTC_ERROR_HB_SOCKET;
            goto error;
        }
        if (setsockopt(hbvar.socket, SOL_SOCKET, SO_BINDTODEVICE,
                       _hb_interface, strlen(_hb_interface) + 1))
        {
            log_internal(MTC_LOG_ERR,
                "HB: cannot set sockopt (BINDTODEVICE device name = %s). (sys %d)\n",
                _hb_interface, errno);
            ret = MTC_ERROR_HB_SOCKET;
            goto error;
        }
        if (bind(hbvar.socket, &ss->sa, sock_len))
        {
            const int error = errno;
            char ip_address[INET6_ADDRSTRLEN];
            if (inet_ntop(ss->sa.sa_family, ss, ip_address, sizeof ip_address))
            {
                log_internal(MTC_LOG_ERR,
                   "HB: cannot bind socket address (IP address = %s:%d). (sys %d)\n",
                   ip_address, _udp_port, error);
            }
            else
            {
                log_internal(MTC_LOG_ERR,
                   "HB: cannot bind socket address (unable to get IP address). (sys %d)\n",
                   error);
            }

            ret = MTC_ERROR_HB_SOCKET;
            goto error;
        }
    }

    // create socket addresses for sendto()
    {
        MTC_S32     index;

        for (index = 0; _is_configured_host(index); index++)
        {
            socket_address *ss = &_host_info[index].sock_address;

            memset(&hbvar.sa_to[index], 0, sizeof(hbvar.sa_to[index]));
            switch (ss->sa.sa_family)
            {
                case AF_INET: {
                    struct sockaddr_in *sa = &hbvar.sa_to[index].sa_in;
                    sa->sin_family = AF_INET;
                    sa->sin_port = htons(_udp_port);
                    sa->sin_addr.s_addr = ss->sa_in.sin_addr.s_addr;
                    break;
                }
                case AF_INET6: {
                    struct sockaddr_in6 *sa6 = &hbvar.sa_to[index].sa_in6;
                    sa6->sin6_family = AF_INET6;
                    sa6->sin6_port = htons(_udp_port);
                    memcpy(&sa6->sin6_addr.s6_addr, ss->sa_in6.sin6_addr.s6_addr, sizeof sa6->sin6_addr.s6_addr);
                    break;
                }
                default:
                    assert(FALSE); // Already checked during config parsing.
                    ret = MTC_ERROR_HB_SOCKET;
                    goto error;
            }
        }
    }

    return MTC_SUCCESS;

error:
    hb_cleanup_objects();

    return ret;
}


//
//  NAME:
//
//      hb_open_objects
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

MTC_STATIC  MTC_S32
hb_open_objects()
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
                            "HB: cannot open COM object (name = %s). (%d)\n",
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
                            "HB: cannot register callback on the COM object (%s). (%d)\n",
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
//      hb_cleanup_objects
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

MTC_STATIC  void
hb_cleanup_objects()
{
#if 0
    MTC_S32 index;

    if (hbvar.socket != -1)
    {
        close(hbvar.socket);
        hbvar.socket = -1;
    }

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


MTC_STATIC  void
hb_hb_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    PCOM_DATA_HB    phb = buffer;

    switch (phb->fencing)
    {
    case FENCING_DISARM_REQUESTED:
        phb->fencing = FENCING_DISARMED;

        hb_spin_lock();
        if (hbvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
        {
            (void)watchdog_close(hbvar.watchdog);
            hbvar.watchdog = INVALID_WATCHDOG_HANDLE_VALUE;
        }
        hb_spin_unlock();
        break;

    case FENCING_ARMED:
    case FENCING_DISARMED:
        break;

    case FENCING_NONE:
    default:
        log_internal(MTC_LOG_ERR,
            "HB: illegal fencing request %d - ignored.\n", phb->fencing);
        assert(FALSE);
        break;
    }
}


MTC_STATIC  void
hb_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    ;
}

MTC_STATIC  void
hb_xapimon_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    ;
}

MTC_STATIC  void
hb_sm_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    ;
}


//
//  NAME:
//
//      hb
//
//  DESCRIPTION:
//
//      The heartbeat main thread.
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

MTC_STATIC  void *
hb(
    void *ignore)
{
    MTC_BOOLEAN         term = FALSE;
    MTC_CLOCK           last, now;

    now = last = _getms();
    do
    {
        log_maskable_debug_message(TRACE, "HB: heartbeat thread activity log.\n");

        if (update_hbdomain())
        {
            start_fh(FALSE);
        }

        // receive heartbeat
        {
            fd_set          fds;
            MTC_S32         nfds = -1;
            struct timeval  wait;

            FD_ZERO(&fds);
            FD_SET(hbvar.socket, &fds);
            nfds = _max(nfds, hbvar.socket);

            wait = mstotv((_t1 * ONE_SEC - (now - last) < 0)? 0: _t1 * ONE_SEC - (now - last));
            if (select(nfds + 1, &fds, NULL, NULL, &wait) > 0)
            {
                receive_hb();
            }
        }

        // time to send haertbeat?
        now = _getms();
        if (now - last < _t1 * ONE_SEC)
        {
            // no, still have to wait.
            continue;
            // note: the variable 'now' is used in next cycle
        }
        // yes, it's time to send heartbeat

        // check fist
        hb_check_fist_sticky();

        // send heartbeat
        send_hb();

        // calculate and store diagnostic values
        {
            PCOM_DATA_HB    phb;

            now = _getms();

            com_writer_lock(hb_object, (void **) &phb);
            phb->time_last_HB[_my_index] = now;
            phb->latency = now - last;
            phb->latency_max = (phb->latency_max < 0)? phb->latency:
                                _max(phb->latency_max, phb->latency);
            phb->latency_min = (phb->latency_min < 0)? phb->latency:
                                _min(phb->latency_min, phb->latency);
            com_writer_unlock(hb_object);
        }

        last = now;

        hb_spin_lock();
        //  Refresh watchdog counter to Wh
        if (hbvar.watchdog != INVALID_WATCHDOG_HANDLE_VALUE)
        {
            watchdog_set(hbvar.watchdog, _Wh);
        }
        term = hbvar.terminate;
        hb_spin_unlock();

        // note: the variable 'now' is used in next cycle
    } while (!term);

    return NULL;
}


MTC_STATIC  MTC_BOOLEAN
update_hbdomain()
{
    MTC_CLOCK       now;
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    MTC_S8          hostmap[MAX_HOST_NUM + 1] = {0};
    MTC_BOOLEAN     changed = FALSE;
    MTC_S32         index;

    now = _getms();
    com_reader_lock(sm_object, (void **) &psm);
    com_writer_lock(hb_object, (void **) &phb);
    for (index = 0; _is_configured_host(index); index++)
    {
        if (index == _my_index)
        {
            MTC_HOSTMAP_SET(phb->hbdomain, index);
            hostmap[index] = 'm';
        }
        else if (phb->time_last_HB[index] >= 0 &&
                 now - phb->time_last_HB[index] < _T1 * ONE_SEC)
        {
            if (MTC_HOSTMAP_ISON(phb->hbdomain, index))
            {
                if (phb->sm_phase[index] >= SM_PHASE_FHREADY &&
                    psm->phase >= SM_PHASE_FHREADY &&
                    !MTC_HOSTMAP_ISON(phb->raw[index].hbdomain, _my_index))
                {
                    MTC_HOSTMAP_RESET(phb->hbdomain, index);
                    hostmap[index] = 'd';
                    changed = TRUE;
                }
                else
                {
                    hostmap[index] = '1';
                }
            }
            else
            {
                if (!sm_get_join_block())
                {
                    MTC_HOSTMAP_SET(phb->hbdomain, index);
                    hostmap[index] = '@';
                    changed = TRUE;
                }
                else
                {
                    hostmap[index] = 'b';
                }
            }
        }
        else
        {
            hbvar.sequence[index] = 0;
            if (!MTC_HOSTMAP_ISON(phb->hbdomain, index))
            {
                hostmap[index] = '0';
            }
            else
            {
                MTC_HOSTMAP_RESET(phb->hbdomain, index);
                hostmap[index] = '_';
                changed = TRUE;
            }
        }
    }
    hostmap[index] = '\0';
    com_writer_unlock(hb_object);
    com_reader_unlock(sm_object);

    if (changed)
    {
        log_message(MTC_LOG_DEBUG,
            "HB: HB domain is updated [hbdomain = (%s)].\n", hostmap);
    }

    return changed;
}


//
//  NAME:
//
//      send_hb
//
//  DESCRIPTION:
//
//      The send heartbeat packet routine, this function is called by heartbeat
//      main thread.
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

MTC_STATIC  void
send_hb()
{
    MTC_S32     index;
    HB_PACKET   pkt = {0};

    {
        MTC_BOOLEAN     enable_HB_send;

        {
            PCOM_DATA_HB    phb;

            com_reader_lock(hb_object, (void **) &phb);
            enable_HB_send = phb->ctl.enable_HB_send;
            com_reader_unlock(hb_object);
        }

        if (!enable_HB_send)
        {
            return;
        }
    }

    // compose hearbeat packet
    pkt.signature = HB_SIG;
    pkt.size = sizeof(pkt);
    UUID_cpy(pkt.generation_uuid, _gen_UUID);
    UUID_cpy(pkt.host_uuid, _my_UUID);
    pkt.host_index = _my_index;

    {
        PCOM_DATA_SM        psm;
        PCOM_DATA_HB        phb;
        PCOM_DATA_SF        psf;
        PCOM_DATA_XAPIMON   pxapimon;

        MTC_CLOCK           now;

        // sequence number
        hb_spin_lock();
        pkt.sequence = ++(hbvar.sequence[_my_index]);
        hb_spin_unlock();

        // acquire reader_lock
        com_reader_lock(sm_object, (void **) &psm);
        com_writer_lock(hb_object, (void **) &phb);
        com_reader_lock(sf_object, (void **) &psf);
        com_reader_lock(xapimon_object, (void **) &pxapimon);

        // SF accelerate
        pkt.SF_accelerate = phb->SF_accelerate;

        // liveset information
        MTC_HOSTMAP_COPY(pkt.current_liveset, psm->current_liveset);
        MTC_HOSTMAP_COPY(pkt.proposed_liveset, psm->proposed_liveset);
        MTC_HOSTMAP_COPY(pkt.hbdomain, phb->hbdomain);
        MTC_HOSTMAP_COPY(pkt.sfdomain, psf->sfdomain);

        // joining
        pkt.joining = phb->ctl.join;

        // State Manager information
        pkt.sm_phase = phb->sm_phase[_my_index] = psm->phase;
        // SR2 flag is requried to copy over heartbeat.
        // When a new host (probably excluded host) is joining liveset that is
        // surviving by SR2, the flag need to be copyed to the new host.
        pkt.SR2 = psm->SR2;

        // State File information
        pkt.SF_access = psf->SF_access;
        pkt.SF_corrupted = psf->SF_corrupted;

        // get current time;
        now = _getms();
        for (index = 0; _is_configured_host(index); index++)
        {
            pkt.time_since_last_HB_receipt[index] = (phb->time_last_HB[index] < 0)?
                                                    -1: (now - phb->time_last_HB[index]);
            pkt.time_since_last_SF_update[index] = (psf->time_last_SF[index] < 0)?
                                                   -1: (now - psf->time_last_SF[index]);
        }
        pkt.time_since_xapi_restart = (pxapimon->time_Xapi_restart < 0)?
                                      -1: (now - pxapimon->time_Xapi_restart);

        // xapi error string
        strncpy(pkt.err_string, pxapimon->err_string, sizeof(pkt.err_string));

        // fence request
        pkt.fence_request = phb->ctl.fence_request;

        // release reaer_locks
        com_reader_unlock(xapimon_object);
        com_reader_unlock(sf_object);
        com_writer_unlock(hb_object);
        com_reader_unlock(sm_object);
    }

    log_maskable_debug_message(TRACE, "HB: sending a heartbeat packet.\n");
    maskable_dump(DUMPPACKET, (void *) &pkt, sizeof(pkt));


    // send hearbeat packet
    if (!hb_check_fist("hb.isolate") &&
        !fist_on("hb.send.lostpacket"))
    {
        const socket_address *ss = &hbvar.sa_to[index];
        const socklen_t dest_len = ss->sa.sa_family == AF_INET ? sizeof(ss->sa_in) : sizeof(ss->sa_in6);

        for (index = 0; _is_configured_host(index); index++)
        {
            if (index != _my_index)
            {
                const int ret = sendto(hbvar.socket, &pkt, sizeof(pkt), 0, &hbvar.sa_to[index].sa, dest_len);
                if (ret == -1)
                {
                    log_message(MTC_LOG_ERR, "HB: sendto() failed. (sys %d)\n", errno);
                }
            }
        }
    }
}


//
//  NAME:
//
//      receive_hb
//
//  DESCRIPTION:
//
//      The receive heartbeat packet routine, this function is called by heartbeat
//      main thread.
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

MTC_STATIC  void
receive_hb()
{
    HB_PACKET           pkt;
    MTC_CLOCK           now;
    MTC_S32             fm_index;
    static MTC_BOOLEAN  recvfrom_failed = FALSE,
                        invalid_packet_recvd = FALSE,
                        need_fh = FALSE;
    static MTC_CLOCK    time_invalid_packet_recvd = 0;

    // Receiving a packet
    {
        socket_address  from;
        socklen_t       len = sizeof(from);
        MTC_S32         recvd;

        recvd = recvfrom(hbvar.socket, &pkt, sizeof(pkt), MSG_DONTWAIT,
                         &from.sa, &len);
        if (recvd < 0 && errno == EAGAIN)
        {
            // Linux may returns EAGAIN even if the socket is selected.
            // see also man page of select(2).
            return;
        }
        else if (recvd < 0 && !recvfrom_failed)
        {
            recvfrom_failed = TRUE;
            log_message(MTC_LOG_WARNING, "HB: recvfrom() failed. (sys %d)\n", errno);
            return;
        }

        if (recvfrom_failed)
        {
            recvfrom_failed = FALSE;
            log_message(MTC_LOG_INFO, "HB: recvfrom() recovered.\n");
        }
        now = _getms();

        // check enable_HB_receive flag
        {
            MTC_BOOLEAN     enable_HB_receive;
            PCOM_DATA_HB    phb;

            com_reader_lock(hb_object, (void **) &phb);
            enable_HB_receive = phb->ctl.enable_HB_receive;
            com_reader_unlock(hb_object);

            if (!enable_HB_receive)
            {
                return;
            }
        }

        // check fist point
        if (hb_check_fist("hb.isolate"))
        {
            return;
        }
        if (fist_on("hb.receive.lostpacket"))
        {
            return;
        }

        // check received packet
        if (!is_valid_hb(&pkt, recvd))
        {
            // invalid packet
            if (!invalid_packet_recvd || now - time_invalid_packet_recvd > ONE_DAY)
            {
                invalid_packet_recvd = TRUE;
                time_invalid_packet_recvd = now;

                char ip_address[INET6_ADDRSTRLEN] = "<invalid_ip>";
                const int family = from.sa.sa_family;
                inet_ntop(family, &from, ip_address, sizeof ip_address);

                uint16_t port = 0;
                switch (family)
                {
                case AF_INET:
                    port = ntohs(from.sa_in.sin_port);
                    break;
                case AF_INET6:
                    port = ntohs(from.sa_in6.sin6_port);
                    break;
                default:
                    break; // Ignore...
                }

                log_message(MTC_LOG_WARNING,
                    "HB: invalid packet received from (%s:%d).\n",
                    ip_address, port);
                maskable_dump(DUMPPACKET, (PMTC_S8) &pkt, recvd);
            }

            return;
        }

        // packet is valid
        fm_index = pkt.host_index;

        // Check sequence number
        if (hbvar.sequence[fm_index] < pkt.sequence ||
            hbvar.sequence[fm_index] - pkt.sequence > 0x80000000)
        {
            hb_spin_lock();
            hbvar.sequence[fm_index] = pkt.sequence;
            hb_spin_unlock();
        }
        else
        {
            // ignore the old packet
            log_maskable_debug_message(TRACE,
                "HB: ignore an old packet from node (%d), sequence (remote:local) = (%d:%d).\n",
                fm_index, pkt.sequence, hbvar.sequence[fm_index]);
            return;
        }

        // Check if the sender of HB is now booting
        if (!MTC_HOSTMAP_ISON(pkt.current_liveset, fm_index))
        {
            PCOM_DATA_SM    psm;
            MTC_BOOLEAN     ignore;

            // If I am thinking the sender is still alive, ignore the packet
            // until Fault Handler finish the process.
            com_reader_lock(sm_object, (void **) &psm);
            ignore = MTC_HOSTMAP_ISON(psm->current_liveset, fm_index);
            com_reader_unlock(sm_object);

            if (ignore)
            {
                log_maskable_debug_message(TRACE,
                        "HB: ignore a packet from starting host (%d).\n", fm_index);
                return;
            }
        }

        log_maskable_debug_message(TRACE,
                     "HB: heartbeat received from host (%d).\n", fm_index);
        maskable_dump(DUMPPACKET, (PMTC_S8) &pkt, recvd);

        if (pkt.fence_request &&
            MTC_HOSTMAP_ISON(pkt.current_liveset, fm_index) &&
            !MTC_HOSTMAP_ISON(pkt.proposed_liveset, _my_index))
        {
            MTC_S8  error_string[256];

            snprintf(error_string, sizeof(error_string),
                     "Heartbeat: fencing is requested from host (%d).  - Self Fence", fm_index);
            self_fence(MTC_ERROR_HB_FENCEREQUESTED, error_string);
            // if returned, fencing is disarmed, then continue
        }
    }


    // sf accelarete
    if (pkt.SF_accelerate)
    {
        sf_accelerate();
    }
    else
    {
        sf_cancel_acceleration();
    }

    // Update HB data
    {
        PCOM_DATA_SM    psm;
        PCOM_DATA_HB    phb;
        MTC_S32         index;

        // START - SM_OBJECT, HB_OBJECT data update
        com_writer_lock(sm_object, (void **) &psm);
        com_writer_lock(hb_object, (void **) &phb);

        // since last HB receipt
        phb->time_last_HB[fm_index] = now;
        for (index = 0; index < MAX_HOST_NUM; index++)
        {
            phb->raw[_my_index].time_since_last_HB_receipt[index] =
                (phb->time_last_HB[index] < 0)? -1: now - phb->time_last_HB[index];
        }

        // liveset information
	    MTC_HOSTMAP_COPY(phb->raw[fm_index].current_liveset, pkt.current_liveset);
	    MTC_HOSTMAP_COPY(phb->raw[fm_index].proposed_liveset, pkt.proposed_liveset);
        MTC_HOSTMAP_COPY(phb->raw[fm_index].hbdomain, pkt.hbdomain);
        MTC_HOSTMAP_COPY(phb->raw[fm_index].sfdomain, pkt.sfdomain);

        // joining
        // this host map is used to gather hosts that are ready to start,
        // when a host is forming a new liveset 
        MTC_HOSTMAP_SET_BOOLEAN(phb->notjoining, fm_index, !pkt.joining);

        // State Manager information
        // SR2 flag is requried to copy over heartbeat.
        // When a new host (probably excluded host) is joining liveset that is
        // surviving by SR2, the flag need to be copyed to the new host.
        if (pkt.SR2)
        {
            psm->SR2 = pkt.SR2;
        }
        switch (phb->sm_phase[fm_index] = pkt.sm_phase)
        {
        case SM_PHASE_FHREADY:
            need_fh = TRUE;
            break;

        default:
            need_fh = FALSE;
            break;
        }

        // State File information
        MTC_HOSTMAP_SET_BOOLEAN(psm->sf_access, fm_index, pkt.SF_access);
        MTC_HOSTMAP_SET_BOOLEAN(psm->sf_corrupted, fm_index, pkt.SF_corrupted);

        // raw data
        arraycpy(phb->raw[fm_index].time_since_last_HB_receipt,
                 pkt.time_since_last_HB_receipt);
        arraycpy(phb->raw[fm_index].time_since_last_SF_update,
                 pkt.time_since_last_SF_update);
        phb->raw[fm_index].time_since_xapi_restart = pkt.time_since_xapi_restart;
        strncpy(phb->err_string[fm_index], pkt.err_string, sizeof(pkt.err_string));

        com_writer_unlock(hb_object);
        com_writer_unlock(sm_object);
        // END - HB_OBJECT, SM_OBJECT data update
    }

    // check fault handling request
    if (need_fh)
    {
        start_fh(TRUE);
    }
}


//
//  NAME:
//
//      is_valid_hb
//
//  DESCRIPTION:
//
//      The heartbeat validation check routine, this function check if the
//      specified packet is valid xHA packet.
//
//  FORMAL PARAMETERS:
//
//      ppkt - pointer to the packet
//      size - size of packet
//
//          
//  RETURN VALUE:
//
//      TRUE - if the packet is valid
//      FALSE - if the packet is invalid
//
//
//  ENVIRONMENT:
//
//

MTC_STATIC  MTC_BOOLEAN
is_valid_hb(PHB_PACKET ppkt, MTC_S32 size)
{
    // check packet
    if (size != sizeof(*ppkt))
    {
        return FALSE;
    }
    if (ppkt->signature != HB_SIG)
    {
        return FALSE;
    }
    if (ppkt->size != sizeof(*ppkt))
    {
        return FALSE;
    }
    if (UUID_comp(ppkt->generation_uuid, _gen_UUID))
    {
        return FALSE;
    }
    if (UUID_comp(ppkt->host_uuid, _host_info[ppkt->host_index].host_id))
    {
        return FALSE;
    }

    // the packet is good
    return TRUE;
}


void
hb_SF_accelerate()
{
    PCOM_DATA_HB    phb;

    sf_accelerate();

    com_writer_lock(hb_object, (void **) &phb);
    phb->SF_accelerate = TRUE;
    com_writer_unlock(hb_object);

    hb_send_hb_now(HB_SFACCELARATION_REQUEST_RETRY);
}


void
hb_SF_cancel_accelerate()
{
    PCOM_DATA_HB    phb;

    sf_cancel_acceleration();

    com_writer_lock(hb_object, (void **) &phb);
    phb->SF_accelerate = FALSE;
    com_writer_unlock(hb_object);

    hb_send_hb_now(HB_SFACCELARATION_REQUEST_RETRY);
}


void
hb_send_hb_now(
    MTC_S32 count)
{
    struct timespec sleep_ts, sleep_rem;
    MTC_S32         index;

    send_hb();

    for (index = 0; index < count - 1; index++)
    {
        sleep_ts = mstots(100);
        nanosleep(&sleep_ts, &sleep_rem);
        send_hb();
    }
}


MTC_STATIC MTC_BOOLEAN
hb_check_fist(
    MTC_S8  fistpoint[])
{
    MTC_BOOLEAN     isolated;
    MTC_CLOCK       target_delay;
    struct timespec ts, ts_rem;

    isolated = fist_on(fistpoint);

    if (fist_on("hb.time.<T1"))
    {
        target_delay = (_T1 - _t1) * ONE_SEC * 3 / 4;
    }
    else if (fist_on("hb.time.=T1"))
    {
        target_delay = (_T1 - _t1) * ONE_SEC;
    }
    else if (fist_on("hb.time.>T1<Wh"))
    {
        target_delay = (_Wh + _T1 - 2 * _t1) * ONE_SEC / 2;
    }
    else if (fist_on("hb.time.2Wh"))
    {
        target_delay = 2 * (_Wh - _t1) * ONE_SEC;
    }
    else
    {
        target_delay = 0;
    }

    if (target_delay != 0)
    {
        log_message(MTC_LOG_DEBUG,
                    "HB(FIST): heartbeat delay is %"PRId64" ms\n", target_delay);

        ts = ts_rem = mstots(target_delay);
        while (nanosleep(&ts, &ts_rem)) ts = ts_rem;
    }

    return isolated;
}

MTC_STATIC void
hb_check_fist_sticky()
{
    MTC_CLOCK       target_delay;
    struct timespec ts, ts_rem;

    if (fist_on("hb.time.<T1.sticky"))
    {
        target_delay = (_T1 - _t1) * ONE_SEC * 3 / 4;
    }
    else if (fist_on("hb.time.=T1/2+t1.sticky"))
    {
        target_delay = _T1 * ONE_SEC / 2;
    }
    else
    {
        target_delay = 0;
    }

    if (target_delay != 0)
    {
        log_message(MTC_LOG_DEBUG,
                    "HB(FIST): heartbeat delay is %"PRId64" ms\n", target_delay);

        ts = mstots(target_delay);
        while (nanosleep(&ts, &ts_rem)) ts = ts_rem;
    }
    return;
}
