//
//  MODULE: sm.h
//

#ifndef SM_H
#define SM_H (1)    // Set flag indicating this file was included

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
//      This is state manager header file.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: March 05, 2008
//
//  DESIGN ISSUES:
//


#include "xha.h"
#include "xapi_mon.h"

typedef enum {
    FENCING_NONE,
    FENCING_ARMED,
    FENCING_DISARM_REQUESTED,
    FENCING_DISARMED,
} MTC_FENCING_MODE, *PMTC_FENCING_MODE;

//
//  Utility macro to handle per-host bitmap

#define MTC_HOSTMAP_UNIT            (sizeof(MTC_U32) * 8)

typedef MTC_U32 MTC_HOSTMAP[_rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT)];

#define _element(hostnum)       ((hostnum) / MTC_HOSTMAP_UNIT)
#define _bitloc(hostnum)        (1 << ((hostnum) % MTC_HOSTMAP_UNIT))

#define MTC_HOSTMAP_SET(b, hostnum)  ((b)[_element(hostnum)] |=  _bitloc(hostnum))
#define MTC_HOSTMAP_RESET(b, hostnum)((b)[_element(hostnum)] &= ~_bitloc(hostnum))
#define MTC_HOSTMAP_ISON(b, hostnum) (((b)[_element(hostnum)] &  _bitloc(hostnum)) != 0)
#define MTC_HOSTMAP_SET_BOOLEAN(b, hostnum, boolean) \
( \
    (boolean) ? MTC_HOSTMAP_SET(b, hostnum) \
              : MTC_HOSTMAP_RESET(b, hostnum) \
)
#define MTC_HOSTMAP_INIT_RESET(b) ({ \
    assert(sizeof(b) == sizeof(MTC_HOSTMAP)); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        (b)[i] = 0; \
    } \
    0; \
})

#define MTC_HOSTMAP_INIT_SET(b) ({ \
    assert(sizeof(b) == sizeof(MTC_HOSTMAP)); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        (b)[i] = ~0; \
    } \
    ~0; \
})

#define MTC_HOSTMAP_COPY(dst, src) ( \
    assert(sizeof(dst) == sizeof(MTC_HOSTMAP) && sizeof(src) == sizeof(MTC_HOSTMAP)), \
    memcpy((dst), (src), sizeof(MTC_HOSTMAP)) \
)

#define MTC_HOSTMAP_COMPARE(b1, OP, b2) ({ \
    assert(OP == '!=' || OP == '<>' || OP == '><'); \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP)); \
    int i, ret = 0; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        if ((b1)[i] - (b2)[i]) \
        { \
            ret = 1; \
            break; \
        } \
    } \
    ret; \
})

#define MTC_HOSTMAP_SUBSETEQUAL(b1, OP, b2) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP)); \
    assert(OP == '(=' || OP == '<='); \
    int i, ret = 1; \
    for (i = 0; i < MAX_HOST_NUM; i++) \
    { \
        if (MTC_HOSTMAP_ISON(b1, i) && !MTC_HOSTMAP_ISON(b2, i)) \
        { \
            ret = 0; \
            break; \
        } \
    } \
    ret; \
})

#define MTC_HOSTMAP_DIFFERENCE(b1, EQ, b2, OP, b3) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP) \
                                             && sizeof(b3) == sizeof(MTC_HOSTMAP)); \
    assert(EQ == '=' || EQ == ':=' || EQ == '<-'); \
    assert(OP == '-'); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        b1[i] = b2[i] & ~b3[i]; \
    } \
    b1; \
})

#define MTC_HOSTMAP_UNION(b1, EQ, b2, OP, b3) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP) \
                                             && sizeof(b3) == sizeof(MTC_HOSTMAP)); \
    assert(EQ == '=' || EQ == ':=' || EQ == '<-'); \
    assert(OP == '|' || EQ == 'OR' || EQ == '+' || \
            EQ == 'u' || EQ == 'U' || EQ == 'v' || EQ == 'V'); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        b1[i] = b2[i] | b3[i]; \
    } \
    b1; \
})

