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
//      This module is responsible for managing liveset information
//      by testing Boot criteria and Survival criteria.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: 
//
//      March 17, 2008
//
//   

//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
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
#include "sm.h"
#include "lock_mgr.h"
#include "statefile.h"
#include "xapi_mon.h"
#include "heartbeat.h"
#include "watchdog.h"
#include "fist.h"


#define RENDEZVOUS_FAULT_HANDLING   1

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
sm_hb_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
sm_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
sm_xapimon_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);

MTC_STATIC void
sm_sm_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer);


//
//
//  L O C A L   D E F I N I T I O N S
//
//

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
    {&hb_object,        COM_ID_HB,      sm_hb_updated},
    {&sf_object,        COM_ID_SF,      sm_sf_updated},
    {&xapimon_object,   COM_ID_XAPIMON, sm_xapimon_updated},
    {&sm_object,        COM_ID_SM,      sm_sm_updated},
    {NULL,              NULL,           NULL}
};

//
//  Internal data for this module
//

static struct {
    MTC_BOOLEAN         terminate;
    MTC_CLOCK           start_time;
    pthread_t           sm_thread;
    pthread_mutex_t     mutex;
    pthread_cond_t      cond;
    SM_PHASE            phase;
    MTC_BOOLEAN         need_fh;
    MTC_BOOLEAN         join_block;
    MTC_HOSTMAP         last_hbdomain;
    MTC_HOSTMAP         last_sfdomain;
    MTC_U32             fencing;
    MTC_BOOLEAN         SR2;
    MTC_BOOLEAN         sm_sig;
    MTC_BOOLEAN         hb_sig;
    MTC_BOOLEAN         sf_sig;
    MTC_BOOLEAN         fh_sleep_extend;
    COM_DATA_HB         stable_hb;
    COM_DATA_SF         stable_sf;
} smvar = {
    .terminate = FALSE,
    .start_time = -1,
    .sm_thread = 0,
    .mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP,
    .cond = PTHREAD_COND_INITIALIZER,
    .phase = SM_PHASE_STARTING,
    .need_fh = FALSE,
    .join_block = FALSE,
    .fencing = FENCING_NONE,
    .SR2 = FALSE,
    .sm_sig = FALSE,
    .hb_sig = FALSE,
    .sf_sig = FALSE,
    .fh_sleep_extend = FALSE,
};


#define mssleep(X) \
{ \
    struct timespec sleep_ts = mstots(X), ts_rem; \
    ts_rem = sleep_ts; \
    while (nanosleep(&sleep_ts, &ts_rem)) sleep_ts = ts_rem; \
}


#define APPROACHING_TIMEOUT_FACTOR          (25)    // [%] of T1 or T2

#define FH_MINIMUM_SLEEP_BEFORE_FO          (10)

#define SYNCHRONIZED_BOOT_TIMEOUT_EXTENDER  (_max(_t1, _t2) * 3)

#define START_FLAGS_INIT                    (1)
#define START_FLAGS_ACTIVE                  (0)
#define START_FLAGS_EXCLUDED                (2)
#define START_FLAGS_NONEXCLUDED             (0)
#define START_FLAGS_EMPTYLIVESET            (4)
#define START_FLAGS_EXISTLIVESET            (0)

#define HB_ACCELERATION_COUNT_JOIN          (3)
#define HB_ACCELERATION_COUNT_RENDEZVOUS    (3)

#define SM_WORKER_INTERVAL  (100)


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_S32
sm_initialize0();

MTC_STATIC MTC_S32
sm_open_objects();

MTC_STATIC void
sm_cleanup_objects();

MTC_STATIC void *
sm_worker(
    void *ignore);

MTC_STATIC void
surviving_by_SR2();

MTC_STATIC MTC_U32
get_pool_state();

MTC_STATIC MTC_BOOLEAN
get_excluded_flag(
    MTC_S32 host);

MTC_STATIC void
check_pool_state();

MTC_STATIC MTC_BOOLEAN
update_sfdomain();

MTC_STATIC MTC_BOOLEAN
wait_fh_event();

MTC_STATIC void *
sm(
    void *ignore);

MTC_STATIC void
rendezvous(
    SM_PHASE    phase1,
    SM_PHASE    phase2,
    SM_PHASE    phase3,
    MTC_BOOLEAN on_heartbeat,
    MTC_BOOLEAN on_statefile);

MTC_STATIC void
fault_handler();

MTC_STATIC MTC_STATUS
wait_all_hosts_recognize_I_was_down();

MTC_STATIC void
start_statefile();

MTC_STATIC void
clear_statefile_starting();

MTC_STATIC void
start_heartbeat();

MTC_STATIC void
set_heartbeat_join();

MTC_STATIC void
fence_request();

MTC_STATIC void
cancel_fence_request();

MTC_STATIC void
wait_all_hosts_up_on_heartbeat(
    MTC_CLOCK   timeout);

MTC_STATIC void
wait_all_hosts_up_on_statefile(
    MTC_CLOCK   timeout);

MTC_STATIC void
start_xapi_monitor();

MTC_STATIC void
arm_fencing();

MTC_STATIC MTC_BOOLEAN
am_I_in_new_liveset(
    MTC_HOSTMAP *pliveset,
    MTC_HOSTMAP *pnewliveset);

MTC_STATIC void
form_new_liveset(
    MTC_HOSTMAP *pliveset);

MTC_STATIC MTC_STATUS
join();

MTC_STATIC void
process_join_request();

MTC_STATIC MTC_STATUS
test_Start_Criteria();

MTC_STATIC MTC_S32
get_partition_size(
    MTC_HOSTMAP sfdomain,
    MTC_HOSTMAP hbdomain,
    MTC_U32     weight[]);

MTC_STATIC MTC_S64
get_partition_score(
    MTC_HOSTMAP hostmap,
    MTC_U32     weight[]);

MTC_STATIC MTC_BOOLEAN
is_all_hosts_up(
    MTC_HOSTMAP hostmap);

MTC_STATIC MTC_BOOLEAN
is_empty_liveset(
    MTC_HOSTMAP liveset);

MTC_STATIC void
print_liveset(
    MTC_S32     pri,
    PMTC_S8     log_string,
    MTC_HOSTMAP hostmap);

MTC_STATIC void
wait_until_HBSF_state_stable();

MTC_STATIC MTC_BOOLEAN
wait_until_all_hosts_have_consistent_view(
    MTC_CLOCK   timeout);

MTC_STATIC MTC_BOOLEAN
test_Survival_Rule(
    PMTC_BOOLEAN pSR2);

MTC_STATIC MTC_BOOLEAN
am_I_in_largest_partition(
    PCOM_DATA_HB phb,
    PCOM_DATA_SF psf,
    MTC_U32      weight[]);

MTC_STATIC MTC_BOOLEAN
wait_until_all_hosts_booted(
    MTC_CLOCK   timeout);

MTC_STATIC MTC_U32
commit_weight();

MTC_STATIC void
sm_send_signals_sm_hb_sf(
    MTC_BOOLEAN sm_sig,
    MTC_BOOLEAN hb_sig,
    MTC_BOOLEAN sf_sig);

MTC_STATIC MTC_BOOLEAN
sm_wait_signals_sm_hb_sf(
    MTC_BOOLEAN sm_sig,
    MTC_BOOLEAN hb_sig,
    MTC_BOOLEAN sf_sig,
    MTC_CLOCK   timeout);


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//


MTC_STATUS
sm_initialize(
    MTC_S32  phase)
{
    static pthread_t    worker_thread = 0;
    MTC_STATUS          ret = MTC_SUCCESS;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "SM: sm_initialize(0).\n");

        ret = sm_initialize0();
        break;

    case 1:
        log_message(MTC_LOG_INFO, "SM: sm_initialize(1).\n");

        // start heartbeat thread
        smvar.terminate = FALSE;
        ret = pthread_create(&smvar.sm_thread, xhad_pthread_attr, sm, NULL);
        if (ret)
        {
            smvar.sm_thread = 0;
            ret = MTC_ERROR_SM_PTHREAD;
            break;
        }

        ret = pthread_create(&worker_thread, xhad_pthread_attr, sm_worker, NULL);
        if (ret)
        {
            worker_thread = 0;
#if 0
            pthread_kill(smvar.sm_thread, SIGKILL);
#endif
            ret = MTC_ERROR_SM_PTHREAD;
            break;
        }

        ret = MTC_SUCCESS;

        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "SM: sm_initialize(-1).\n");

        {
            PCOM_DATA_SM        psm;
            PCOM_DATA_HB        phb;
            PCOM_DATA_SF        psf;
            PCOM_DATA_XAPIMON   pxapimon;

            com_writer_lock(sm_object, (void **) &psm);
            smvar.fencing = psm->fencing = FENCING_DISARMED;
            com_writer_unlock(sm_object);

            com_writer_lock(hb_object, (void **) &phb);
            phb->ctl.enable_HB_send = FALSE;
            phb->ctl.enable_HB_receive = FALSE;
            com_writer_unlock(hb_object);

            com_writer_lock(sf_object, (void **) &psf);
            psf->ctl.enable_SF_read = FALSE;
            psf->ctl.enable_SF_write = FALSE;
            com_writer_unlock(sf_object);

            com_writer_lock(xapimon_object, (void **) &pxapimon);
            pxapimon->ctl.enable_Xapi_monitor = FALSE;
            com_writer_unlock(xapimon_object);
        }

        smvar.terminate = TRUE;

#if 0
        // wait for thread termination
        if ((ret = pthread_join(worker_thread, NULL)))
        {
            if (worker_thread)
            {
                pthread_kill(worker_thread, SIGKILL);
                worker_thread = 0;
            }
        }
        if ((ret = pthread_join(smvar.sm_thread, NULL)))
        {
            if (smvar.sm_thread)
            {
                pthread_kill(smvar.sm_thread, SIGKILL);
                smvar.sm_thread = 0;
            }
        }
#endif

        ret = MTC_SUCCESS;

        break;
    }

    return ret;
}


