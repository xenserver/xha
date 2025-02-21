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

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "config.h"
#include "xapi_mon.h"
#include "com.h"
#include "sm.h"
#include "fist.h"


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

#define XAPIMON_MAX_FORK_ERR_RETRY          (5)
#define XAPIMON_WAITCHLD_POLLING_DIVIDER    (10)
#define XAPI_HEALTHCHECKER_PATH             "/opt/xensource/libexec/"
#define XAPI_HEALTHCHECKER                  "xapi-health-check"
#define XAPI_RESTARTER_PATH                 "/sbin/"
#define XAPI_RESTARTER                      "service"
#define XAPI_RESTARTER_OPTION1              "xapi"
#define XAPI_RESTARTER_OPTION2              "restart"
#define XAPI_LICENSE_CHECKER_PATH           "/opt/xensource/libexec/"
#define XAPI_LICENSE_CHECKER                "xha-lc"


static HA_COMMON_OBJECT_HANDLE xapimon_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;

static MTC_BOOLEAN          terminate = FALSE;

#define XAPIMON_CHECK_INTERVAL  (100)

//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATUS
xapi_healthcheck(
    pid_t       *ppid,
    PMTC_S32    ppip);

void
xapi_restart();

pid_t
restart_xapi();

MTC_STATIC  void *
xapimon(
    void *ignore);

MTC_STATIC  pid_t
wait_pid_timeout(
    pid_t       pid,
    PMTC_S32    pstat,
    MTC_CLOCK   start_time,
    MTC_CLOCK   timeout);

MTC_STATIC void
xm_sleep(
    MTC_CLOCK msec);

MTC_STATIC void
xm_close_descriptors();


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//



//
//++
//
//  NAME:
//
//      xapimon_initialize
//
//  DESCRIPTION:
//
//      This function initialize Xapi monitor.
//
//  FORMAL PARAMETERS:
//
//      phase
//          0   - first phase: initializes global values.
//          1   - second phase: starts Xapi monitoring thread.
//          -1  - termination phase: terminates thread.
//
//  RETURN VALUE:
//
//      Success - zero
//      Failure - nonzero
//
//  ENVIRONMENT:
//
//
//--
//

MTC_S32
xapimon_initialize(
    MTC_S32  phase)
{
    MTC_S32             ret;
    static pthread_t    xapimon_thread = 0;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "Xapimon: xapimon_initialize(0).\n");

        {
            COM_DATA_XAPIMON    xapimon = {
                .ctl.enable_Xapi_monitor = FALSE,
                .latency = -1,
                .latency_max = -1,
                .latency_min = -1,
                .time_Xapi_restart = -1,
                .time_healthcheck_start = -1,
                .err_string = ""};

            // create common object
            ret = com_create(COM_ID_XAPIMON,
                             &xapimon_object, sizeof(COM_DATA_XAPIMON), &xapimon);
            if (ret != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR,
                    "Xapimon: cannot create COM object. (%d)\n", ret);
                xapimon_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
                goto error;
            }
        }
        break;

    case 1:
        log_message(MTC_LOG_INFO, "Xapimon: xapimon_initialize(1).\n");

        // start heartbeat thread
        terminate = FALSE;

        ret = pthread_create(&xapimon_thread, xhad_pthread_attr, xapimon, NULL);
        if (ret)
        {
            log_internal(MTC_LOG_ERR,
                "Xapimon: cannot create Xapimon thread. (%d)\n", ret);
            xapimon_thread = 0;
            ret = MTC_ERROR_XAPIMON_PTHREAD;
            goto error;
        }
        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "Xapimon: xapimon_initialize(-1).\n");

#if 0
        if (xapimon_thread)
        {
            terminate = TRUE;

            ret = pthread_join(xapimon_thread, NULL);
            if (ret)
            {
                log_message(MTC_LOG_WARNING,
                    "Xapimon: cannot terminate Xapimon thread. (%d)\n", ret);
                ret = pthread_kill(xapimon_thread, SIGKILL);
                if (ret)
                {
                    log_internal(MTC_LOG_ERR,
                        "Xapimon: cannot kill Xapimon thread. (%d)\n", ret);
                    ret = MTC_ERROR_XAPIMON_PTHREAD;
                    goto error;
                }
            }
        }

        com_close(xapimon_object);
        xapimon_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
