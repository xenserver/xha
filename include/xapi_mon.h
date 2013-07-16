//
//  MODULE: xapi_mon.h
//

#ifndef XAPI_MON_H
#define XAPI_MON_H (1)    // Set flag indicating this file was included

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
//      This is Xapi monitor header file.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: March 05, 2008
//
//  DESIGN ISSUES:
//


extern MTC_S32
xapimon_initialize(
    MTC_S32  phase);

extern MTC_S32
Xapi_license_check();

void
xm_timer_1sec();


#define XAPI_MAX_ERROR_STRING_LEN   100

//
// Xapimon
//
#define COM_ID_XAPIMON  "xapimon"

typedef struct _COM_DATA_XAPIMON {
    struct {
        MTC_BOOLEAN enable_Xapi_monitor;// Enable to send heartbeat if TRUE.
    } ctl;

    MTC_S32     latency;                // Interval [msec] between the last two
                                        // heartbeat transmitted from the local
                                        // host.
                                        // -1 if the stat is not available.

    MTC_S32     latency_max;            // Maximum interval [msec].
                                        // -1 if the stat is not available.
                                        // Query_liveset should set -1 when it
                                        // retrieves this data.

    MTC_S32     latency_min;            // Minimum interval [msec].
                                        // -1 if the stat is not available.
                                        // Query_liveset should set -1 when it
                                        // retrieves this data.

    MTC_S64     time_Xapi_restart;
                                        // Time [msec] started xapi restarting.
                                        // -1 if xapi is healthy

    MTC_CLOCK   time_healthcheck_start;
                                        // Time [msec] started xapi healthcheck.
                                        // -1 if not executing the healthcheck.

    MTC_S8      err_string[XAPI_MAX_ERROR_STRING_LEN + 1];
                                        // Xapi error string.
                                        // set if xapi healthcheck failed.

} COM_DATA_XAPIMON, *PCOM_DATA_XAPIMON;



extern pthread_mutex_t  mut_sigchld;
extern pthread_cond_t   cond_sigchld;


#endif	// XAPI_MON_H