MTC_STATUS
sm_initialize0()
{
    COM_DATA_SM sm;
    MTC_S32     ret = MTC_SUCCESS;

    // initialize COM_DATA_SM
    MTC_HOSTMAP_INIT_RESET(sm.current_liveset);
    MTC_HOSTMAP_INIT_RESET(sm.proposed_liveset);
    MTC_HOSTMAP_INIT_RESET(sm.sf_access);
    MTC_HOSTMAP_INIT_RESET(sm.sf_corrupted);

    sm.phase = SM_PHASE_STARTING;
    sm.status = MTC_SUCCESS;
    sm.fencing = FENCING_NONE;
    sm.SR2 = FALSE;

    sm.hb_approaching_timeout = FALSE;
    sm.sf_approaching_timeout = FALSE;
    sm.xapi_approaching_timeout = FALSE;

    sm.weight = sm.commited_weight = 0;

    // create common object
    ret = com_create(COM_ID_SM, &sm_object, sizeof(COM_DATA_SM), &sm);
    if (ret != MTC_SUCCESS)
    {
        log_internal(MTC_LOG_ERR, "SM: cannot create COM object. (%d)\n", ret);
        sm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
        goto error;
    }

    MTC_HOSTMAP_INIT_RESET(smvar.last_hbdomain);
    MTC_HOSTMAP_INIT_RESET(smvar.last_sfdomain);

    // open refereced objects
    ret = sm_open_objects();
    if (ret != MTC_SUCCESS)
    {
        log_internal(MTC_LOG_ERR, "SM: cannot open COM objects. (%d)\n", ret);
        goto error;
    }

    return MTC_SUCCESS;

error:
    sm_cleanup_objects();

    return ret;
}


void
self_fence(
    MTC_STATUS  code,
    PMTC_S8     message)
{
    if (smvar.fencing == FENCING_ARMED)
    {
        log_status(code, message);
        watchdog_selffence();
        exit(status_to_exit(code));     // if possible
    }
}


MTC_STATIC MTC_STATUS
sm_open_objects()
{
    MTC_S32     index;
    MTC_STATUS  ret;

    for (index = 0; objects[index].handle != NULL; index++)
    {
        // open common objects
        if (*(objects[index].handle) == HA_COMMON_OBJECT_INVALID_HANDLE_VALUE)
        {
            ret = com_open(objects[index].name, objects[index].handle);
            if (ret != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR,
                            "SM: cannot open COM object (name = %s). (%d)\n",
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
            if (ret != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR,
                            "SM: cannot register callback to %s. (%d)\n",
                            objects[index].name, ret);
                return ret;
            }
        }
    }

    return MTC_SUCCESS;
}


MTC_STATIC void
sm_cleanup_objects()
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


MTC_STATIC void
sm_hb_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    if (smvar.sm_thread != pthread_self())
    {
        sm_send_signals_sm_hb_sf(FALSE, TRUE, FALSE);
    }
}

MTC_STATIC void
sm_sf_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    if (smvar.sm_thread != pthread_self())
    {
        sm_send_signals_sm_hb_sf(FALSE, FALSE, TRUE);
    }
}

MTC_STATIC void
sm_xapimon_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    ;
}

MTC_STATIC void
sm_sm_updated(
    HA_COMMON_OBJECT_HANDLE handle,
    void *buffer)
{
    PCOM_DATA_SM    psm = buffer;
    
    if (psm->fencing == FENCING_DISARM_REQUESTED)
    {
        smvar.fencing = psm->fencing = FENCING_DISARMED;
    }

    if (smvar.sm_thread != pthread_self())
    {
        sm_send_signals_sm_hb_sf(TRUE, FALSE, FALSE);
    }
    
    smvar.SR2 = psm->SR2;
}


MTC_STATIC void
sm_abort(
    MTC_STATUS  code,
    PMTC_S8     msg)
{
    PCOM_DATA_SM        psm;
    PCOM_DATA_HB        phb;
    PCOM_DATA_SF        psf;
    PCOM_DATA_XAPIMON   pxapimon;

    log_status(code, msg);

    com_writer_lock(sm_object, (void **) &psm);
    psm->status = code;
    smvar.phase = psm->phase = SM_PHASE_ABORTED;
    com_writer_unlock(sm_object);

    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.enable_HB_send = FALSE;
    phb->ctl.enable_HB_receive = FALSE;
    com_writer_unlock(hb_object);

    com_writer_lock(sf_object, (void **) &psf);
    psf->ctl.enable_SF_read = FALSE;
    psf->ctl.enable_SF_write = FALSE;
    com_writer_unlock(sf_object);

    com_writer_lock(xapimon_object, (void **) &pxapimon);
    pxapimon->ctl.enable_Xapi_monitor = FALSE;
    com_writer_unlock(xapimon_object);

    // stop daemon
    main_terminate(code);
}


MTC_STATIC void
set_sm_phase(
    SM_PHASE phase)
{
    PCOM_DATA_SM    psm;

    com_writer_lock(sm_object, (void **) &psm);
    smvar.phase = psm->phase = phase;
    com_writer_unlock(sm_object);
}


MTC_STATIC void *
sm(
    void *ignore)
{
    MTC_STATUS  status;
    MTC_U32     weight;

    log_message(MTC_LOG_INFO, "SM: thread ID: %ld.\n", syscall(SYS_gettid));
    // commit initial weight
    weight = commit_weight();
    log_message(MTC_LOG_INFO, "Initial weight = %d.\n", weight);

    // Start State File read and Heartbeat send/receive
    start_statefile();
    log_message(MTC_LOG_INFO, "State File read/write enabled.\n");
    start_heartbeat();
    log_message(MTC_LOG_INFO, "Heartbeat send/receive enabled.\n");
    smvar.start_time = _getms();

    // Wait for all hosts are visible
    wait_all_hosts_up_on_heartbeat(_T1 * ONE_SEC);
    log_message(MTC_LOG_DEBUG, "SM: heartbeat is ready\n");
    wait_all_hosts_up_on_statefile(_T2 * ONE_SEC);
    log_message(MTC_LOG_DEBUG, "SM: statefile is ready\n");
    status = wait_all_hosts_recognize_I_was_down();
    if (status != MTC_SUCCESS)
    {
        sm_abort(status,
                 "Cannot start HA daemon at this time, because this host is still online from other hosts' perspectives.  - Abort");
        return NULL;
    }
    log_message(MTC_LOG_DEBUG, "SM: other hosts are ready\n");

    // Now ready to join or forming a liveset
    set_heartbeat_join();

    // Start Criteria test
    status = test_Start_Criteria();
    if (status != MTC_SUCCESS)
    {
        sm_abort(status, "Cannot start HA daemon, because Start Criteria is not met for the local host.  - Abort");
        return NULL;
    }
    log_message(MTC_LOG_INFO, "Start Criteria has been met for the local host.\n");

    // Clear excluded flag
    if (get_excluded_flag(_my_index))
    {
        status = sf_set_excluded(FALSE);
        if (status != MTC_SUCCESS)
        {
            sm_abort(status, "Cannot clear excluded flag.  - Abort");
            return NULL;
        }
        log_message(MTC_LOG_NOTICE, "Excluded flag is cleared successfully.\n");
    }

    // Join to the existing liveset
    status = join();
    if (status != MTC_SUCCESS)
    {
        sm_abort(status, "Cannot start HA daemon.  - Abort");
        return NULL;
    }
    // accelerate to send heartbeat to notify I am in
    hb_send_hb_now(HB_ACCELERATION_COUNT_JOIN);
    log_message(MTC_LOG_INFO, "Joined.\n");

    // Clear starting flag of State File object
    clear_statefile_starting();

    // Start Xapi Monitor
    start_xapi_monitor();
    log_message(MTC_LOG_INFO, "Xapi monitor started.\n");

    // Arm fencing
    arm_fencing();
    log_message(MTC_LOG_INFO, "Fencing is armed.\n");

    set_sm_phase(SM_PHASE_STARTED);

    log_message(MTC_LOG_NOTICE, "The local host has transitioned to online state.\n");

    // Notify main that sm is in steady state
    main_steady_state();

    // Enter stable operation
    while (!smvar.terminate)
    {
        if (wait_fh_event())
        {
            fault_handler();
        }
    }

    log_message(MTC_LOG_INFO, "State Manager terminated.\n");

    return NULL;
}


MTC_STATIC void
rendezvous(
    SM_PHASE    phase1,
    SM_PHASE    phase2,
    SM_PHASE    phase3,
    MTC_BOOLEAN on_heartbeat,
    MTC_BOOLEAN on_statefile)
{
#if RENDEZVOUS_FAULT_HANDLING
    void rendezvous_wait(SM_PHASE p1, SM_PHASE p2)
    {
        PCOM_DATA_SM    psm;
        PCOM_DATA_HB    phb;
        PCOM_DATA_SF    psf;
        MTC_BOOLEAN     rendezvous;
        MTC_S32         index;
        MTC_CLOCK       log_time = _getms();

        if (on_heartbeat)
        {
            hb_send_hb_now(HB_ACCELERATION_COUNT_RENDEZVOUS);
        }
        if (on_statefile)
        {
            hb_SF_accelerate();
        }

        rendezvous = FALSE;
        while (!rendezvous)
        {
            rendezvous = TRUE;
            com_reader_lock(sm_object, (void **) &psm);
            com_reader_lock(hb_object, (void **) &phb);
            com_reader_lock(sf_object, (void **) &psf);
            for (index = 0; _is_configured_host(index); index++)
            {
                if (MTC_HOSTMAP_ISON(psm->current_liveset, index))
                {
                    if (on_heartbeat)
                    {
                        if (MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                            !(phb->sm_phase[index] == p1 || phb->sm_phase[index] == p2))
                        {
                            rendezvous = FALSE;
                        }
                    }
                    if (on_statefile)
                    {
                        if (MTC_HOSTMAP_ISON(psf->sfdomain, index) &&
                            !(psf->sm_phase[index] == p1 || psf->sm_phase[index] == p2))
                        {
                            rendezvous = FALSE;
                        }
                    }
                }
            }
            if (_getms() - log_time > ONE_MINUTE && !rendezvous)
            {
                log_time = _getms();
                print_liveset(MTC_LOG_DEBUG,
                    "SM: rendezvousing current_liveset = %s\n", psm->current_liveset);
                print_liveset(MTC_LOG_DEBUG,
                    "SM: rendezvousing hbdomain = %s\n", phb->hbdomain);
                print_liveset(MTC_LOG_DEBUG,
                    "SM: rendezvousing sfdomain = %s\n", psf->sfdomain);
                for (index = 0; _is_configured_host(index); index++)
                {
                    log_message(MTC_LOG_DEBUG,
                        "SM: rendezvousing phase on heartbeat[%d] = %d\n", index,
                        (index == _my_index)? psm->phase: phb->sm_phase[index]);
                    log_message(MTC_LOG_DEBUG,
                        "SM: rendezvousing phase on statefile[%d] = %d\n", index,
                        (index == _my_index)? psm->phase: psf->sm_phase[index]);
                }
#if 0
                assert(FALSE);
#endif
            }
            com_reader_unlock(sf_object);
            com_reader_unlock(hb_object);
            com_reader_unlock(sm_object);

            sm_wait_signals_sm_hb_sf(TRUE, TRUE, TRUE, -1);
        }

        if (on_statefile)
        {
            hb_SF_cancel_accelerate();
        }
    }

    set_sm_phase(phase1);
    rendezvous_wait(phase1, phase2);

    set_sm_phase(phase2);
    rendezvous_wait(phase2, phase3);
#else
    set_sm_phase(phase2);
#endif
}


