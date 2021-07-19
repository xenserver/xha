//
//++
//
//  MODULE: main.c 
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
//      xhad main.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      March 4, 2008
//
//--
//


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

    // Included via CEXPORT.H

//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

#include "log.h"
#include "mtcerrno.h"
#include "config.h"
#include "com.h"
#include "lock_mgr.h"
#include "bond_mon.h"
#include "heartbeat.h"
#include "statefile.h"
#include "xapi_mon.h"
#include "script.h"
#include "sm.h"
#include "buildid.h"
#include "fist.h"
#include "hostweight.h"

#define HA_DAEMON_LOCK_FILE     "/var/run/xhad.lock"

//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//

HA_CONFIG   ha_config;

MTC_STATUS  exit_status = MTC_SUCCESS;

// pthread attr to set stacksize;
pthread_attr_t *xhad_pthread_attr = NULL;


//
//
//  L O C A L   D E F I N I T I O N S
//
//

typedef struct _DAEMON_VARS
{
    char        *name;          //  name of HA daemon
    char        *config_path;   //  Config-file path
    pthread_t            sighandler_thread;
    volatile MTC_U32     terminate_requested;
    volatile MTC_U32     catch_SIGHUP;
    volatile MTC_U32     catch_unexpected_signo;
} DAEMON_VARS, *PDAEMON_VARS;

static DAEMON_VARS daemon_vars = {0};

//  Initialization functions

typedef int (*INIT_FUNC)(
    MTC_S32 phase);

static INIT_FUNC init_funcs[] =
{
    com_initialize,
    sf_initialize,
    lm_initialize,
    bm_initialize,
    hb_initialize,
    xapimon_initialize,
    script_initialize,
    hostweight_initialize,
    sm_initialize,
};

// Stacksize for pthread_create

#define XHA_THREAD_STACKSIZE (1024*1024)
static pthread_attr_t pthread_attr;

#define N_INIT_FUNCS    (sizeof(init_funcs) / sizeof(init_funcs[0]))

#define main_exit(status)   exit(status_to_exit(status))

#define MAIN_SIGHANDLER_INTERVAL  (1000)


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC void
sigcatch(
    int signo);

MTC_STATIC void
post_phase_init(
    int phase);

MTC_STATIC void
main_save_scheduler(void);

MTC_STATIC void
main_log_timeouts(void);

MTC_STATIC  void *
main_sighandler(
    void *ignore);

//
//++
//
//  NAME:
//
//      Main
//
//  DESCRIPTION:
//
//      The main function of the HA daemon, invoked by ha_start_daemon script.
//
//  FORMAL PARAMETERS:
//
//      ha_daemon config-file-path
//
//  RETURN VALUE:
//
//      Success - zero
//      Failure - nonzero
//
//  ENVIRONMENT:
//
//      Called from the standard CRT.
//
//--
//