#define MTC_HOSTMAP_INTERSECTION(b1, EQ, b2, OP, b3) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP) \
                                             && sizeof(b3) == sizeof(MTC_HOSTMAP)); \
    assert(EQ == '=' || EQ == ':=' || EQ == '<-'); \
    assert(OP == '&' || EQ == 'AND' || EQ == '+' || EQ == '^'); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        b1[i] = b2[i] & b3[i]; \
    } \
    b1; \
})

#define MTC_HOSTMAP_COMPLEMENT(b1, EQ, OP, b2) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP) && sizeof(b2) == sizeof(MTC_HOSTMAP)); \
    assert(EQ == '=' || EQ == ':=' || EQ == '<-'); \
    assert(OP == '~' || EQ == 'NOT' || EQ == '!'); \
    int i; \
    for (i = 0; i < _rounddiv(MAX_HOST_NUM, MTC_HOSTMAP_UNIT); i++) \
    { \
        b1[i] = ~(b2[i]); \
    } \
    b1; \
})

#define MTC_HOSTMAP_MASK_UNCONFIG(b1) ({ \
    assert(sizeof(b1) == sizeof(MTC_HOSTMAP)); \
    int i; \
    for (i = 0; i < MAX_HOST_NUM; i++) \
    { \
        if (!_is_configured_host(i)) \
        { \
            MTC_HOSTMAP_RESET(b1, i); \
        } \
    } \
    b1; \
})



//
// common data structure for HB, SF, SM
//
typedef struct _RAW_DATA {
    MTC_HOSTMAP current_liveset;        // Current liveset as seen by this host
    MTC_HOSTMAP proposed_liveset;       // Proposed liveset by this host
    MTC_HOSTMAP hbdomain;               // Current host set on the heartbeat
    MTC_HOSTMAP sfdomain;               // Current host set on the state file

    MTC_S32     time_since_last_HB_receipt[MAX_HOST_NUM];
                                        // Set of time [msec] since last HB
                                        // receipt, received from node x.

    MTC_S32     time_since_last_SF_update[MAX_HOST_NUM];
                                        // Set of time [msec] since last SF
                                        // update, received from node x.

    MTC_S32     time_since_xapi_restart;
                                        // Set of time [msec] since xapi
                                        // restart received from node x.
} RAW_DATA, *PRAW_DATA;


//
// Status Manager
//
#define COM_ID_SM   "statemanager"

typedef enum {
    SM_PHASE_STARTING,  // 0
    SM_PHASE_STARTED,   // 1
    SM_PHASE_ABORTED,   // 2
    SM_PHASE_FHREADY,   // 3
    SM_PHASE_FH1,       // 4
    SM_PHASE_FH1DONE,   // 5
    SM_PHASE_FH2,       // 6
    SM_PHASE_FH2DONE,   // 7
    SM_PHASE_FH3,       // 8
    SM_PHASE_FH3DONE,   // 9
    SM_PHASE_FH4,       // 10
    SM_PHASE_FH4DONE,   // 11
} SM_PHASE, *PSM_PHASE;

typedef struct _COM_DATA_SM {
    MTC_HOSTMAP         current_liveset;
    MTC_HOSTMAP         proposed_liveset;
//    MTC_HOSTMAP         excluded;
    MTC_FENCING_MODE    fencing;
    MTC_STATUS          status;
    SM_PHASE            phase;
    MTC_BOOLEAN         SR2;

    MTC_HOSTMAP         sf_access;
    MTC_HOSTMAP         sf_corrupted;

    MTC_BOOLEAN         hb_approaching_timeout;
    MTC_BOOLEAN         sf_approaching_timeout;
    MTC_BOOLEAN         xapi_approaching_timeout;

    MTC_U32             weight;             //  Latest weight specified by ha_set_host_weight
    MTC_U32             commited_weight;    //  Commited weight of the local host
} COM_DATA_SM, *PCOM_DATA_SM;


//
// Heartbeat
//
#define COM_ID_HB   "heartbeat"