#endif

        break;
    }

    return 0;

error:
    return ret;
}


//
//++
//
//  NAME:
//
//      xapimon
//
//  DESCRIPTION:
//
//      Xapi monitoring thread
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
//--
//

MTC_STATIC  void *
xapimon(
    void *ignore)
{
    MTC_BOOLEAN         enable_Xapi_monitor;
    PCOM_DATA_XAPIMON   pxapimon;
    MTC_CLOCK           start, now, target_delay;
    MTC_S32             xapimon_pipe, fork_errcnt = 0, nread;
    pid_t               xapimon_pid;
    siginfo_t           info;
    MTC_S8              err_string[XAPI_MAX_ERROR_STRING_LEN + 1],
                        readbuf[XAPI_MAX_ERROR_STRING_LEN + 1];
    PMTC_S8             perr_string = err_string;
    MTC_STATUS          status;

    log_message(MTC_LOG_INFO, "Xapimon: thread ID: %ld.\n", syscall(SYS_gettid));
    while (!terminate)
    {
        log_maskable_debug_message(TRACE, "Xapimon: Xapi monitor thread activity log.\n");

        xm_sleep(_tXapi * ONE_SEC);

        com_reader_lock(xapimon_object, (void **) &pxapimon);
        enable_Xapi_monitor = pxapimon->ctl.enable_Xapi_monitor;
        com_reader_unlock(xapimon_object);
        if (!enable_Xapi_monitor)
        {
            continue;
        }

        start = now = _getms();

        // do xapi healthcheck
        status = xapi_healthcheck(&xapimon_pid, &xapimon_pipe);
        if (status != MTC_SUCCESS)
        {
            // healthcheck process failed to start
            if (++fork_errcnt > XAPIMON_MAX_FORK_ERR_RETRY)
            {
                self_fence(MTC_ERROR_XAPIMON_XAPI_FAILED,
                    "Xapimon: cannot start Xapi healthcheck process.  - Self-Fence");
                break;
            }
            continue;
        }
        else
        {
            fork_errcnt = 0;
        }

        //
        // record start time
        //

        com_writer_lock(xapimon_object, (void **) &pxapimon);
        pxapimon->time_healthcheck_start = start;
        com_writer_unlock(xapimon_object);


        // read pipe while waiting.
        // ( Dom0 Linux's pipe size is 8 * 512 bytes )
        // 
        // when child process terminates
        //    select returns 1
        //    read returns 0 (EOF)
        //    breaks this loop immediately
        //

        perr_string = err_string;
        do
        {
            fd_set fds;
            MTC_S32 nfds = -1;
            struct timeval tv;

            FD_ZERO(&fds);
            FD_SET(xapimon_pipe, &fds);
            nfds = _max(nfds, xapimon_pipe);

            tv = mstotv((_TXapi * ONE_SEC - (now - start) < 0) ? 0: _TXapi * 1000 - (now - start));
            if (select(nfds + 1, &fds, NULL, NULL, &tv) > 0)
            {
                // try to read error string
                if ((nread = read(xapimon_pipe, readbuf, sizeof(readbuf))) > 0)
                {
                    if (perr_string - err_string < XAPI_MAX_ERROR_STRING_LEN)
                    {
                        memcpy(perr_string, readbuf,
                            _min(nread,
                                 XAPI_MAX_ERROR_STRING_LEN - (perr_string - err_string)));
                        perr_string += _min(nread,
                                 XAPI_MAX_ERROR_STRING_LEN - (perr_string - err_string));
                        *(perr_string) = '\0';
                    }
                }
                else 
                {
                    // EOF or read error
                    *(perr_string) = '\0';
                    close(xapimon_pipe);
                    break;
                }
            }
            else 
            {
                // select timeout or error
                *(perr_string) = '\0';
                close(xapimon_pipe);
                break;
            }
            // recalc timeout and select retry
            now = _getms();
        } while (1);


        // wait pid
        info.si_pid = 0;
        while (waitid(P_PID, xapimon_pid, &info, WEXITED | WNOHANG) == 0 &&
               info.si_pid != xapimon_pid)
        {
            info.si_pid = 0;
            xm_sleep(XAPIMON_CHECK_INTERVAL);
            if (_getms() - start > _TXapi * ONE_SEC)
            {
                // Xapi healthcheck is hang.
                kill(xapimon_pid, SIGKILL);
                break;
            }
        }

        // check fist points
        target_delay = 0;
        if (fist_on("xm.time.<TXapi"))
        {
            target_delay = _TXapi * ONE_SEC * 3 / 4;
        }
        else if (fist_on("xm.time.=TXapi"))
        {
            target_delay = _TXapi * ONE_SEC;
        }
        else if (fist_on("xm.time.>TXapi"))
        {
            target_delay = 2 * _TXapi * ONE_SEC;
            info.si_pid = 0;
        }
        else if (fist_on("xm.xapi.error"))
        {
            target_delay = 0;
            info.si_status = -1;
            strcpy(err_string, "XM(FIST): xm.xapi.error is inserted");
        }
        if (target_delay != 0)
        {
            log_message(MTC_LOG_DEBUG,
                        "XM(FIST): xapi healthcheck delay is %d ms\n", target_delay);
            xm_sleep(target_delay - (_getms() - start));
        }

        // see if xapi is healthy
        if (info.si_pid == xapimon_pid &&
            (info.si_code == CLD_EXITED && info.si_status == 0))
        {
            // xapi is healthy, calculate and store diagnostic values
            now = _getms();

            com_writer_lock(xapimon_object, (void **) &pxapimon);
            pxapimon->latency = now - start;
            pxapimon->latency_max =
                (pxapimon->latency_max < 0)? pxapimon->latency:
                    _max(pxapimon->latency_max, pxapimon->latency);
            pxapimon->latency_min =
                (pxapimon->latency_min < 0)? pxapimon->latency:
                    _min(pxapimon->latency_min, pxapimon->latency);
            memset(pxapimon->err_string, 0, sizeof(pxapimon->err_string));
            pxapimon->time_healthcheck_start = -1;
            com_writer_unlock(xapimon_object);
        }
        else
        {
            // xapi is not healthy.
            com_writer_lock(xapimon_object, (void **) &pxapimon);
            strncpy(pxapimon->err_string, err_string,
                    sizeof(pxapimon->err_string));
            pxapimon->time_healthcheck_start = -1;
            com_writer_unlock(xapimon_object);

            // log error info
            if (info.si_pid != xapimon_pid)
            {
                log_message(MTC_LOG_WARNING,
                            "Xapimon: Xapi healthchecker (pid=%d) is hung. - [%s]\n",
                            xapimon_pid, err_string);
            }
            else if (info.si_code == CLD_EXITED)
            {
                log_message(MTC_LOG_WARNING,
                            "Xapimon: Xapi healthchecker has reported an error (%d). - [%s]\n",
                            info.si_status, err_string);
            }
            else
            {
                log_message(MTC_LOG_WARNING,
                            "Xapimon: Xapi healthchecker is killed. - [%s]\n",
                            err_string);
            }

            xapi_restart();
        }
    }

    return NULL;
}