int
main(
    int argc,
    char *argv[])
{
    int lockfiled, fd, sig, init_index;
    MTC_S32 phase;
    struct rlimit rlimit;
    MTC_BOOLEAN licensed = TRUE;
    MTC_STATUS status;
    char buf[32];
    int index;
    int pthread_ret;

    //
    //  Ignore SIGHUP here.
    //  logrotate may send SIGHUP regardless of the state of xha. 
    //

    sigignore(SIGHUP);
        
    //  Initialize ha_config in BSS.
    //  (static initialization causes a warning for unknown reason)

    bzero(&ha_config, sizeof(ha_config));

    fist_initialize();

    if (argc < 2)
    {
        main_exit(MTC_ERROR_INVALID_PARAMETER);
    }

    //  #### Remember the command name used to start this process.

    daemon_vars.name = argv[0];
    daemon_vars.config_path = argv[1];

    //  #### If HA daemon is already present, exit here.

    if ((lockfiled = open(HA_DAEMON_LOCK_FILE, (O_RDWR | O_CREAT), 0640)) < 0 ||
        lockf(lockfiled, F_TLOCK, 0) < 0)
    {
        //  The lock attempt failed. There must be another HA daemon running
        //  on the local host.

        if (lockfiled >= 0)
        {
            close(lockfiled);
        }

        main_exit(MTC_ERROR_DAEMON_EXIST);
    }

    snprintf(buf, sizeof(buf) - 1, "%d\n", getpid());
    buf[sizeof(buf) - 1] = '\0';    // forces null termination
    write(lockfiled, buf, strlen(buf));

    //  #### Interpret the config-file

    status = interpret_config_file(daemon_vars.config_path, &ha_config);

    if (status != MTC_SUCCESS)
    {
        main_exit(status);
    }

    //  #### See if XenServer HA is properly licensed.

    if (Xapi_license_check() < 0)
    {
        //  Log subsystem is not opened yet
        licensed = FALSE;
    }

    //  #### Close all inherited file descriptors.

    getrlimit(RLIMIT_NOFILE, &rlimit);
    for (fd = 2; fd < rlimit.rlim_cur; fd++)
    {
        if (fd != lockfiled)
        {
            close(fd);
        }
    }

    //  Open logging

    if ((status = log_initialize()) != MTC_SUCCESS)
    {
        main_exit(status);
    }

    log_message(MTC_LOG_NOTICE, "HA daemon started - built at " BUILD_DATE " - " BUILD_ID "\n");
    main_log_timeouts();
    log_logmask();

    //  assume all remaining parameters are to enable the initial
    //  FIST points.

    for (index = 2; index < argc; index++)
    {
        fist_enable(argv[index]) == MTC_SUCCESS
            ? log_message(MTC_LOG_DEBUG, "Accepted an initial FIST point %s.\n", argv[index])
            : log_message(MTC_LOG_DEBUG, "FIST point %s is not valid.\n", argv[index]);
    }

    if (fist_on("init.license.fail"))
    {
        licensed = FALSE;
    }

    if (licensed == FALSE)
    {
        log_status(MTC_ERROR_IMPROPER_LICENSE, NULL);
        log_terminate();
        main_exit(MTC_ERROR_IMPROPER_LICENSE);
    }

    //  #### Detach this process from the current process group,
    //       not to receive any TTY-originated signals, such as SIGINT.

    setpgrp();

    //  #### Set default umask

    umask(~750);

    //  #### Attempt to ignore all signals.
    //  SIGTERM, SIGCHLD and SIGHUP will be reprogrammed later,
    //  after all threads are created. We do not want those threads
    //  to inherit sigcatch settings.

    for (sig = 1; sig < _NSIG; sig++)
    {
        sigignore(sig);
    }

    //  #### Set resource limit of core size to max value

    getrlimit(RLIMIT_CORE, &rlimit);
    rlimit.rlim_cur = rlimit.rlim_max;
    setrlimit(RLIMIT_CORE, &rlimit);

    //  #### Save scheduler policy and priority

    main_save_scheduler();

    //  #### Set stacksize for pthread_create();

    pthread_ret = pthread_attr_init(&pthread_attr);
    if (pthread_ret == 0)
    {
        pthread_ret = pthread_attr_setstacksize(&pthread_attr, XHA_THREAD_STACKSIZE);
        if (pthread_ret == 0)
        {
            // succeeded to set stacksize. use it for all pthread_create();
            xhad_pthread_attr = &pthread_attr;
        }
        else
        {
            log_message(MTC_LOG_WARNING, "pthread_attr_setstacksize failed. (%d)\n", pthread_ret);
        }
    }
    else
    {
        log_message(MTC_LOG_WARNING, "pthread_attr_init failed. (%d)\n", pthread_ret);
    }

    //  #### Initialize internal modules (phase 0 and 1)

    init_index = 0;
    for (phase = 0; phase < 2; phase++)
    {
        for (init_index = 0; init_index < N_INIT_FUNCS; init_index++)
        {
            if ((status = (*init_funcs[init_index])(phase)) != MTC_SUCCESS)
            {
                //  Uninitialize modules if an error is detected.

                while (--init_index >= 0)
                {
                    (void)(*init_funcs[init_index])(-1);
                }

                main_exit(status);
            }
        }

        post_phase_init(phase);
    }

    //  #### Initialization completed.
    //       The main thread enters idle loop.

    for (;;)
    {
        sleep(1);

        if (daemon_vars.terminate_requested)
        {
            log_message(MTC_LOG_NOTICE, "HA daemon started shutdown process.\n");

            for (init_index = N_INIT_FUNCS - 1; init_index >= 0; init_index--)
            {
                (void)(*init_funcs[init_index])(-1);
            }

            break;
        }
    }

    log_message(MTC_LOG_NOTICE, "HA daemon completed shutdown process.\n");
    log_terminate();

    close(lockfiled);

    main_exit(exit_status);

    return 0;   // stub to avoid a compiler warning
}

