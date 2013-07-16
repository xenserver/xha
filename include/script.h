//  MODULE: script.h

#ifndef SCRIPT_H
#define SCRIPT_H (1)	// Set flag indicating this file was included

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
//      This header file containts 
//         socketname
//         data structures
//      between scripts and script service
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 12, 2008
//   


#include "mtctypes.h"
#include "config.h"
#include "xapi_mon.h"
#include "hostweight.h"

////
//
//
//  D E F I N I T I O N S
//
//
////


//
//
// SOCKET_NAME
//
//


#define SCRIPT_SOCKET_NUM         3
#define SCRIPT_SOCK_NAME_BASE     "xha_socket_"
#define SCRIPT_SOCK_NAME_ARRAY    { \
                                    SCRIPT_SOCK_NAME_BASE "0", \
                                    SCRIPT_SOCK_NAME_BASE "1", \
                                    SCRIPT_SOCK_NAME_BASE "2"  \
                                  }

#define SCRIPT_SOCK_INDEX_FOR_OTHER 0
#define SCRIPT_SOCK_INDEX_FOR_QUERY 1
#define SCRIPT_SOCK_INDEX_FOR_INTERNAL 2


//
//
// SCRIPT_TYPE
//
//

enum {
    SCRIPT_TYPE_BASE = 0,
    SCRIPT_TYPE_QUERY_LIVESET,
    SCRIPT_TYPE_PROPOSE_MASTER,
    SCRIPT_TYPE_DISARM_FENCING,
    SCRIPT_TYPE_SET_POOL_STATE,
    SCRIPT_TYPE_SET_EXCLUDED,
    SCRIPT_TYPE_CLEAR_EXCLUDED,
    SCRIPT_TYPE_PID,
    SCRIPT_TYPE_HOSTSTATE,
    SCRIPT_TYPE_CANCEL_MASTER,
    SCRIPT_TYPE_GETLOGMASK,
    SCRIPT_TYPE_SETLOGMASK,
    SCRIPT_TYPE_RESETLOGMASK,
    SCRIPT_TYPE_DUMPCOM,
    SCRIPT_TYPE_BUILDID,
    SCRIPT_TYPE_PRIVATELOG,
    SCRIPT_TYPE_FIST,
    SCRIPT_TYPE_RELOAD_HOST_WEIGHT,
    SCRIPT_TYPE_NUM
};


//
//
// SCRIPT_MAGIC
//
//

#define SCRIPT_MAGIC 0xFAFBFCFD

//
//
// STATUS
//
//

#define LIVESET_STATUS_STARTING 0
#define LIVESET_STATUS_ONLINE   1

//
//
// BUILD_ID
//
//

#define BUILD_DATE_LEN 64
#define BUILD_ID_LEN 64

//
// privatelog command
//

#define PRIVATELOG_CMD_NONE 0
#define PRIVATELOG_CMD_ENABLE 1
#define PRIVATELOG_CMD_DISABLE 2

//
// fist
//

#define MAX_FIST_NAME_LEN 64


//
// script_socket_table;
//

typedef struct script_socket_table {
    MTC_U32 type;
    MTC_U32 socket_index;
}   SCRIPT_SOCKET_TABLE;

typedef MTC_S32 (*SCRIPT_SERVICE_FUNC)(MTC_U32, void *, MTC_U32 *, void *);

typedef struct script_func_table {
    MTC_U32 type;
    SCRIPT_SERVICE_FUNC func;
}   SCRIPT_FUNC_TABLE;

//
// script_socket_table
//


# define SCRIPT_SOCKET_TABLE_INITIALIZER {                              \
        {SCRIPT_TYPE_BASE, 0xffff},                                     \
        {SCRIPT_TYPE_QUERY_LIVESET, SCRIPT_SOCK_INDEX_FOR_QUERY},       \
        {SCRIPT_TYPE_PROPOSE_MASTER,SCRIPT_SOCK_INDEX_FOR_OTHER},       \
        {SCRIPT_TYPE_DISARM_FENCING,SCRIPT_SOCK_INDEX_FOR_OTHER},       \
        {SCRIPT_TYPE_SET_POOL_STATE,SCRIPT_SOCK_INDEX_FOR_OTHER},       \
        {SCRIPT_TYPE_SET_EXCLUDED,SCRIPT_SOCK_INDEX_FOR_OTHER},         \
        {SCRIPT_TYPE_CLEAR_EXCLUDED,SCRIPT_SOCK_INDEX_FOR_OTHER},       \
        {SCRIPT_TYPE_PID, SCRIPT_SOCK_INDEX_FOR_INTERNAL},              \
        {SCRIPT_TYPE_HOSTSTATE, SCRIPT_SOCK_INDEX_FOR_QUERY},           \
        {SCRIPT_TYPE_CANCEL_MASTER, SCRIPT_SOCK_INDEX_FOR_OTHER},       \
        {SCRIPT_TYPE_GETLOGMASK, SCRIPT_SOCK_INDEX_FOR_INTERNAL},       \
        {SCRIPT_TYPE_SETLOGMASK, SCRIPT_SOCK_INDEX_FOR_INTERNAL},       \
        {SCRIPT_TYPE_RESETLOGMASK, SCRIPT_SOCK_INDEX_FOR_INTERNAL},     \
        {SCRIPT_TYPE_DUMPCOM, SCRIPT_SOCK_INDEX_FOR_INTERNAL},          \
        {SCRIPT_TYPE_BUILDID, SCRIPT_SOCK_INDEX_FOR_INTERNAL},          \
        {SCRIPT_TYPE_PRIVATELOG, SCRIPT_SOCK_INDEX_FOR_INTERNAL},       \
        {SCRIPT_TYPE_FIST, SCRIPT_SOCK_INDEX_FOR_INTERNAL},             \
        {SCRIPT_TYPE_RELOAD_HOST_WEIGHT, SCRIPT_SOCK_INDEX_FOR_INTERNAL},  \
        {0, 0}}