//
//++
//
//  NAME:
//
//      xapi_healthcheck
//
//  DESCRIPTION:
//
//      This function start Xapi healthcheck script.  While starting child process,
//      this function create a pipe, and pass one end to the child as stdout, and
//      return the other end as function return value.
//
//
//  FORMAL PARAMETERS:
//
//      ppid:   process id of child process is stored in *ppid
//      ppip:   file descriptor of one end of pipe that is connected to stdout of
//              the child process is stored in *ppip
//
//
//  RETURN VALUE:
//
//      MTC_SUCCESS: if success
//
//
//  ENVIRONMENT:
//
//
//--
//

MTC_STATUS
xapi_healthcheck(
    pid_t       *ppid,
    PMTC_S32    ppip)
{
    MTC_S32 pip[2];

    if (pipe(pip) < 0)
    {
        log_internal(MTC_LOG_ERR, "Xapimon: cannot create pipe. (%d)\n", errno);
        return MTC_ERROR_XAPIMON_PIPE;
    }

    if ((*ppid = fork()) == 0)
    {
        dup2(pip[1], STDOUT_FILENO);
        close(pip[0]);
        close(pip[1]);

        //  Close all other descriptors.
        xm_close_descriptors();

        // Reset scheduling policy and priority.
        main_reset_scheduler();

        execl(XAPI_HEALTHCHECKER_PATH XAPI_HEALTHCHECKER,
              XAPI_HEALTHCHECKER, (char *) NULL);
        _exit(-1);
    }

    if (*ppid < 0)
    {
        log_internal(MTC_LOG_ERR,
            "Xapimon: cannot start xapi healthchecker. (%d)\n", errno);
        close(pip[0]);
        close(pip[1]);
        return MTC_ERROR_XAPIMON_FORK;
    }
    else
    {
        close(pip[1]);
        *ppip = pip[0];
        return MTC_SUCCESS;
    }
}