//
//  sigcatch -
//
//  signal handler for sigset().
//
//  sigcatch may be invoked in any thread context in any time.
//  Acquiring any lock which does not support recursive in this function causes deadlock.
//

MTC_STATIC void
sigcatch(
    int signo)
{
    sigset(signo, sigcatch);

    switch (signo)
    {
    case SIGTERM:
        // log_message(MTC_LOG_DEBUG, "HA daemon received SIGTERM.\n");
        daemon_vars.terminate_requested = 1;
        break;

    case SIGCHLD:
        // log_maskable_debug_message(TRACE, "HA daemon received SIGCHLD.\n");
        // pthread_mutex_lock(&mut_sigchld);
        // pthread_cond_broadcast(&cond_sigchld);
        // pthread_mutex_unlock(&mut_sigchld);

        // do nothing here.
        
        break;

    case SIGHUP:
        // log_message(MTC_LOG_DEBUG, "HA daemon received SIGHUP.\n");
        // log_reopen();
        daemon_vars.catch_SIGHUP = 1;
        break;

    default:
        // log_message(MTC_LOG_WARNING, "HA daemon received an unexpected signal(%d).\n", signo);
        daemon_vars.catch_unexpected_signo = signo;
        break;
    }
}

MTC_STATIC void
post_phase_init(
    int phase)
{
    MTC_S32 ret;

    if (phase == 1)
    {
        //  All the initialization is done and threads are created.

        //  create signal handler thread

        ret = pthread_create(&daemon_vars.sighandler_thread, xhad_pthread_attr, main_sighandler, NULL);
        if (ret)
        {
            log_message(MTC_LOG_WARNING, "HA daemon cannot create signal handler thread (sys %d)\n", 
                        ret);
            daemon_vars.sighandler_thread = 0;

            // set exit_status and terminate_requested.
            // The main thread will start the shutdown process.

            exit_status = MTC_ERROR_SYSTEM_LEVEL_FAILURE;
            daemon_vars.terminate_requested = 1;
            
            return;
        }

        //  Now it's time to catch signals.

        sigset(SIGTERM, sigcatch);
        sigset(SIGCHLD, sigcatch);
        sigset(SIGHUP, sigcatch);
    }
}

//
//  main_terminate -
//
//  Set terminate flag and sit in idle state.
//  The terminate flag is checked periodically by main thread
//  and it will exit.
//

void
main_terminate(
    MTC_STATUS status)
{
    daemon_vars.terminate_requested = 1;
    if (exit_status == MTC_SUCCESS)
    {
        exit_status = status;
    }
    for (;;)
    {
        sleep(60);
    }
}

//
//  main_steady_state
//
//  This function should be called by sm when the daemon has 
//  transitioned to the steady state.
//  Set process scheduling policy/priority and Lock resident memory.
//

static void move_to_root_cgroup(void) {
  FILE *f;

  f = fopen("/sys/fs/cgroup/cpu/tasks", "w");
  if (f == NULL) {
    log_message(LOG_WARNING, "Can't open cgroups tasks file for writing (sys %d)\n", errno);
    return;
  }

  if (fprintf(f, "%d\n", getpid()) <= 0) {
		log_message(LOG_WARNING, "Can't write pid into cgroups tasks file (sys %d)\n", errno);
  }

  if (fclose(f) != 0) {
		log_message(LOG_WARNING, "Can't close cgroups tasks file (sys %d)\n", errno);
  }
}