MTC_STATIC void
fault_handler()
{
    PCOM_DATA_SM    psm;
    MTC_U32         weight;
    MTC_CLOCK       p3_start_time, p4_start_time, sleep_time;
    MTC_BOOLEAN     degrading;


    log_message(MTC_LOG_DEBUG, "FH: Start fault handler.\n");

    smvar.join_block = TRUE;


    // phase 1: Wait until HB/SF state becomes stable, and commit weight value
    rendezvous(SM_PHASE_FHREADY, SM_PHASE_FH1, SM_PHASE_FH1DONE, TRUE, FALSE);

    wait_until_HBSF_state_stable();
    log_maskable_debug_message(FH_TRACE, "FH: HB/SF state has become stable.\n");

    weight = commit_weight();
    log_maskable_debug_message(FH_TRACE, "FH: weight value [%d] is commited.\n", weight);

    // phase 2: Wait until all hosts have consistent view
    rendezvous(SM_PHASE_FH1DONE, SM_PHASE_FH2, SM_PHASE_FH2DONE, TRUE, TRUE);
    log_maskable_debug_message(FH_TRACE, "FH: waiting for consistent view...\n");
    if (!wait_until_all_hosts_have_consistent_view(_max(_T1, _T2) * ONE_SEC))
    {
        self_fence(MTC_ERROR_SM_FAULTHANDLER_TIMEOUT,
                   "FH: cannot get consistent view within timeout.  - Self-Fence");
    }
    log_maskable_debug_message(FH_TRACE,
                 "FH: All hosts now have consistent view to the pool membership.\n");

    // phase 3: Determine one winner
    //          In this test, if I am winner, the proposed liveset is updated.
    rendezvous(SM_PHASE_FH2DONE, SM_PHASE_FH3, SM_PHASE_FH3DONE, TRUE, FALSE);
    p3_start_time = _getms();

    if (!test_Survival_Rule(&smvar.SR2))
    {
        self_fence(MTC_ERROR_SM_SURVIVALRULE_FAILED,
                   "FH: Survival Rule is not met for the local host.  - Self-Fence");
    }

    if (fist_on("sm.fence_in_FH3"))
    {
        self_fence(MTC_ERROR_SM_SURVIVALRULE_FAILED,
                   "FH: Self-Fence by FIST sm.fence_in_FH3");
    }
    if (fist_on("sm.sleep_in_FH3"))
    {
        log_message(MTC_LOG_DEBUG, "FH: sleep %d by FIST sm.sleep_in_FH3", _max(_Wh, _Ws) * 2);
        mssleep(_max(_Wh, _Ws) * 2 * ONE_SEC);
    }



    // phase 4: Perform fencing and updating liveset
    // Wait for fencing timeout
    rendezvous(SM_PHASE_FH3DONE, SM_PHASE_FH4, SM_PHASE_FH4DONE, TRUE, FALSE);

    com_reader_lock(sm_object, (void **) &psm);
    degrading = !MTC_HOSTMAP_SUBSETEQUAL(psm->current_liveset, '(=', psm->proposed_liveset);
    com_reader_unlock(sm_object);

    if (degrading)
    {
        if (smvar.fencing == FENCING_ARMED)
        {
            fence_request();
        }

        p4_start_time = _getms();
        sleep_time = _max(((_max(_Wh, _Ws) - _min(_T1, _T2) + _max(_t1, _t2)) * ONE_SEC)
                            - (p4_start_time - p3_start_time)
                            + (smvar.fh_sleep_extend? _min(_T1, _T2): 0) * ONE_SEC,
                          FH_MINIMUM_SLEEP_BEFORE_FO * ONE_SEC);
        mssleep(sleep_time);

        // Update liveset

        if (smvar.fencing == FENCING_ARMED)
        {
            com_writer_lock(sm_object, (void **) &psm);
            MTC_HOSTMAP_INTERSECTION(psm->current_liveset, '=',
                                        psm->current_liveset, '&', psm->proposed_liveset);
            print_liveset(MTC_LOG_NOTICE,
                "Liveset has been updated."
                "  new liveset = (%s)\n", psm->current_liveset);
            com_writer_unlock(sm_object);

            cancel_fence_request();
        }
    }

    rendezvous(SM_PHASE_FH4DONE, SM_PHASE_STARTED, SM_PHASE_FHREADY, TRUE, FALSE);
    smvar.join_block = FALSE;
    log_message(MTC_LOG_DEBUG, "FH: End fault handler.\n");
}


MTC_STATIC MTC_BOOLEAN
test_Survival_Rule(
    PMTC_BOOLEAN pSR2)
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb = &smvar.stable_hb;
    PCOM_DATA_SF    psf = &smvar.stable_sf;
    MTC_HOSTMAP     hbd, sfd;
    MTC_BOOLEAN     winner = FALSE,
                    sf_access, excluded, in_hbd, in_sfd;
    MTC_S32         index;

    com_writer_lock(sm_object, (void **) &psm);

    MTC_HOSTMAP_MASK_UNCONFIG(phb->hbdomain);
    MTC_HOSTMAP_MASK_UNCONFIG(psf->sfdomain);
    MTC_HOSTMAP_COPY(smvar.last_hbdomain, phb->hbdomain);
    MTC_HOSTMAP_COPY(smvar.last_sfdomain, psf->sfdomain);
    MTC_HOSTMAP_INTERSECTION(hbd, '=', phb->hbdomain, '&', psm->current_liveset);
    MTC_HOSTMAP_INTERSECTION(sfd, '=', psf->sfdomain, '&', psm->current_liveset);

    if (MTC_HOSTMAP_SUBSETEQUAL(sfd, '(=', hbd) && !is_empty_liveset(sfd))
    {
        winner = TRUE;
    }
    else
    {
        winner = am_I_in_largest_partition(phb, psf, psf->weight);
        if (winner)
        {
            log_message(MTC_LOG_DEBUG, "SM: I have won by the result of weight caluculation.\n");
        }
    }

    if (winner)
    {
        log_maskable_debug_message(FH_TRACE, "FH: I have won.\n");
    }
    else
    {
        *pSR2 = TRUE;

        // Check if Survival Rule-2
        for (index = 0; _is_configured_host(index); index++)
        {
            sf_access = (index == _my_index)? psf->SF_access: MTC_HOSTMAP_ISON(phb->raw[index].sfdomain, index);
            excluded = MTC_HOSTMAP_ISON(psf->excluded, index);
            in_hbd = MTC_HOSTMAP_ISON(hbd, index);

            if ((in_hbd && sf_access) || (!in_hbd && !excluded))
            {
                *pSR2 = FALSE;
            }
        }

        if (*pSR2)
        {
            psm->SR2 = TRUE;
            log_maskable_debug_message(FH_TRACE, "FH: I am surviving according to Survival Rule-2.\n");
            winner = TRUE;
        }
        else
        {
            log_maskable_debug_message(FH_TRACE, "FH: I have lost.\n");
        }
    }

    smvar.fh_sleep_extend = FALSE;

    if (winner && smvar.fencing == FENCING_ARMED)
    {
        com_writer_lock(hb_object, (void **) &phb);
        com_writer_lock(sf_object, (void **) &psf);

        for (index = 0; _is_configured_host(index); index++)
        {
            in_hbd = MTC_HOSTMAP_ISON(hbd, index);
            in_sfd = (*pSR2)? TRUE: MTC_HOSTMAP_ISON(sfd, index);

            if (!in_hbd || !in_sfd)
            {
                log_message(MTC_LOG_DEBUG, "SM: Node (%d) will be removed from liveset.\n", index);
                MTC_HOSTMAP_RESET(psm->proposed_liveset, index);

                if (MTC_HOSTMAP_ISON(phb->hbdomain, index))
                {
                    MTC_HOSTMAP_RESET(phb->hbdomain, index);
                    print_liveset(MTC_LOG_DEBUG,
                        "SM: HB domain is updated = (%s)\n", phb->hbdomain);
                    smvar.fh_sleep_extend = TRUE;
                }
                if (MTC_HOSTMAP_ISON(psf->sfdomain, index))
                {
                    MTC_HOSTMAP_RESET(psf->sfdomain, index);
                    print_liveset(MTC_LOG_DEBUG,
                        "SM: SF domain is updated = (%s)\n", psf->sfdomain);
                    smvar.fh_sleep_extend = TRUE;
                }
            }
        }

        com_writer_unlock(sf_object);
        com_writer_unlock(hb_object);
    }

    com_writer_unlock(sm_object);

    return winner;
}