# define SCRIPT_FUNC_TABLE_INITIALIZER {                                \
        {SCRIPT_TYPE_BASE, NULL},                                       \
        {SCRIPT_TYPE_QUERY_LIVESET, script_service_do_query_liveset},   \
        {SCRIPT_TYPE_PROPOSE_MASTER,script_service_do_propose_master},  \
        {SCRIPT_TYPE_DISARM_FENCING,script_service_do_disarm_fencing},  \
        {SCRIPT_TYPE_SET_POOL_STATE,script_service_do_set_pool_state},  \
        {SCRIPT_TYPE_SET_EXCLUDED,script_service_do_set_excluded},      \
        {SCRIPT_TYPE_CLEAR_EXCLUDED,script_service_do_clear_excluded},  \
        {SCRIPT_TYPE_PID, script_service_do_pid},                       \
        {SCRIPT_TYPE_HOSTSTATE, script_service_do_hoststate},           \
        {SCRIPT_TYPE_CANCEL_MASTER, script_service_do_cancel_master},   \
        {SCRIPT_TYPE_GETLOGMASK, script_service_do_getlogmask},         \
        {SCRIPT_TYPE_SETLOGMASK, script_service_do_setlogmask},         \
        {SCRIPT_TYPE_RESETLOGMASK, script_service_do_resetlogmask},     \
        {SCRIPT_TYPE_DUMPCOM, script_service_do_dumpcom},               \
        {SCRIPT_TYPE_BUILDID, script_service_do_buildid},               \
        {SCRIPT_TYPE_PRIVATELOG, script_service_do_privatelog},         \
        {SCRIPT_TYPE_FIST, script_service_do_fist},                     \
        {SCRIPT_TYPE_RELOAD_HOST_WEIGHT, script_service_do_reload_host_weight}, \
        {0, NULL}}

//
// data_structure
//

typedef struct script_data_head {
    MTC_U32 magic;
    MTC_U32 response;   // request:0 response:1
    MTC_U32 type;
    MTC_U32 length;
} SCRIPT_DATA_HEAD;


typedef struct query_live_set_host_info {
    MTC_S8 host_id[MTC_UUID_SIZE];
    MTC_U32 liveness;
    MTC_U32 master;
    MTC_U32 sf_access;
    MTC_U32 sf_corrupted;
    MTC_U32 excluded;
    MTC_U32 time_since_last_update_on_sf;
    MTC_S32 time_since_last_hb;
    MTC_U32 time_since_xapi_restart;
    MTC_U32 hb_list_on_hb[MAX_HOST_NUM];
    MTC_U32 sf_list_on_hb[MAX_HOST_NUM];
    MTC_U32 hb_list_on_sf[MAX_HOST_NUM];
    MTC_U32 sf_list_on_sf[MAX_HOST_NUM];
    MTC_S8  xapi_err_string[XAPI_MAX_ERROR_STRING_LEN + 1];
}   QUERY_LIVESET_HOST_INFO;

typedef struct script_data_response_query_live_set {
    MTC_U32 status;
    MTC_U32 hostnum;
    MTC_U32 localhost_index;
    MTC_S32 sf_latency;
    MTC_S32 sf_latency_max;
    MTC_S32 sf_latency_min;
    MTC_S32 hb_latency;
    MTC_S32 hb_latency_max;
    MTC_S32 hb_latency_min;
    MTC_S32 xapi_latency;
    MTC_S32 xapi_latency_max;
    MTC_S32 xapi_latency_min;
    MTC_U32 T1;
    MTC_U32 T2;
    MTC_U32 T3;
    MTC_U32 Wh;
    MTC_U32 Ws;
    MTC_U32 sf_lost;
    MTC_U32 hb_approaching_timeout;
    MTC_U32 sf_approaching_timeout;
    MTC_U32 xapi_approaching_timeout;
    MTC_U32 bonding_error;
    QUERY_LIVESET_HOST_INFO host[MAX_HOST_NUM];
} SCRIPT_DATA_RESPONSE_QUERY_LIVESET;

typedef struct script_data_response_propose_master {
    MTC_U32 retval;
    MTC_U32 accepted_as_master;
} SCRIPT_DATA_RESPONSE_PROPOSE_MASTER;

typedef struct script_data_response_pid {
    MTC_S32 pid;
} SCRIPT_DATA_RESPONSE_PID;