void
main_steady_state(void)
{
    struct sched_param sparam = {0};
    int sched_policy;
    int status;
    
    // set scheduler policy and priority
    sched_policy = SCHED_RR;
    sparam.sched_priority = 50;


    move_to_root_cgroup();

    status = sched_setscheduler(getpid() , sched_policy, &sparam);
    if (status == 0) 
    {
        log_message(MTC_LOG_DEBUG, "HA daemon set scheduler policy=%d priority=%d\n", 
                    sched_policy, sparam.sched_priority);
    }
    else 
    {
        log_message(MTC_LOG_WARNING, "HA daemon set scheduler failed (sys %d) status=%d policy=%d priority=%d\n", 
                    errno, 
                    status, sched_policy, sparam.sched_priority);
    }

    // mlockall
    status = mlockall(MCL_CURRENT);
    if (status == 0) 
    {
        log_message(MTC_LOG_DEBUG, "HA daemon mlockall succeeded\n");
    }
    else 
    {
        log_message(MTC_LOG_WARNING, "HA daemon mlockall failed (sys %d)\n", 
                    errno);
    }
}


static int sched_policy_save = SCHED_OTHER;
static struct sched_param sparam_save = {0};

//
//  main_save_scheduler
//
//  Save original scheduling policy and priority of the process.
//

MTC_STATIC void
main_save_scheduler(void)
{
    struct sched_param sparam = {0};
    int sched_policy;
    int status;
    
    // get current scheduler policy and priority

    sched_policy = sched_getscheduler(getpid());
    if (sched_policy < 0) 
    {
        log_message(MTC_LOG_WARNING, "HA daemon get scheduler policy failed (sys %d)\n", 
                    errno);
        return;
    }
    status = sched_getparam(getpid(), &sparam);
    if (status != 0)
    {
        log_message(MTC_LOG_WARNING, "HA daemon get scheduler param failed (sys %d)\n", 
                    errno);
        return;
    }
    sched_policy_save = sched_policy;
    sparam_save = sparam;
}

//
//  main_reset_scheduler
//
//  This function should be called by child process.
//  Reset scheduling policy and priority as original.
//

void
main_reset_scheduler(void)
{
    int status;
    
    // set scheduler policy and priority

    status = sched_setscheduler(getpid() , sched_policy_save, &sparam_save);
}

//
//  main_log_timeouts
//
//  logging configured timeout values.
//

MTC_STATIC void
main_log_timeouts(void)
{
    log_message(MTC_LOG_INFO, "CONF: my_index=%d num_host=%d t1=%d T1=%d t2=%d T2=%d Wh=%d Ws=%d Tboot=%d Tenable=%d tXapi=%d TXapi=%d RestartXapi=%d TRestartXapi=%d Tlicence=%d\n", 
               _my_index, _num_host,
               _t1, _T1,
               _t2, _T2,
               _Wh, _Ws,
               _Tboot, _Tenable, _tXapi, _TXapi, _RestartXapi, _TRestartXapi, _Tlicense);
            
}

//
// main_sleep - sleep specified time [ms]

MTC_STATIC void
main_sleep(
    MTC_CLOCK msec)
{
    struct timespec ts, ts_rem;

    ts = ts_rem = mstots(msec);
    while (nanosleep(&ts, &ts_rem)) ts = ts_rem;
}

//
//  main_sighandler
//
//  signal handler thread
//

MTC_STATIC  void *
main_sighandler(
    void *ignore)
{
    while (!daemon_vars.terminate_requested)
    {
        main_sleep (MAIN_SIGHANDLER_INTERVAL);
        if (daemon_vars.catch_SIGHUP)
        {
            log_message(MTC_LOG_DEBUG, "HA daemon received SIGHUP.\n");
            log_reopen();
            daemon_vars.catch_SIGHUP = 0;
        }

        if (daemon_vars.catch_unexpected_signo)
        {
            log_message(MTC_LOG_WARNING, "HA daemon received an unexpected signal(%d).\n", daemon_vars.catch_unexpected_signo);
            daemon_vars.catch_unexpected_signo = 0;
        }
    }
    return NULL;
}