// TBD - It is better if this function can be divided to
// the Start Criteria test part and the forming new liveset part.
MTC_STATIC MTC_STATUS
test_Start_Criteria()
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    COM_DATA_SM     sm;
    COM_DATA_HB     hb;
    COM_DATA_SF     sf;
    MTC_U32         pool_state = get_pool_state(),
                    my_excluded_flag = get_excluded_flag(_my_index),
                    start_flags,
                    last_flags = -1;
    MTC_HOSTMAP     sfdomain_nonstarting, hbdomain_or_excluded, liveset;
    MTC_BOOLEAN     all_excluded, win;
    MTC_CLOCK       timeout;


    // Check pool state
    switch (pool_state)
    {
    case SF_STATE_INIT:
    case SF_STATE_ACTIVE:
        break;

    case SF_STATE_NONE:
        log_internal(MTC_LOG_ERR,
                    "Start Criteria: This host has no state file access (%d).\n",
                    pool_state);
        return MTC_ERROR_SM_NOSTATEFILE;
        break;

    case SF_STATE_INVALID:
    default:
        log_internal(MTC_LOG_ERR,
                    "Start Criteria: invalid pool state (%d).\n", pool_state);
        return MTC_ERROR_SM_INVALID_POOL_STATE;
        break;
    }


    // Test Start Criteria with boot/enable timeout
    do
    {
        com_reader_lock(sm_object, (void **) &psm);
        com_reader_lock(hb_object, (void **) &phb);
        com_reader_lock(sf_object, (void **) &psf);
        sm = *psm;
        hb = *phb;
        sf = *psf;
        com_reader_unlock(sf_object);
        com_reader_unlock(hb_object);
        com_reader_unlock(sm_object);


        // Still have State File access?
        if (!sf.SF_access)
        {
            log_internal(MTC_LOG_ERR,
                        "Start Criteria: lost the state file access while starting.\n");
            return MTC_ERROR_SM_NOSTATEFILE;
        }

        // pool_state, excluded, sfdomain-booting
        start_flags = 0;
        start_flags |= (pool_state == SF_STATE_INIT)?
                            START_FLAGS_INIT: START_FLAGS_ACTIVE;
        start_flags |= (my_excluded_flag)?
                            START_FLAGS_EXCLUDED: START_FLAGS_NONEXCLUDED;
        MTC_HOSTMAP_DIFFERENCE(sfdomain_nonstarting, '=', sf.sfdomain, '-', sf.starting);
        MTC_HOSTMAP_MASK_UNCONFIG(sfdomain_nonstarting);
        // if pool is surviving by SR2 and this host can comunicate with all non-excluded hosts
        // by heartbeat, let's think as there is a liveset.
        MTC_HOSTMAP_UNION(hbdomain_or_excluded, '=', hb.hbdomain, '|', sf.excluded);
        if (is_empty_liveset(sfdomain_nonstarting) &&
            !(sm.SR2 && is_all_hosts_up(hbdomain_or_excluded)))
        {
            start_flags |= START_FLAGS_EMPTYLIVESET;
        }
        else
        {
            start_flags |= START_FLAGS_EXISTLIVESET;
        }

        if (last_flags != start_flags)
        {
            log_message(MTC_LOG_INFO,
                        "Start Criteria: pool state=%s; excluded=%s; liveset=%s\n",
                        (start_flags & START_FLAGS_INIT)? "INIT": "ACTIVE",
                        (start_flags & START_FLAGS_EXCLUDED)? "TRUE": "FALSE",
                        (start_flags & START_FLAGS_EMPTYLIVESET)? "EMPTY": "EXIST");
            last_flags = start_flags;
        }

        switch (start_flags)
        {
        case START_FLAGS_INIT   | START_FLAGS_EXCLUDED    | START_FLAGS_EXISTLIVESET:
        case START_FLAGS_INIT   | START_FLAGS_NONEXCLUDED | START_FLAGS_EXISTLIVESET:
            log_internal(MTC_LOG_ERR,
                        "Start Criteria: Invalid pool state. (0x%x)\n", start_flags);
            return MTC_ERROR_SM_INVALID_POOL_STATE;
            break;

        case START_FLAGS_INIT   | START_FLAGS_EXCLUDED    | START_FLAGS_EMPTYLIVESET:
        case START_FLAGS_INIT   | START_FLAGS_NONEXCLUDED | START_FLAGS_EMPTYLIVESET:
        case START_FLAGS_ACTIVE | START_FLAGS_EXCLUDED    | START_FLAGS_EMPTYLIVESET:
        case START_FLAGS_ACTIVE | START_FLAGS_NONEXCLUDED | START_FLAGS_EMPTYLIVESET:
            if (is_all_hosts_up(hb.hbdomain) && is_all_hosts_up(sf.sfdomain))
            {
                log_message(MTC_LOG_INFO,
                    "Start Criteria: Forming a new liveset with all configured hosts.\n");
                com_writer_lock(sm_object, (void **) &psm);
                MTC_HOSTMAP_COPY(psm->proposed_liveset, hb.hbdomain);
                print_liveset(MTC_LOG_INFO,
                    "Start Criteria: current_liveset = (%s)\n", psm->current_liveset);
                print_liveset(MTC_LOG_INFO,
                    "Start Criteria: proposed_liveset = (%s)\n", psm->proposed_liveset);
                com_writer_unlock(sm_object);

                // Wait until all other hosts boot up
                if (wait_until_all_hosts_booted(
                    (((pool_state == SF_STATE_INIT)? _Tenable: _Tboot) +
                     SYNCHRONIZED_BOOT_TIMEOUT_EXTENDER) * ONE_SEC))
                {
                    log_message(MTC_LOG_INFO,
                        "Start Criteria: all hosts are ready to start.\n");
                    com_reader_lock(sm_object, (void **) &psm);
                    MTC_HOSTMAP_COPY(liveset, psm->proposed_liveset);
                    com_reader_unlock(sm_object);

                    form_new_liveset(&psm->proposed_liveset);

                    // change pool state to ACTIVE
                    if (pool_state == SF_STATE_INIT)
                    {
                        log_message(MTC_LOG_NOTICE,
                            "Start Criteria: Changing the pool state to ACTIVE.\n");
                        sf_set_pool_state(SF_STATE_ACTIVE);
                    }

                    return MTC_SUCCESS;
                }
            }
            break;

        case START_FLAGS_ACTIVE | START_FLAGS_EXCLUDED    | START_FLAGS_EXISTLIVESET:
        case START_FLAGS_ACTIVE | START_FLAGS_NONEXCLUDED | START_FLAGS_EXISTLIVESET:
            // Somebody may form a new liveset, see if I am in.
            if (am_I_in_new_liveset(&sfdomain_nonstarting, &liveset))
            {
                log_message(MTC_LOG_INFO,
                    "Start Criteria: Forming a new liveset with a subset of configured hosts.\n");
                form_new_liveset(&liveset);
                return MTC_SUCCESS;
            }

            MTC_HOSTMAP_MASK_UNCONFIG(hb.hbdomain);
            MTC_HOSTMAP_MASK_UNCONFIG(sf.sfdomain);
            if (MTC_HOSTMAP_SUBSETEQUAL(sf.sfdomain, '(=', hb.hbdomain))
            {
                log_message(MTC_LOG_INFO,
                    "Start Criteria: Joining the existing liveset.\n");
                com_writer_lock(sm_object, (void **) &psm);
                MTC_HOSTMAP_DIFFERENCE(psm->proposed_liveset, '=',
                                        sf.sfdomain, '-', sf.starting);
                MTC_HOSTMAP_MASK_UNCONFIG(psm->proposed_liveset);
                MTC_HOSTMAP_COPY(psm->current_liveset, psm->proposed_liveset);
                print_liveset(MTC_LOG_INFO,
                    "Start Criteria: current_liveset = (%s)\n", psm->current_liveset);
                print_liveset(MTC_LOG_INFO,
                    "Start Criteria: proposed_liveset = (%s)\n", psm->proposed_liveset);
                com_writer_unlock(sm_object);

                return MTC_SUCCESS;
            }
            break;

        default:
            assert(FALSE);
            break;
        }

        timeout = ((pool_state == SF_STATE_INIT)? _Tenable: _Tboot) * ONE_SEC
                - (_getms() - smvar.start_time);
        timeout = (timeout < 0)? 0: timeout;
    } while (sm_wait_signals_sm_hb_sf(TRUE, TRUE, TRUE, timeout));


    // Time out
    switch (start_flags)
    {
    case START_FLAGS_INIT   | START_FLAGS_EXCLUDED    | START_FLAGS_EXISTLIVESET:
    case START_FLAGS_INIT   | START_FLAGS_NONEXCLUDED | START_FLAGS_EXISTLIVESET:
        assert(FALSE);
        break;

    case START_FLAGS_INIT   | START_FLAGS_EXCLUDED    | START_FLAGS_EMPTYLIVESET:
    case START_FLAGS_INIT   | START_FLAGS_NONEXCLUDED | START_FLAGS_EMPTYLIVESET:
        // If the pool state is INIT and there is no liveset, then the pool
        // cannot start with a subset of configured hosts.
        log_internal(MTC_LOG_NOTICE,
            "Start Criteria: Start timeout (0x%x)\n", start_flags);
        return MTC_ERROR_SM_BOOT_TIMEOUT;
        break;

    case START_FLAGS_ACTIVE | START_FLAGS_EXCLUDED    | START_FLAGS_EXISTLIVESET:
    case START_FLAGS_ACTIVE | START_FLAGS_NONEXCLUDED | START_FLAGS_EXISTLIVESET:
        // Somebody may form a new liveset, see if I am in.
        if (am_I_in_new_liveset(&sfdomain_nonstarting, &liveset))
        {
            log_message(MTC_LOG_INFO,
                "Start Criteria: Forming a new liveset with a subset of configured hosts.\n");
            form_new_liveset(&liveset);
            return MTC_SUCCESS;
        }
        // No, then...
        // If the pool state is ACTIVE and there is an existing liveset,
        // then this host must join the liveset.  Comming down here means
        // this host could not join it in timeout period because of partition.
        // Transitional views of partition should be solved in timeout period.
        // We may be treating a few case of transitional partition as parmanent
        // erroneous partition.
        log_internal(MTC_LOG_NOTICE,
            "Start Criteria: Cannot join an existing liveset (0x%x)\n", start_flags);
        return MTC_ERROR_SM_JOIN_FAILED;
        break;

    case START_FLAGS_ACTIVE | START_FLAGS_EXCLUDED    | START_FLAGS_EMPTYLIVESET:
        com_reader_lock(sf_object, (void **) &psf);
        all_excluded = is_all_hosts_up(sf.excluded);
        com_reader_unlock(sf_object);
        if (!all_excluded)
        {
            log_internal(MTC_LOG_ERR,
                "Start Criteria: Start Rule is not met, because I am excluded, "
                "and there are one or more non-excluded hosts.\n");
            return MTC_ERROR_SM_BOOT_BLOCKED_BY_EXCLUDED;
        }
        // (don't break) fall down to the next case
    case START_FLAGS_ACTIVE | START_FLAGS_NONEXCLUDED | START_FLAGS_EMPTYLIVESET:
        com_reader_lock(hb_object, (void **) &phb);
        com_reader_lock(sf_object, (void **) &psf);
        win = am_I_in_largest_partition(phb, psf, psf->weight);
        com_reader_unlock(sf_object);
        com_reader_unlock(hb_object);

        if (win)
        {
            log_message(MTC_LOG_INFO,
                "Start Criteria: Forming a new liveset with a subset of configured hosts.\n");

            MTC_HOSTMAP_DIFFERENCE(liveset, '=', hb.hbdomain, '-', hb.notjoining);
            form_new_liveset(&liveset);

            return MTC_SUCCESS;
        }
        else
        {
            log_internal(MTC_LOG_NOTICE,
                "Start Criteria: Start Rule is not met, I am not in the largest partition.\n");
            return MTC_ERROR_SM_BOOT_FAILED;
        }
        break;

    default:
        assert(FALSE);
        break;
    }

    assert(FALSE);
    return MTC_ERROR_SM_BOOT_FAILED;
}


MTC_STATIC MTC_BOOLEAN
am_I_in_new_liveset(
    MTC_HOSTMAP *pliveset,
    MTC_HOSTMAP *pnewliveset)
{
    PCOM_DATA_SF    psf;
    MTC_S32         index;

    for (index = 0; _is_configured_host(index); index++)
    {
        if (MTC_HOSTMAP_ISON(*pliveset, index))
        {
            com_reader_lock(sf_object, (void **) &psf);
            MTC_HOSTMAP_COPY(*pnewliveset, psf->raw[index].current_liveset);
            com_reader_unlock(sf_object);

            if (MTC_HOSTMAP_ISON(*pnewliveset, _my_index))
            {
                return TRUE;
            }
            break;
        }
    }

    return FALSE;
}