typedef struct script_data_response_hoststate {
    MTC_U32 status;
} SCRIPT_DATA_RESPONSE_HOSTSTATE;

typedef struct script_data_response_disarm_fencing {
    MTC_U32 retval;
} SCRIPT_DATA_RESPONSE_RETVAL_ONLY;

typedef struct script_data_request_set_pool_state {
    MTC_U32 state;
} SCRIPT_DATA_REQUEST_SET_POOL_STATE;

typedef struct script_data_logmask {
    MTC_U32 logmask;
}   SCRIPT_DATA_REQUEST_SETLOGMASK, 
    SCRIPT_DATA_REQUEST_RESETLOGMASK,
    SCRIPT_DATA_RESPONSE_GETLOGMASK;

typedef struct script_data_request_privatelog {
    MTC_U32 cmd;
}   SCRIPT_DATA_REQUEST_PRIVATELOG;

typedef struct script_data_response_privatelog {
    MTC_U32 privatelogflag;
}   SCRIPT_DATA_RESPONSE_PRIVATELOG;

typedef struct script_data_response_buildid {
    MTC_S8 build_date[BUILD_DATE_LEN];
    MTC_S8 build_id[BUILD_ID_LEN];
} SCRIPT_DATA_RESPONSE_BUILDID;


typedef struct script_data_request_dumpcom {
    MTC_U32 dumpflag;
} SCRIPT_DATA_REQUEST_DUMPCOM;

typedef struct script_data_request_fist {
    MTC_S8 name[MAX_FIST_NAME_LEN];
    MTC_BOOLEAN set;
} SCRIPT_DATA_REQUEST_FIST;

typedef struct script_data_request {
    SCRIPT_DATA_HEAD head;
    union {
        SCRIPT_DATA_REQUEST_SET_POOL_STATE request_set_pool_state;
        SCRIPT_DATA_REQUEST_SETLOGMASK     request_setlogmask;
        SCRIPT_DATA_REQUEST_RESETLOGMASK   request_resetlogmask;
        SCRIPT_DATA_REQUEST_DUMPCOM        request_dumpcom;
        SCRIPT_DATA_REQUEST_FIST           request_fist;
    } body;
} SCRIPT_DATA_REQUEST;

typedef struct script_data_response {
    SCRIPT_DATA_HEAD head;
    union {
        SCRIPT_DATA_RESPONSE_QUERY_LIVESET  query_live_set;
        SCRIPT_DATA_RESPONSE_PROPOSE_MASTER propose_master;
        SCRIPT_DATA_RESPONSE_PID            pid;
        SCRIPT_DATA_RESPONSE_HOSTSTATE      hoststate;
        SCRIPT_DATA_RESPONSE_RETVAL_ONLY    retval_only;
        SCRIPT_DATA_RESPONSE_GETLOGMASK     getlogmask;
        SCRIPT_DATA_RESPONSE_BUILDID        buildid;
    } body;
} SCRIPT_DATA_RESPONSE;

//
//
//  NAME:
//
//      script_initialize
//
//  DESCRIPTION:
//
//      script service initialize/start/terminate
//
//  FORMAL PARAMETERS:
//
//      phase - 0: initialize
//              1: start
//             -1: terminate
//          
//  RETURN VALUE:
//
//      0 - success
//      not 0 - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_S32 
script_initialize(
    MTC_S32 phase);

//
//
//  NAME:
//
//      script_service_do_query_liveset
//      script_service_do_propose_master
//      script_service_do_disarm_fencing
//      script_service_do_set_pool_state
//      script_service_do_set_excluded
//      script_service_do_clear_excluded
//      script_service_do_pid
//      script_service_do_hoststate
//      script_service_do_dumpcom
//      script_service_do_buildid
//
//  DESCRIPTION:
//
//      query_liveset service function
//
//  FORMAL PARAMETERS:
//
//      req_len - length of request
//      req_body - buffer of request
//      res_len - length of response
//      res_body - buffer of response
//          
//  RETURN VALUE:
//
//      0 - success
//      not 0 - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATUS
script_service_do_query_liveset(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);


MTC_STATUS
script_service_do_propose_master(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_disarm_fencing(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_set_pool_state(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_set_excluded(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_clear_excluded(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_pid(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_hoststate(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_cancel_master(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_getlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_setlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_resetlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_dumpcom(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_buildid(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_privatelog(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_fist(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

MTC_STATUS
script_service_do_reload_host_weight(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body);

//
//
//  NAME:
//
//      script_service_check_after_set_excluded();
//
//  DESCRIPTION:
//
//      check set_excluded called and succeeded.
//      this function should be called after write response to client.
//      If set_excluded is succeeded, the daemon shall exit without any
//      terminate process.
//      The host will be fenced by watchdog.
//
//  FORMAL PARAMETERS:
//
//      type - type of script (IN)
//      res_len - length of response buffer (IN)
//      res_body - body of response buffer  (IN)
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      dom0
//
//

void
script_service_check_after_set_excluded(
    MTC_U32 type,
    MTC_U32 res_len,
    void *res_body);

#endif // SCRIPT_H
