//
//  MODULE: statefile.h
//

#ifndef STATEFILE_H
#define STATEFILE_H (1)		// Set flag indicating this file was included

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
//      This header file defines the intrinsic data types used by Marathon 
//      components. This header file must stand alone, and must not use any 
//      Linux specific definitions from other header files.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: March 14
//
//  DESIGN ISSUES:
//
//  PORTABILITY ISSUES:
//

#include "config.h"

//
//  State-File format version 2 constants
//

#define SF_VERSION              2
#define LENGTH_GLOBAL           4096
#define LENGTH_HOST_SPECIFIC    4096

//
//  Implementation specific constants
//

#define IOUNIT          512     // as required by open(2) for O_DIRECT
#define IOALIGN         4096

//  sig and sig_inv

#define sf_create_sig(sig)              (sig)
#define sf_create_inverted_sig(sig)     ((~(sig)) >> 3)

//
//  State-File structure (global section)
//

#define SIG_SF_GLOBAL           'loop'
#define SIG_END_MARKER_GLOBAL   'pdne'

typedef struct _SF_GLOBAL_SECTION {
    union {
        struct _sf_global {
            MTC_U32     sig;
            MTC_U32     sig_inv;
            MTC_U32     checksum;
            MTC_U32     version;                //  version number of this State-File format (1)
            MTC_U32     length_global;          //  length of the global section in byte (4096)
            MTC_U32     length_host_specfic;    //  length of each host specific element (4096)
            MTC_U32     max_hosts;              //  maximum number of supported hosts (64)
            MTC_UUID    gen_uuid;               //  generation UUID of the pool configuration
            MTC_U32     pool_state;             //  HA pool state (init, active, invalid)
            MTC_UUID    master;                 //  master's host UUID
            MTC_U32     config_hosts;           //  number of configured hosts
/*
            HA_CONFIG_COMMON config;            //  reformatted config info as of the last
                                                //  "ha_set_pool_state init"
 */
            MTC_U32     end_marker;             //  checksum end marker
        } data;
        char pad[LENGTH_GLOBAL];
    };
} SF_GLOBAL_SECTION, *PSF_GLOBAL_SECTION;

MTC_ASSERT_SIZE(sizeof(struct _sf_global) <= LENGTH_GLOBAL);

//  pool_state

#define SF_STATE_NONE       0
#define SF_STATE_INIT       1
#define SF_STATE_ACTIVE     2
#define SF_STATE_INVALID    3

//
//  State-File structure (host-specific element)
//

#define SIG_SF_HOST             'tsoh'
#define SIG_END_MARKER_HOST     'hdne'

typedef struct _SF_HOST_SPECIFIC_SECTION {
    union {
        struct _sf_host_specific {
            MTC_U32     sig;
            MTC_U32     sig_inv;
            MTC_U32     checksum;
            MTC_U32     sequence;               //  Sequence number of the last update
            MTC_U32     host_index;             //  Index of this host
            MTC_UUID    host_uuid;              //  UUID of this host
            MTC_U32     excluded;               //  1 if this host is excluded
            MTC_U32     starting;               //  1 if this host is in boot
            MTC_S32     since_last_hb_receipt[MAX_HOST_NUM];
                                                //  The elapsed time in milliseconds since this host (which this host-specific
                                                //  element is dedicated to) received a heartbeat from the host designated by the array
                                                //  index (i.e. host-index). A value of -1 indicates the host indexed by the host
                                                //  index has reached T1 timeout.
            MTC_S32     since_last_sf_update[MAX_HOST_NUM];
                                                //  The elapsed time in milliseconds since this host (which this host-specific
                                                //  element is dedicated to) noticed a SF update from the host designated by
                                                //  the array index (i.e. host-index). A value of -1 indicates the host indexed
                                                //  by the host index has reached T2 timeout.

            MTC_S32     since_xapi_restart_first_attempted;
                                                //  The elapsed time in milliseconds since this host (which this host-specific
                                                //  element is dedicated to) started attempting to restart Xapi. Time for retries
                                                //  is accumulated. -1 if this host is not currently attempting to restart Xapi.
            MTC_HOSTMAP current_liveset;
            MTC_HOSTMAP proposed_liveset;
            MTC_HOSTMAP hbdomain;
            MTC_HOSTMAP sfdomain;
            MTC_U32     lock_request;
            MTC_HOSTMAP lock_grant;
            SM_PHASE    sm_phase;               //  Current SM phase of this host
            MTC_U32     weight;                 //  Importance weight of this host
            MTC_U32     end_marker;             //  checksum end marker
        } data;
        char pad[LENGTH_HOST_SPECIFIC];
    };
} SF_HOST_SPECIFIC_SECTION, *PSF_HOST_SPECIFIC_SECTION;

MTC_ASSERT_SIZE(sizeof(struct _sf_host_specific) <= LENGTH_HOST_SPECIFIC);

//
//  Entire State-File
//

typedef struct _STATE_FILE {
    SF_GLOBAL_SECTION           global;
    SF_HOST_SPECIFIC_SECTION    host[MAX_HOST_NUM];
} STATE_FILE, *PSTATE_FILE;

extern MTC_S32
sf_initialize(
    MTC_S32  phase);

extern int
sf_open(
    char *path);

extern int
sf_close(
    int desc);

extern MTC_STATUS
sf_readglobal(
    int desc,
    PSF_GLOBAL_SECTION pglobal,
    MTC_UUID expected_uuid);

extern MTC_STATUS
sf_readhostspecific(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost);

extern MTC_STATUS
sf_writeglobal(
    int desc,
    PSF_GLOBAL_SECTION pglobal);

extern MTC_STATUS
sf_writehostspecific(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost);

extern MTC_STATUS
sf_read(
    int desc,
    char *buffer,
    int length,
    off_t offset);

extern MTC_STATUS
sf_write(
    int desc,
    char *buffer,
    int length,
    off_t offset);

extern MTC_U32
sf_checksum(
    MTC_U32 *p,
    MTC_U32 *end);

void
sf_reportlatency(
    MTC_CLOCK latency,
    MTC_BOOLEAN write);

extern MTC_STATUS
sf_set_pool_state(
    MTC_U32 pool_state);

extern MTC_STATUS
sf_set_master(
    MTC_UUID master);

extern MTC_STATUS
sf_set_excluded(
    MTC_BOOLEAN excluded);

void
sf_accelerate();

void
sf_cancel_acceleration();

void
sf_watchdog_set();

void
sf_sleep(
    MTC_U32 msec);

#endif  // STATEFILE_H