MTC_STATIC void
form_new_liveset(
    MTC_HOSTMAP *pliveset)
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;

    com_writer_lock(sm_object, (void **) &psm);
    MTC_HOSTMAP_COPY(psm->proposed_liveset, *pliveset);
    MTC_HOSTMAP_COPY(psm->current_liveset, psm->proposed_liveset);
    print_liveset(MTC_LOG_INFO,
        "Start Criteria: current_liveset = (%s)\n", psm->current_liveset);
    print_liveset(MTC_LOG_INFO,
        "Start Criteria: proposed_liveset = (%s)\n", psm->proposed_liveset);
    com_writer_unlock(sm_object);

    // cancel join request
    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.join = FALSE;
    com_writer_unlock(hb_object);
}


MTC_STATIC MTC_STATUS
join()
{
    PCOM_DATA_HB    phb;
    PCOM_DATA_SM    psm;
    MTC_BOOLEAN     join, joined;
    MTC_CLOCK       join_start_time, timeout;
    MTC_S32         index;

    // Check if Join is requested
    com_reader_lock(hb_object, (void **) &phb);
    join = phb->ctl.join;
    com_reader_unlock(hb_object);
    if (!join)
    {
        return MTC_SUCCESS;
    }

    // Send join request by heartbeat
    com_writer_lock(sm_object, (void **) &psm);
    MTC_HOSTMAP_SET(psm->proposed_liveset, _my_index);
    print_liveset(MTC_LOG_DEBUG,
        "Join: trying to join [proposed_liveset = (%s)].\n", psm->proposed_liveset);
    com_writer_unlock(sm_object);

    // accelerate to send heartbeat to notify my join request
    hb_send_hb_now(HB_ACCELERATION_COUNT_JOIN);

    join_start_time = _getms();

    // Wait until join allowed
    do
    {
        MTC_S32         index;

        // Check if all hosts allow to join
        com_reader_lock(sm_object, (void **) &psm);
        com_reader_lock(hb_object, (void **) &phb);
        joined = TRUE;
        for (index = 0; _is_configured_host(index); index++)
        {
            if (index != _my_index &&
                MTC_HOSTMAP_ISON(psm->current_liveset, index) &&
                !MTC_HOSTMAP_ISON(phb->raw[index].proposed_liveset, _my_index))
            {
                joined = FALSE;
                break;
            }
        }
        com_reader_unlock(hb_object);
        com_reader_unlock(sm_object);

        if (joined)
        {
            break;
        }

        timeout = _Tboot * ONE_SEC - (_getms() - join_start_time);
        timeout = (timeout < 0)? 0: timeout;
    } while (sm_wait_signals_sm_hb_sf(TRUE, TRUE, FALSE, timeout));

    // If joined, update current_liveset.  If not, cancel join request
    com_writer_lock(sm_object, (void **) &psm);
    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.join = FALSE;
    if (joined)
    {
        if ((smvar.SR2 = psm->SR2))
        {
            log_message(MTC_LOG_DEBUG, "Join: joining with SR2 enabled\n");
        }
        MTC_HOSTMAP_SET(psm->current_liveset, _my_index);
        print_liveset(MTC_LOG_INFO,
            "Join: joined successfully [new liveset = (%s)].\n", psm->current_liveset);
    }
    else
    {
        print_liveset(MTC_LOG_WARNING,
            "Join: failed to join [my proposed liveset = (%s)]\n", psm->proposed_liveset);
        for (index = 0; _is_configured_host(index); index++)
        {
            print_liveset(MTC_LOG_WARNING,
                "Join: \tother hosts [proposed liveset = (%s)]\n", phb->raw[index].proposed_liveset);
        }
        MTC_HOSTMAP_RESET(psm->proposed_liveset, _my_index);
    }
    com_writer_unlock(hb_object);
    com_writer_unlock(sm_object);

    return (joined)? MTC_SUCCESS: MTC_ERROR_SM_JOIN_FAILED;
}


MTC_STATIC void
process_join_request()
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    MTC_S32         index, index2;
    MTC_HOSTMAP     joining_set;

    MTC_HOSTMAP_INIT_RESET(joining_set);

    com_writer_lock(sm_object, (void **) &psm);
    com_reader_lock(hb_object, (void **) &phb);
    com_reader_lock(sf_object, (void **) &psf);

#if 0
    if (psm->phase != SM_PHASE_STARTING)
    {
#else
    // If liveset is empty, join is handled in Start Criteria.
    if (!is_empty_liveset(psm->current_liveset))
    {
#endif
        for (index = 0; _is_configured_host(index); index++)
        {
            // If Node i is requesting to join,
            if (index != _my_index &&
                MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                !MTC_HOSTMAP_ISON(phb->raw[index].current_liveset, index) &&
                MTC_HOSTMAP_ISON(phb->raw[index].proposed_liveset, index))
            {
                // Validate if join can be allowed
                if (!smvar.join_block &&
                    (MTC_HOSTMAP_ISON(psf->sfdomain, index) || psm->SR2))
                {
                    // Send Join Ack
                    if (!MTC_HOSTMAP_ISON(psm->proposed_liveset, index))
                    {
                        MTC_HOSTMAP_SET(psm->proposed_liveset, index);
                        log_message(MTC_LOG_DEBUG,
                            "Join Agent: Send ack to join request from host (%d).\n", index);
                    }
                }

                if (!MTC_HOSTMAP_ISON(psm->current_liveset, index))
                {
                    MTC_BOOLEAN allowed = TRUE;

                    // Check if all other hosts acked to the Join request
                    for (index2 = 0; _is_configured_host(index2); index2++)
                    {
                        if (MTC_HOSTMAP_ISON(psm->current_liveset, index2) &&
                            ((index2 == _my_index)?
                             !MTC_HOSTMAP_ISON(psm->proposed_liveset, index):
                             !MTC_HOSTMAP_ISON(phb->raw[index2].proposed_liveset, index)))
                        {
                            allowed = FALSE;
                            break;
                        }
                    }
                    if (allowed)
                    {
                        // join is allowed
                        MTC_HOSTMAP_SET(joining_set, index);
                        log_message(MTC_LOG_INFO,
                            "Join Agent: Join request from host (%d) is accepted by the local host.\n", index);
                    }
                }
            }
            // TBD - We may be able to merge this clause to the above.
            // If the peer thinks he is in the liveset, accept join.
            else if (index != _my_index &&
                     MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                     MTC_HOSTMAP_ISON(phb->raw[index].current_liveset, index))
            {
                if (!MTC_HOSTMAP_ISON(psm->current_liveset, index))
                {
                    log_message(MTC_LOG_INFO,
                        "Join Agent: Join request from host (%d) has been accepted by the other hosts.\n", index);
                    MTC_HOSTMAP_SET(psm->proposed_liveset, index);
                    MTC_HOSTMAP_SET(joining_set, index);
                }
            }
        }
    }

    MTC_HOSTMAP_UNION(psm->current_liveset, '=', psm->current_liveset, '|', joining_set);
    if (!is_empty_liveset(joining_set))
    {
        print_liveset(MTC_LOG_NOTICE,
            "Liveset has been updated.  new liveset = (%s)\n", psm->current_liveset);
        print_liveset(MTC_LOG_INFO,
            "Join Agent: proposed_liveset = (%s)\n", psm->proposed_liveset);
    }

    com_reader_unlock(sf_object);
    com_reader_unlock(hb_object);
    com_writer_unlock(sm_object);

    // accelerate to send heartbeat to notify my accept
    if (!is_empty_liveset(joining_set))
    {
        hb_send_hb_now(HB_ACCELERATION_COUNT_JOIN);
    }
}


MTC_STATIC void *
sm_worker(
    void *ignore)
{
    log_message(MTC_LOG_INFO, "SM_Worker: thread ID: %ld.\n", syscall(SYS_gettid));
    while (!smvar.terminate)
    {
        mssleep(SM_WORKER_INTERVAL);

        check_pool_state();

        if (update_sfdomain())
        {
            start_fh(FALSE);
        }

        process_join_request();

        if (smvar.SR2)
        {
            surviving_by_SR2();
        }
    }

    return NULL;
}


MTC_STATIC void
surviving_by_SR2()
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    MTC_S32         index;
    MTC_BOOLEAN     recovered;

    sf_watchdog_set();

    com_reader_lock(hb_object, (void **) &phb);
    com_reader_lock(sf_object, (void **) &psf);

    // Check if recovering Survival Rule-2
    recovered = TRUE;
    for (index = 0; _is_configured_host(index); index++)
    {
        if (index == _my_index)
        {
            if (!psf->SF_access)
            {
                recovered = FALSE;
            }
        }
        else if (MTC_HOSTMAP_ISON(phb->hbdomain, index))
        {
            if (! MTC_HOSTMAP_ISON(phb->raw[index].sfdomain, index))
            {
                recovered = FALSE;
            }
        }
        else // !(MTC_HOSTMAP_ISON(phb->hbdomain, index))
        {
            if  (! MTC_HOSTMAP_ISON(psf->excluded, index))
            {
                recovered = FALSE;
            }
        }
    }

    com_reader_unlock(sf_object);
    com_reader_unlock(hb_object);

    if (recovered)
    {
        com_writer_lock(sm_object, (void **) &psm);
        psm->SR2 = smvar.SR2 = FALSE;
        com_writer_unlock(sm_object);
        log_message(MTC_LOG_DEBUG, "SM: I am leaving from Survival Rule-2\n");
    }
}


void
start_fh(
    MTC_BOOLEAN force)
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    MTC_BOOLEAN     hb_degraded = FALSE,
                    sf_degraded = FALSE,
                    force_requested = FALSE;
    MTC_S32         index;

    com_reader_lock(sm_object, (void **) &psm);
    com_writer_lock(hb_object, (void **) &phb);
    com_writer_lock(sf_object, (void **) &psf);
    for (index = 0; _is_configured_host(index); index++)
    {
        // check if up on HB domain
        if (MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
            !MTC_HOSTMAP_ISON(smvar.last_hbdomain, index))
        {
            MTC_HOSTMAP_SET(smvar.last_hbdomain, index);
        }
        // check if up on SF domain
        if (MTC_HOSTMAP_ISON(psf->sfdomain, index) &&
            !MTC_HOSTMAP_ISON(smvar.last_sfdomain, index))
        {
            MTC_HOSTMAP_SET(smvar.last_sfdomain, index);
        }

        // check if down on HB domain
        if (MTC_HOSTMAP_ISON(smvar.last_hbdomain, index) &&
            !MTC_HOSTMAP_ISON(phb->hbdomain, index))
        {
            MTC_HOSTMAP_RESET(smvar.last_hbdomain, index);
            hb_degraded = MTC_HOSTMAP_ISON(psm->current_liveset, index);
        }
        // check if down on SF domain
        if (MTC_HOSTMAP_ISON(smvar.last_sfdomain, index) &&
            !MTC_HOSTMAP_ISON(psf->sfdomain, index))
        {
            MTC_HOSTMAP_RESET(smvar.last_sfdomain, index);
            sf_degraded = MTC_HOSTMAP_ISON(psm->current_liveset, index);
        }
    }

    if (force && smvar.phase == SM_PHASE_STARTED)
    {
        force_requested = TRUE;
    }
    com_writer_unlock(sf_object);
    com_writer_unlock(hb_object);
    com_reader_unlock(sm_object);

    if (smvar.SR2 && sf_degraded)
    {
        log_message(MTC_LOG_DEBUG,
            "SM: degrade on SF domain is ignored becase of SR2.\n");
        sf_degraded = FALSE;
    }

    if (hb_degraded || sf_degraded || force_requested)
    {
        switch (smvar.phase)
        {
        case SM_PHASE_FHREADY:
        case SM_PHASE_FH1:
        case SM_PHASE_FH1DONE:
        case SM_PHASE_FH2:
        case SM_PHASE_FH2DONE:
            break;

        default:
            // signal to the fault handler
            pthread_mutex_lock(&smvar.mutex);
            smvar.need_fh = TRUE;
            pthread_cond_broadcast(&smvar.cond);
            pthread_mutex_unlock(&smvar.mutex);
            break;
        }
    }
}