//
//++
//
//  NAME:
//
//      xapi_restart
//
//  DESCRIPTION:
//
//      This function try to restart Xapi
//
//
//  FORMAL PARAMETERS:
//
//
//
//  RETURN VALUE:
//
//
//
//  ENVIRONMENT:
//
//
//--
//

void
xapi_restart()
{
    MTC_CLOCK           time_restarting, now;
    static MTC_CLOCK    time_restarted = -ONE_DAY;
    static MTC_S32      restart_count = 0;
    MTC_S32             status;
    pid_t               xapirestarter_pid, ret_pid;
    PCOM_DATA_XAPIMON   pxapimon;

    // restart timestamp
    now = _getms();
    com_writer_lock(xapimon_object, (void **) &pxapimon);
    pxapimon->time_Xapi_restart = time_restarting = now;
    com_writer_unlock(xapimon_object);

    if (time_restarted < 0 || _getms() - time_restarted > ONE_DAY)
    {
        restart_count = 0;
    }

    log_message(MTC_LOG_INFO, "Xapimon: restarting Xapi.\n");
    while (restart_count < _RestartXapi)
    {
        restart_count++;

        xapirestarter_pid = restart_xapi();
        if (xapirestarter_pid < 0)
        {
            // failed to start Xapi
            continue;
        }


        // wait for timeout or process termination  (by polling)
        // TBD - should wakeup soon after xapi restart process is terminated?
        ret_pid = wait_pid_timeout(xapirestarter_pid, &status,
                                   time_restarting, _TRestartXapi * ONE_SEC);


        // see if xapi restart process exited & get exit status
        if (ret_pid == xapirestarter_pid)
        {
            switch (status)
            {
            case 0:
                com_writer_lock(xapimon_object, (void **) &pxapimon);
                pxapimon->time_Xapi_restart = -1;
                com_writer_unlock(xapimon_object);
                log_message(MTC_LOG_INFO, "Xapimon: Xapi restarted.\n");
                time_restarted = _getms();

                return;

            default:
                log_message(MTC_LOG_WARNING,
                            "Xapimon: Xapi restart failed (try=%d). (%d)\n",
                            restart_count, status);
                break;
            }
        }
        else
        {
            // time out
            log_message(MTC_LOG_WARNING,
                        "Xapimon: Xapi restart process (pid=%d) is hung (try=%d).\n",
                        xapirestarter_pid, restart_count);
            kill(xapirestarter_pid, SIGKILL);
        }
    }

    log_message(MTC_LOG_INFO, "Xapimon: Xapi failed.  Self Fence.\n");
    log_message(MTC_LOG_INFO, "Xapimon: Sleep t1 * 2 to notify the other hosts.\n");
    sleep(_t1 * 2);
    self_fence(MTC_ERROR_XAPIMON_XAPI_FAILED, "Xapimon: Xapi failed.  - Self-Fence");
    assert(FALSE);

    return;
}


//
//++
//
//  NAME:
//
//      restart_xapi
//
//  DESCRIPTION:
//
//      This function try to start Xapi start script
//
//
//  FORMAL PARAMETERS:
//
//
//
//  RETURN VALUE:
//
//      Process ID of Xapi starter process
//
//
//  ENVIRONMENT:
//
//
//--
//