typedef struct _COM_DATA_HB {
    struct {
        MTC_BOOLEAN enable_HB_send;     // Enable to send heartbeat if TRUE.

        MTC_BOOLEAN enable_HB_receive;  // Discard all received heartbeat if FALSE.
                                        // HB will always receive hearbeat.
        MTC_BOOLEAN join;
        MTC_BOOLEAN fence_request;
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

    MTC_S64     time_last_HB[MAX_HOST_NUM];
                                        // Time [msec] last received from node x.

    MTC_HOSTMAP hbdomain;               // ON if the host looks active on the heartbeat
    MTC_HOSTMAP notjoining;             // Bit-on if the corresponding host is NOT ready to join.

    RAW_DATA    raw[MAX_HOST_NUM];      // Raw data from node x.
                                        // raw[local_host] shows the last data
                                        // transmitted from the local host.

    MTC_S8      err_string[MAX_HOST_NUM][XAPI_MAX_ERROR_STRING_LEN + 1];
                                        // Xapi error string.
                                        // set if xapi healthcheck failed.
    

    MTC_FENCING_MODE    fencing;        // Fencing mode (NULL->ARMED->DISARM_REQUESTED->DISARMED)
    MTC_BOOLEAN         SF_accelerate;  // 1 if SF acceleration requested
    SM_PHASE            sm_phase[MAX_HOST_NUM];
                                        //
} COM_DATA_HB, *PCOM_DATA_HB;


//
// StateFile
//
#define COM_ID_SF   "statefile"

typedef struct _COM_DATA_SF
{
    MTC_U32 modified_mask;                  // Fields designated by the mask have been
                                            // updated on memory (in this structure),
                                            // but not written to the State-File.
                                            // modified_mask is only for internal use by SF.
#define SF_MODIFIED_MASK_NONE           (0)
#define SF_MODIFIED_MASK_POOL_STATE     (1 << 0)
#define SF_MODIFIED_MASK_MASTER         (1 << 1)
#define SF_MODIFIED_MASK_EXCLUDED       (1 << 2)

    //  Set by the State Manager (sm)
    //  Referenced by the State-File handler (sf)
    
    struct {
        MTC_BOOLEAN enable_SF_write;
        MTC_BOOLEAN enable_SF_read;
        MTC_BOOLEAN starting;
    } ctl;

    //  Set and refrenced by the State-File handler (sf)
    //  and by the lock manager (lm)

    struct {
        MTC_BOOLEAN request;                // TRUE if this host has requested a lock
        MTC_HOSTMAP grant;                  // MTC_HOSTMAP_ISON(grant, Y) is TRUE
                                            // if this host has granted a lock to the host Y
    } lm[MAX_HOST_NUM];

    MTC_U32     pool_state;                 // Pool state as written in the SF global section.
    MTC_UUID    master;                     // Master's host UUID as written in the SF global section.
    MTC_BOOLEAN SF_access;                  // TRUE if if the local host has access to the SF
    MTC_BOOLEAN SF_corrupted;               // TRUE if SF corruption is detected. SF_access turns to FALSE as well.
    MTC_S64     time_last_SF[MAX_HOST_NUM]; // Milliseconds since the last update to the State-File
                                            // was seen from each host.
    MTC_HOSTMAP excluded;                   // Bit-on if the corresponding host has been excluded.
    MTC_HOSTMAP starting;                   // Bit-on if the corresponding host is starting.
    RAW_DATA    raw[MAX_HOST_NUM];          // Array of raw data as seen by each host

    MTC_S32 latency;                        // State-Fie access latency in ms (latest)
    MTC_S32 latency_max;                    // State-Fie access latency in ms (max since the last query_liveset)
    MTC_S32 latency_min;                    // State-Fie access latency in ms (min since the last query_liveset)

    MTC_HOSTMAP sfdomain;                   // ON if the host looks active on the State File
    MTC_FENCING_MODE fencing;               // Fencing mode (NULL->ARMED->DISARM_REQUESTED->DISARMED)

    SM_PHASE    sm_phase[MAX_HOST_NUM];     // Current SM phase of each host
    MTC_U32     weight[MAX_HOST_NUM];       // Weight committed by each host
} COM_DATA_SF, *PCOM_DATA_SF;

extern void
start_fh(
    MTC_BOOLEAN force);

extern MTC_STATUS
sm_initialize(
    MTC_S32 phase);

extern void
self_fence(MTC_STATUS code, PMTC_S8 message);

extern MTC_BOOLEAN
sm_get_join_block();

#endif  // SM_H