MTC_STATIC MTC_BOOLEAN
wait_fh_event()
{
    MTC_BOOLEAN need_fh = FALSE;

    pthread_mutex_lock(&smvar.mutex);
    while (!smvar.need_fh)
    {
        pthread_cond_wait(&smvar.cond, &smvar.mutex);
    }
    need_fh = smvar.need_fh;
    smvar.need_fh = FALSE;
    smvar.phase = SM_PHASE_FHREADY;
    pthread_mutex_unlock(&smvar.mutex);

    set_sm_phase(smvar.phase);

    return need_fh && !smvar.terminate;
}


MTC_STATIC void
check_pool_state()
{
    switch(get_pool_state())
    {
    case SF_STATE_INVALID:
        sm_abort(MTC_ERROR_SM_INVALID_POOL_STATE, NULL);
        break;

    case SF_STATE_INIT:
        if (smvar.phase != SM_PHASE_STARTING && smvar.phase != SM_PHASE_ABORTED)
        {
            sm_abort(MTC_ERROR_SM_INVALID_INIT_POOL_STATE, NULL);
        }
        break;

    // NONE is OK, because pool state is never set back to NONE once the state
    // is set to valid state.  The NONE state is handled at start criteria test.
    case SF_STATE_NONE:
    case SF_STATE_ACTIVE:
    default:
        break;
    }
}