pid_t
restart_xapi()
{
    pid_t   pid;

    if ((pid = fork()) == 0)
    {
        //  Close all descriptors.
        xm_close_descriptors();

        // Reset scheduling policy and priority.
        main_reset_scheduler();

        if (execlp(XAPI_RESTARTER,
                   XAPI_RESTARTER, XAPI_RESTARTER_OPTION1, XAPI_RESTARTER_OPTION2,
                   (char *) NULL) < 0)
        {
            switch (errno)
            {
            case ENOENT:
                execl(XAPI_RESTARTER_PATH XAPI_RESTARTER,
                      XAPI_RESTARTER, XAPI_RESTARTER_OPTION1, XAPI_RESTARTER_OPTION2,
                      (char *) NULL);
                break;
            default:
                break;
            }
        }

        _exit(-1);
    }

    if (pid < 0)
    {
        log_message(MTC_LOG_WARNING,
            "Xapimon: cannot fork xapi restart process. (%d)\n", errno);
    }

    return pid;
}


//
//++
//
//  NAME:
//
//      Xapi_license_check
//
//  DESCRIPTION:
//
//      This function checks if HA is licensed
//
//
//  FORMAL PARAMETERS:
//
//
//
//  RETURN VALUE:
//
//      0   - HA is licensed
//      <0  - HA is not licensed
//
//
//  ENVIRONMENT:
//
//
//--
//

MTC_S32
Xapi_license_check()
{
    pid_t           pid, ret_pid;
    MTC_CLOCK       time_start_license_check;
    MTC_S32         status, ret;

    // start license checker
    if ((pid = fork()) == 0)
    {
        //  Close all descriptors.
        xm_close_descriptors();

        // Reset scheduling policy and priority.
        main_reset_scheduler();

        execl(XAPI_LICENSE_CHECKER_PATH XAPI_LICENSE_CHECKER,
              XAPI_LICENSE_CHECKER, (char *) NULL);
        _exit(-1);
    }

    if (pid < 0)
    {
        log_message(MTC_LOG_WARNING,
            "Xapimon: cannot fork xapi license checker. (%d)\n", errno);
        ret = -1;
    }
    else
    {
        // wait for process termination or timeout
        time_start_license_check = _getms();
        // TBD
        ret_pid = wait_pid_timeout(pid, &status,
                                   time_start_license_check, _Tlicense * ONE_SEC);
        if (ret_pid == pid)
        {
            switch (status)
            {
            case 0:
                ret = 0;
                break;

            default:
                log_message(MTC_LOG_WARNING,
                            "Xapimon: Xapi license checker failed. (%d)\n", status);
                ret = -1;
                break;
            }
        }
        else
        {
            // timeout
            log_message(MTC_LOG_WARNING,
                        "Xapimon: Xapi license checker (pid=%d) is fung.\n", pid);
            kill(pid, SIGKILL);
            ret = -1;
        }
    }

    return ret;
}


//
//
//  Return value
//      return process id 'pid' if the process successfully terminated
//      return 0 if timeout
//

MTC_STATIC pid_t
wait_pid_timeout(
    pid_t       pid,
    PMTC_S32    pstat,
    MTC_CLOCK   start_time,
    MTC_CLOCK   timeout)
{
    pid_t           ret_pid = 0;
    MTC_CLOCK       now = start_time;
    struct timespec sleep_ts, sleep_rem;

    sleep_ts = mstots(timeout / XAPIMON_WAITCHLD_POLLING_DIVIDER);
    while ((ret_pid = waitpid(pid, pstat, WNOHANG)) == 0 &&
           now < start_time + timeout)
    {
        ret_pid = 0;
        nanosleep(&sleep_ts, &sleep_rem);
        now = _getms();
    }

    assert(ret_pid == pid || ret_pid == 0);

    return ret_pid;
}

//
// xm_sleep - sleep specified time [ms]

MTC_STATIC void
xm_sleep(
    MTC_CLOCK msec)
{
    struct timespec ts, ts_rem;

    ts = ts_rem = mstots(msec);
    while (nanosleep(&ts, &ts_rem)) ts = ts_rem;
}


//
//   xm_close_descriptors

MTC_STATIC void
xm_close_descriptors()
{
    struct rlimit   rlimit;
    int             fd;

    getrlimit(RLIMIT_NOFILE, &rlimit);
    for (fd = 2; fd < rlimit.rlim_cur; fd++)
    {
        close(fd);
    }
}