MTC_STATIC MTC_BOOLEAN
update_sfdomain()
{
    MTC_CLOCK       now;
    MTC_S8          hostmap[MAX_HOST_NUM + 1] = {0};
    MTC_BOOLEAN     changed = FALSE;

    MTC_BOOLEAN update_sfdomain_sub(
        MTC_BOOLEAN writable)
    {
        PCOM_DATA_SF    psf;
        MTC_BOOLEAN changed = FALSE;
        MTC_S32     index;

        if (writable)
        {
            com_writer_lock(sf_object, (void **) &psf);
        }
        else
        {
            com_reader_lock(sf_object, (void **) &psf);
        }

        for (index = 0; _is_configured_host(index); index++)
        {
            if (index == _my_index && !psf->ctl.enable_SF_write)
            {
                hostmap[index] = (psf->SF_access)? 'D': 'd';
                if (MTC_HOSTMAP_ISON(psf->sfdomain, index))
                {
                    if (writable)
                    {
                        MTC_HOSTMAP_RESET(psf->sfdomain, index);
                    }
                    changed = TRUE;
                }
            }
            else if (psf->time_last_SF[index] >= 0 &&
                     now - psf->time_last_SF[index] < _T2 * ONE_SEC)
            {
                if (MTC_HOSTMAP_ISON(psf->sfdomain, index))
                {
                    hostmap[index] = '1';
                }
                else
                {
                    if (!smvar.join_block)
                    {
                        if (writable)
                        {
                            MTC_HOSTMAP_SET(psf->sfdomain, index);
                        }
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
                if (index == _my_index)
                {
                    if (writable)
                    {
                        psf->SF_access = FALSE;
                    }
                }

                if (!MTC_HOSTMAP_ISON(psf->sfdomain, index))
                {
                    hostmap[index] = '0';
                }
                else
                {
                    if (writable)
                    {
                        MTC_HOSTMAP_RESET(psf->sfdomain, index);
                    }
                    hostmap[index] = '_';
                    changed = TRUE;
                }
            }
        }
        hostmap[index] = '\0';

        if (writable)
        {
            com_writer_unlock(sf_object);
        }
        else
        {
            com_reader_unlock(sf_object);
        }

        return changed;
    }

    now = _getms();

    changed = update_sfdomain_sub(FALSE);
    if (changed)
    {
        changed = update_sfdomain_sub(TRUE);
        if (changed)
        {
            log_message(MTC_LOG_DEBUG,
                "SM: SF domain is updated [sfdomain = (%s)].\n", hostmap);
        }
    }

    return changed;
}


MTC_BOOLEAN
sm_get_join_block()
{
    return smvar.join_block;
}


MTC_STATIC MTC_BOOLEAN
get_excluded_flag(
    MTC_S32 host)
{
    PCOM_DATA_SF    psf;
    MTC_U32         excluded;

    com_reader_lock(sf_object, (void **) &psf);
    excluded = MTC_HOSTMAP_ISON(psf->excluded, host);
    com_reader_unlock(sf_object);

    return excluded;
}


MTC_STATIC MTC_U32
get_pool_state()
{
    PCOM_DATA_SF    psf;
    MTC_U32         pool_state;

    com_reader_lock(sf_object, (void **) &psf);
    pool_state = psf->pool_state;
    com_reader_unlock(sf_object);

    return pool_state;
}


MTC_STATIC MTC_S32
get_partition_size(
    MTC_HOSTMAP sfdomain,
    MTC_HOSTMAP hbdomain,
    MTC_U32     weight[])
{
    MTC_S32 index, size = 0;

    if (is_empty_liveset(sfdomain))
    {
        return 0;
    }

    for (index = 0; _is_configured_host(index); index++)
    {
        if (MTC_HOSTMAP_ISON(sfdomain, index) && MTC_HOSTMAP_ISON(hbdomain, index))
        {
            if (!weight)
            {
                size++;
            }
            else
            {
                size++;
                size += 0x100 * weight[index];
            }
        }
    }

    return size;
}


MTC_STATIC MTC_S64
get_partition_score(
    MTC_HOSTMAP hostmap,
    MTC_U32     weight[])
{
    MTC_S64 index, size_score = 0, index_score = 0, weight_score = 0;

    for (index = ha_config.common.hostnum - 1; index >= 0; index--)
    {
        if (MTC_HOSTMAP_ISON(hostmap, index))
        {
            weight_score += weight[index];
            size_score++;
            index_score = MAX_HOST_NUM - index;
        }
    }

    return (weight_score * 0x10000 + size_score * 0x100 + index_score);
}


MTC_STATIC MTC_BOOLEAN
am_I_in_largest_partition(
    PCOM_DATA_HB phb,
    PCOM_DATA_SF psf,
    MTC_U32      weight[])
{
    MTC_S32     winner_size = 0,
                winner_index = -1,
                partition_size[MAX_HOST_NUM] = {0},
                index;
    MTC_BOOLEAN winner = FALSE;

    for (index = 0; _is_configured_host(index); index++)
    {
        if (!MTC_HOSTMAP_ISON(psf->sfdomain, index))
        {
            partition_size[index] = 0;
        }
        else if (index == _my_index)
        {
            partition_size[index] = get_partition_size(psf->sfdomain, phb->hbdomain, weight);
        }
        else
        {
            partition_size[index] =
                get_partition_size(psf->raw[index].sfdomain, psf->raw[index].hbdomain, weight);
        }

        if (partition_size[index] > winner_size)
        {
            winner_index = index;
            winner_size = partition_size[index];
        }
        log_message(MTC_LOG_WARNING,
            "SM: partition_size[%d] = %d %d\n",
            index, partition_size[index] / 0x100, partition_size[index] % 0x100);
    }

    log_message(MTC_LOG_WARNING, "SM: winner_index = %d\n", winner_index);
    if (winner_index >= 0)
    {
        if (winner_index == _my_index ||
            (MTC_HOSTMAP_ISON(psf->raw[winner_index].sfdomain, _my_index) &&
             MTC_HOSTMAP_ISON(psf->raw[winner_index].hbdomain, _my_index) &&
             MTC_HOSTMAP_ISON(psf->sfdomain, winner_index) &&
             MTC_HOSTMAP_ISON(phb->hbdomain, winner_index)))
        {
            winner = TRUE;
        }
    }

    return winner;
}


MTC_STATIC MTC_BOOLEAN
is_all_hosts_up(
    MTC_HOSTMAP hostmap)
{
    MTC_S32     index;

    for (index = 0; _is_configured_host(index); index++)
    {
        if (!MTC_HOSTMAP_ISON(hostmap, index))
        {
            return FALSE;
        }
    }
    return TRUE;
}


MTC_STATIC MTC_BOOLEAN
is_empty_liveset(
    MTC_HOSTMAP liveset)
{
    MTC_S32 index;

    for (index = 0; _is_configured_host(index); index++)
    {
        if (MTC_HOSTMAP_ISON(liveset, index))
        {
            return FALSE;
        }
    }

    return TRUE;
}


// synchronize functions

MTC_STATIC MTC_STATUS
wait_all_hosts_recognize_I_was_down()
{
    MTC_BOOLEAN     all_recognized;
    MTC_U32         pool_state = get_pool_state();
    MTC_CLOCK       timeout;

    // Wait until all hosts recognize my down
    do
    {
        PCOM_DATA_HB    phb;
        PCOM_DATA_SF    psf;
        MTC_S32         index;

        all_recognized = TRUE;
        com_reader_lock(hb_object, (void **) &phb);
        com_reader_lock(sf_object, (void **) &psf);
        for (index = 0; _is_configured_host(index); index++)
        {

            if (index != _my_index &&
                ((MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                  MTC_HOSTMAP_ISON(phb->raw[index].current_liveset, _my_index)) ||
                 (MTC_HOSTMAP_ISON(psf->sfdomain, index) &&
                  MTC_HOSTMAP_ISON(psf->raw[index].current_liveset, _my_index))))
            {
                all_recognized = FALSE;
            }
        }
        com_reader_unlock(sf_object);
        com_reader_unlock(hb_object);
        if (all_recognized)
        {
            break;
        }

        timeout = ((pool_state == SF_STATE_INIT)? _Tenable: _Tboot) * ONE_SEC
                - (_getms() - smvar.start_time);
        timeout = (timeout < 0)? 0: timeout;
    } while (sm_wait_signals_sm_hb_sf(FALSE, TRUE, TRUE, timeout));

    return (all_recognized)? MTC_SUCCESS: MTC_ERROR_SM_BOOT_TIMEOUT;
}


MTC_STATIC void
wait_all_hosts_up_on_heartbeat(
    MTC_CLOCK   timeout)
{
    PCOM_DATA_HB    phb;
    MTC_CLOCK       to;

    do
    {
        com_reader_lock(hb_object, (void **) &phb);
        if (is_all_hosts_up(phb->hbdomain))
        {
            com_reader_unlock(hb_object);
            break;
        }
        com_reader_unlock(hb_object);

        to = timeout - (_getms() - smvar.start_time);
        to = (to < 0)? 0: to;
    } while (sm_wait_signals_sm_hb_sf(FALSE, TRUE, FALSE, to));
}


MTC_STATIC void
wait_all_hosts_up_on_statefile(
    MTC_CLOCK   timeout)
{
    PCOM_DATA_SF    psf;
    MTC_CLOCK       to;

    do
    {
        com_reader_lock(sf_object, (void **) &psf);
        if (is_all_hosts_up(psf->sfdomain))
        {
            com_reader_unlock(sf_object);
            break;
        }
        com_reader_unlock(sf_object);

        to = timeout - (_getms() - smvar.start_time);
        to = (to < 0)? 0: to;
    } while (sm_wait_signals_sm_hb_sf(FALSE, FALSE, TRUE, to));
}


MTC_STATIC void
wait_until_HBSF_state_stable()
{
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    MTC_BOOLEAN     stable;
    MTC_CLOCK       start_time, now;
    MTC_S32         index;
    MTC_BOOLEAN     logged_hb[MAX_HOST_NUM];
    MTC_BOOLEAN     logged_sf[MAX_HOST_NUM];

    for (index = 0; _is_configured_host(index); index++)
    {
        logged_hb[index] = FALSE;
        logged_sf[index] = FALSE;
    }

    stable = FALSE;
    start_time = _getms();
    while (!stable)
    {
        stable = TRUE;
        now = _getms();

        com_reader_lock(hb_object, (void **) &phb);
        com_reader_lock(sf_object, (void **) &psf);
        for (index = 0; _is_configured_host(index); index++)
        {
            if (MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                (phb->time_last_HB[index] >= 0) &&
                (now - phb->time_last_HB[index] > _T1 * 10 * APPROACHING_TIMEOUT_FACTOR) &&
                (now - phb->time_last_HB[index] > now - start_time))
            {
                if (!logged_hb[index] && MTC_HOSTMAP_ISON(phb->hbdomain, index))
                {
                    log_maskable_debug_message(FH_TRACE,
                        "FH: waiting for HB from host (%d),"
                        " time since last HB receive = %d.\n",
                        index, now - phb->time_last_HB[index]);
                    logged_hb[index] = TRUE;
                }
                stable = FALSE;
            }
            if (MTC_HOSTMAP_ISON(psf->sfdomain, index) &&
                (psf->time_last_SF[index] >= 0) &&
                (now - psf->time_last_SF[index] > _T2 * 10 * APPROACHING_TIMEOUT_FACTOR) &&
                (now - psf->time_last_SF[index] > now - start_time))
            {
                if (!logged_sf[index] && MTC_HOSTMAP_ISON(psf->sfdomain, index))
                {
                    log_maskable_debug_message(FH_TRACE,
                        "FH: waiting for SF from host (%d),"
                        " time since last SF update = %d.\n",
                        index,
                        now - psf->time_last_SF[index]);
                    logged_sf[index] = TRUE;
                }
                stable = FALSE;
            }
            if (logged_hb[index] &&
                ((now - phb->time_last_HB[index] <= _T1 * 10 * APPROACHING_TIMEOUT_FACTOR) ||
                 (now - phb->time_last_HB[index] <= now - start_time)))
            {
                logged_hb[index] = FALSE;
            }
            if (logged_sf[index] &&
                ((now - psf->time_last_SF[index] <= _T2 * 10 * APPROACHING_TIMEOUT_FACTOR) ||
                 (now - psf->time_last_SF[index] <= now - start_time)))
            {
                logged_sf[index] = FALSE;
            }
        }
        com_reader_unlock(sf_object);
        com_reader_unlock(hb_object);

        if (!stable)
        {
            sm_wait_signals_sm_hb_sf(FALSE, TRUE, TRUE, -1);
        }
    }
}


MTC_STATIC MTC_BOOLEAN
wait_until_all_hosts_have_consistent_view(
    MTC_CLOCK   timeout)
{
    PCOM_DATA_SM    psm;
    PCOM_DATA_HB    phb;
    PCOM_DATA_SF    psf;
    MTC_BOOLEAN     consistent = FALSE;
    MTC_S32         index, index2, selected;
    MTC_S64         score, minimum;
    MTC_HOSTMAP     my_hbdomain, remote_hbdomain, my_sfdomain, remote_sfdomain,
                    remote_hbdomain_onsf, remote_sfdomain_onhb,
                    removedhost, tmp_hostmap;

#if 1   // TBD - do we need this timeout?
    MTC_CLOCK       start = _getms();

    while (!consistent && _getms() - start < timeout)
#else
    while (!consistent)
#endif
    {
        consistent = TRUE;

        com_reader_lock(sm_object, (void **) &psm);
        com_writer_lock(hb_object, (void **) &phb);
        com_writer_lock(sf_object, (void **) &psf);

        MTC_HOSTMAP_MASK_UNCONFIG(phb->hbdomain);
        MTC_HOSTMAP_MASK_UNCONFIG(psf->sfdomain);
        MTC_HOSTMAP_INTERSECTION(my_hbdomain, '=',
                                    phb->hbdomain, '&', psm->current_liveset);
        MTC_HOSTMAP_INTERSECTION(my_sfdomain, '=',
                                    psf->sfdomain, '&', psm->current_liveset);

        for (index = 0; _is_configured_host(index); index++)
        {
            if (index != _my_index &&
                MTC_HOSTMAP_ISON(psm->current_liveset, index))
            {
                MTC_HOSTMAP_MASK_UNCONFIG(phb->raw[index].hbdomain);
                MTC_HOSTMAP_MASK_UNCONFIG(psf->raw[index].sfdomain);
                MTC_HOSTMAP_INTERSECTION(remote_hbdomain, '=',
                                    phb->raw[index].hbdomain, '&', psm->current_liveset);
                MTC_HOSTMAP_INTERSECTION(remote_sfdomain, '=',
                                    psf->raw[index].sfdomain, '&', psm->current_liveset);

                MTC_HOSTMAP_MASK_UNCONFIG(phb->raw[index].sfdomain);
                MTC_HOSTMAP_MASK_UNCONFIG(psf->raw[index].hbdomain);
                MTC_HOSTMAP_INTERSECTION(remote_hbdomain_onsf, '=',
                                    psf->raw[index].hbdomain, '&', psm->current_liveset);
                MTC_HOSTMAP_INTERSECTION(remote_sfdomain_onhb, '=',
                                    phb->raw[index].sfdomain, '&', psm->current_liveset);

                if ((MTC_HOSTMAP_ISON(my_hbdomain, index) &&
                     MTC_HOSTMAP_COMPARE(my_hbdomain, '!=', remote_hbdomain))
                    ||
                    (MTC_HOSTMAP_ISON(my_sfdomain, index) &&
                     MTC_HOSTMAP_COMPARE(my_sfdomain, '!=', remote_sfdomain))
                    ||
                    (MTC_HOSTMAP_ISON(my_sfdomain, index) &&
                     MTC_HOSTMAP_COMPARE(my_hbdomain, '!=', remote_hbdomain_onsf))
                    ||
                    (MTC_HOSTMAP_ISON(my_hbdomain, index) &&
                     MTC_HOSTMAP_COMPARE(my_sfdomain, '!=', remote_sfdomain_onhb)))

                {
                    consistent = FALSE;
                    break;
                }
            }
        }
        com_writer_unlock(sf_object);
        com_writer_unlock(hb_object);
        com_reader_unlock(sm_object);

        if (!consistent)
        {
            mssleep(100);
            sm_wait_signals_sm_hb_sf(TRUE, TRUE, TRUE, -1);
        }
    }

    com_reader_lock(sm_object, (void **) &psm);
    com_reader_lock(hb_object, (void **) &phb);
    com_reader_lock(sf_object, (void **) &psf);
    smvar.stable_hb = *phb;
    smvar.stable_sf = *psf;
    com_reader_unlock(sf_object);
    com_reader_unlock(hb_object);
    phb = &(smvar.stable_hb);
    psf = &(smvar.stable_sf);

    if (!consistent)
    {
        log_message(MTC_LOG_WARNING, "Host (%d) and the local host do not agre on the view to the pool membership.\n", index);
        print_liveset(MTC_LOG_WARNING, "\tlocal HB domain = (%s)\n", my_hbdomain);
        print_liveset(MTC_LOG_WARNING, "\tlocal SF domain = (%s)\n", my_sfdomain);
        print_liveset(MTC_LOG_WARNING, "\tremote HB domain = (%s)\n", remote_hbdomain);
        print_liveset(MTC_LOG_WARNING, "\tremote SF domain = (%s)\n", remote_sfdomain);
        print_liveset(MTC_LOG_WARNING, "\tremote HB domain on SF = (%s)\n", remote_hbdomain_onsf);
        print_liveset(MTC_LOG_WARNING, "\tremote SF domain on HB = (%s)\n", remote_sfdomain_onhb);

        MTC_HOSTMAP_COPY(phb->raw[_my_index].hbdomain, my_hbdomain);
        MTC_HOSTMAP_COPY(psf->raw[_my_index].hbdomain, my_hbdomain);
        MTC_HOSTMAP_COPY(phb->raw[_my_index].sfdomain, my_sfdomain);
        MTC_HOSTMAP_COPY(psf->raw[_my_index].sfdomain, my_sfdomain);

        for (index = 0; _is_configured_host(index); index++)
        {
            MTC_HOSTMAP_INTERSECTION(phb->raw[index].hbdomain, '=',
                                    phb->raw[index].hbdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother HB domain = (%s)\n", phb->raw[index].hbdomain);

            MTC_HOSTMAP_INTERSECTION(psf->raw[index].sfdomain, '=',
                                    psf->raw[index].sfdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother SF domain = (%s)\n", psf->raw[index].sfdomain);

            MTC_HOSTMAP_INTERSECTION(psf->raw[index].hbdomain, '=',
                                    psf->raw[index].hbdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother HB domain on SF = (%s)\n", psf->raw[index].hbdomain);

            MTC_HOSTMAP_INTERSECTION(phb->raw[index].sfdomain, '=',
                                    phb->raw[index].sfdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother SF domain on HB = (%s)\n", phb->raw[index].sfdomain);
        }
        for (index = 0; _is_configured_host(index); index++)
        {
            log_message(MTC_LOG_WARNING, "\tweight[%d] = %d\n", index, psf->weight[index]);
        }

        // remove hosts that have lost state file
        for (index = 0; _is_configured_host(index); index++)
        {
            if (index == _my_index)
            {
                continue;
            }

            if (is_empty_liveset(psf->raw[index].sfdomain) ||
                is_empty_liveset(phb->raw[index].sfdomain))
            {
                MTC_HOSTMAP_RESET(my_sfdomain, index);
                MTC_HOSTMAP_RESET(psf->sfdomain, index);
                MTC_HOSTMAP_RESET(psf->raw[_my_index].sfdomain, index);
                MTC_HOSTMAP_RESET(phb->raw[_my_index].sfdomain, index);
            }
        }

        MTC_HOSTMAP_INIT_RESET(removedhost);
        // remove all hosts do not have state file access
        for (index = 0; _is_configured_host(index); index++)
        {
            if (!MTC_HOSTMAP_ISON(my_sfdomain, index))
            {
                MTC_HOSTMAP_INIT_RESET(psf->raw[index].hbdomain);
                for (index2 = 0; _is_configured_host(index2); index2++)
                {
                    MTC_HOSTMAP_RESET(psf->raw[index2].hbdomain, index);
                }
                MTC_HOSTMAP_SET(removedhost, index);
            }
        }
        // find out (probably) the largest partition that has perfect connectivity on heartbeat
        while (TRUE)
        {
            // select a host that has the smallest hbdomain.  the domain include smaller node index
            // will survibe when tie-break is required.
            selected = -1;
            minimum = -1;
            for (index = ha_config.common.hostnum - 1; index >= 0; index--)
            {
                if (MTC_HOSTMAP_ISON(removedhost, index))
                {
                    continue;
                }
                score = get_partition_score(psf->raw[index].hbdomain, psf->weight);
                if (selected < 0 || score < minimum)
                {
                    selected = index;
                    minimum = score;
                }
            }
            if (selected < 0)
            {
                MTC_HOSTMAP_COPY(tmp_hostmap, removedhost);
            }
            else
            {
                MTC_HOSTMAP_UNION(tmp_hostmap, '=', psf->raw[selected].hbdomain, '|', removedhost);
            }
            if (is_all_hosts_up(tmp_hostmap))
            {
                break;
            }
            // remove the selected host
            MTC_HOSTMAP_INIT_RESET(psf->raw[selected].hbdomain);
            for (index = 0; _is_configured_host(index); index++)
            {
                MTC_HOSTMAP_RESET(psf->raw[index].hbdomain, selected);
            }
            MTC_HOSTMAP_SET(removedhost, selected);
        }

        log_message(MTC_LOG_WARNING, "after merger:\n", index);
        for (index = 0; _is_configured_host(index); index++)
        {
            MTC_HOSTMAP_INTERSECTION(phb->raw[index].hbdomain, '=',
                                    phb->raw[index].hbdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother HB domain = (%s)\n", phb->raw[index].hbdomain);

            MTC_HOSTMAP_INTERSECTION(psf->raw[index].sfdomain, '=',
                                    psf->raw[index].sfdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother SF domain = (%s)\n", psf->raw[index].sfdomain);

            MTC_HOSTMAP_INTERSECTION(psf->raw[index].hbdomain, '=',
                                    psf->raw[index].hbdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother HB domain on SF = (%s)\n", psf->raw[index].hbdomain);

            MTC_HOSTMAP_INTERSECTION(phb->raw[index].sfdomain, '=',
                                    phb->raw[index].sfdomain, '&', psm->current_liveset);
            print_liveset(MTC_LOG_WARNING,
                "\tother SF domain on HB = (%s)\n", phb->raw[index].sfdomain);
        }
        for (index = 0; _is_configured_host(index); index++)
        {
            log_message(MTC_LOG_WARNING, "\tweight[%d] = %d\n", index, psf->weight[index]);
        }

        MTC_HOSTMAP_COPY(phb->hbdomain, psf->raw[_my_index].hbdomain);
        MTC_HOSTMAP_COPY(psf->sfdomain, my_sfdomain);

        print_liveset(MTC_LOG_WARNING, "\tmerged HB domain = (%s)\n", phb->hbdomain);
        print_liveset(MTC_LOG_WARNING, "\tmerged SF domain = (%s)\n", psf->sfdomain);

        consistent = TRUE;
    }
    else
    {
        log_message(MTC_LOG_WARNING, "All hosts now have a consistent view.\n");
        print_liveset(MTC_LOG_WARNING, "\tHB domain = (%s)\n", phb->hbdomain);
        print_liveset(MTC_LOG_WARNING, "\tSF domain = (%s)\n", psf->sfdomain);
    }
    com_reader_unlock(sm_object);

    return consistent;
}


MTC_STATIC MTC_BOOLEAN
wait_until_all_hosts_booted(
    MTC_CLOCK   timeout)
{
    PCOM_DATA_HB    phb;
    MTC_BOOLEAN     booted;
    MTC_S32         index;
    MTC_CLOCK       to;

    do
    {
        com_reader_lock(hb_object, (void **) &phb);
        booted = TRUE;
        for (index = 0; _is_configured_host(index); index++)
        {
            if (index != _my_index &&
                MTC_HOSTMAP_ISON(phb->hbdomain, index) &&
                !MTC_HOSTMAP_ISON(phb->raw[index].proposed_liveset, _my_index))
            {
                booted = FALSE;
                break;
            }
        }
        com_reader_unlock(hb_object);

        if (booted)
        {
            break;
        }

        to = timeout - (_getms() - smvar.start_time);
        to = (to < 0)? 0: to;
    } while (sm_wait_signals_sm_hb_sf(FALSE, TRUE, FALSE, to));

    return booted;
}


MTC_STATIC MTC_U32
commit_weight()
{
    MTC_U32         weight;
    PCOM_DATA_SM    psm;

    com_writer_lock(sm_object, (void **) &psm);
    weight = psm->commited_weight = psm->weight;
    com_writer_unlock(sm_object);

    return weight;
}


//  Cotroll other objects

MTC_STATIC void
start_statefile()
{
    PCOM_DATA_SF    psf;

    com_writer_lock(sf_object, (void **) &psf);
    psf->ctl.starting = TRUE;
    psf->ctl.enable_SF_read = TRUE;
    psf->ctl.enable_SF_write = TRUE;
    com_writer_unlock(sf_object);
}


MTC_STATIC void
clear_statefile_starting()
{
    PCOM_DATA_SF    psf;

    com_writer_lock(sf_object, (void **) &psf);
    psf->ctl.starting = FALSE;
    com_writer_unlock(sf_object);
}

MTC_STATIC void
start_heartbeat()
{
    PCOM_DATA_HB    phb;

    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.join = FALSE;
    phb->ctl.enable_HB_receive = TRUE;
    phb->ctl.enable_HB_send = TRUE;
    com_writer_unlock(hb_object);
}

MTC_STATIC void
set_heartbeat_join()
{
    PCOM_DATA_HB    phb;

    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.join = TRUE;
    com_writer_unlock(hb_object);
}

MTC_STATIC void
fence_request()
{
    PCOM_DATA_HB    phb;

    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.fence_request = TRUE;
    com_writer_unlock(hb_object);
}

MTC_STATIC void
cancel_fence_request()
{
    PCOM_DATA_HB    phb;

    com_writer_lock(hb_object, (void **) &phb);
    phb->ctl.fence_request = FALSE;
    com_writer_unlock(hb_object);
}

MTC_STATIC void
start_xapi_monitor()
{
    PCOM_DATA_XAPIMON   pxapimon;

    com_writer_lock(xapimon_object, (void **) &pxapimon);
    pxapimon->ctl.enable_Xapi_monitor = TRUE;
    com_writer_unlock(xapimon_object);
}


MTC_STATIC void
arm_fencing()
{
    PCOM_DATA_SM    psm;

    com_writer_lock(sm_object, (void **) &psm);
    smvar.fencing = psm->fencing = FENCING_ARMED;
    com_writer_unlock(sm_object);
}


// print liveset

MTC_STATIC void
print_liveset(
    MTC_S32     pri,
    PMTC_S8     log_string,
    MTC_HOSTMAP hostmap)
{
    MTC_S8          liveset[MAX_HOST_NUM + 1] = {'\0'};
    MTC_S32         index;

    for (index = 0; _is_configured_host(index); index++)
    {
        liveset[index] = (MTC_HOSTMAP_ISON(hostmap, index))? '1': '0';
    }

    log_message(pri, log_string, liveset);
}


MTC_STATIC void
sm_send_signals_sm_hb_sf(
    MTC_BOOLEAN sm_sig,
    MTC_BOOLEAN hb_sig,
    MTC_BOOLEAN sf_sig)
{
    // send signal
    pthread_mutex_lock(&smvar.mutex);
    if (sm_sig)
    {
        smvar.sm_sig = TRUE;
    }
    if (hb_sig)
    {
        smvar.hb_sig = TRUE;
    }
    if (sf_sig)
    {
        smvar.sf_sig = TRUE;
    }
    pthread_cond_broadcast(&smvar.cond);
    pthread_mutex_unlock(&smvar.mutex);
}


MTC_STATIC MTC_BOOLEAN
sm_wait_signals_sm_hb_sf(
    MTC_BOOLEAN sm_sig,
    MTC_BOOLEAN hb_sig,
    MTC_BOOLEAN sf_sig,
    MTC_CLOCK   timeout)
{
    MTC_BOOLEAN signaled;
    MTC_CLOCK   start = _getms();

    MTC_BOOLEAN check_sigs()
    {
        MTC_BOOLEAN signaled = FALSE;

        if (sm_sig && smvar.sm_sig)
        {
            smvar.sm_sig = FALSE;
            signaled = TRUE;
        }
        if (hb_sig && smvar.hb_sig)
        {
            smvar.hb_sig = FALSE;
            signaled = TRUE;
        }
        if (sf_sig && smvar.sf_sig)
        {
            smvar.sf_sig = FALSE;
            signaled = TRUE;
        }

        return signaled;
    }

    if (timeout == 0)
    {
        return FALSE;
    }

    pthread_mutex_lock(&smvar.mutex);
    while (!(signaled = check_sigs()) &&
           ((timeout < 0)? TRUE: (_getms() - start < timeout)))
    {
        if (timeout < 0)
        {
            pthread_cond_wait(&smvar.cond, &smvar.mutex);
        }
        else
        {
            pthread_mutex_unlock(&smvar.mutex);
            mssleep(100);
            pthread_mutex_lock(&smvar.mutex);
        }
    }
    pthread_mutex_unlock(&smvar.mutex);

    return signaled;
}
