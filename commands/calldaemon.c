//  MODULE: calldaemon.c

//
//
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
//      Xen HA command call to daemon.
//        query_liveset
//        propose_master
//        disarm_fencing
//
//        pid
//
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 18, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <assert.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "script.h"
#include "sm.h"
#include "statefile.h"
#include "hostweight.h"

//
//
//  S T A T I C   F U N C T I O N   P R O T O T Y P E S
//
//


static MTC_STATUS
print_query_liveset(
    MTC_U32 size,
    void *param);

static MTC_STATUS
print_propose_master(
    MTC_U32 size,
    void *param);

static MTC_STATUS
print_pid(
    MTC_U32 size,
    void *param);

static MTC_STATUS
print_hoststate(
    MTC_U32 size,
    void *param);

static MTC_STATUS
return_retval(
    MTC_U32 size,
    void *param);


static MTC_STATUS
print_logmask(
    MTC_U32 size,
    void *param);

static MTC_STATUS
req_set_pool_state(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf);

static MTC_STATUS
req_set_logmask(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf);

static MTC_STATUS
req_set_dumpcom(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf);


static MTC_STATUS
print_buildid(
    MTC_U32 size,
    void *param);

static MTC_STATUS
req_privatelog(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf);

static MTC_STATUS
print_privatelog(
    MTC_U32 size,
    void *param);

static MTC_STATUS
req_fist(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf);


//
//
//  L O C A L   D E F I N I T I O N S
//
//

#define VALID_CHARS "\t !\"#$%'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQQSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

typedef MTC_STATUS (*REQUEST_FUNC)(int argc, char **argv, MTC_U32 *len, void *buf);
typedef MTC_STATUS (*RESPONSE_FUNC)(MTC_U32, void *);

typedef struct cmd_info 
{
    MTC_U32 type;
    MTC_S8  *name;
    REQUEST_FUNC req_func;
    RESPONSE_FUNC res_func;
} CMD_INFO;

CMD_INFO cmd_info_list[] = {
    {SCRIPT_TYPE_QUERY_LIVESET,  "query_liveset",  NULL, print_query_liveset},
    {SCRIPT_TYPE_PROPOSE_MASTER, "propose_master", NULL, print_propose_master},
    {SCRIPT_TYPE_DISARM_FENCING, "disarm_fencing", NULL, return_retval},
    {SCRIPT_TYPE_SET_POOL_STATE, "set_pool_state", req_set_pool_state, return_retval},
    {SCRIPT_TYPE_SET_EXCLUDED,   "set_excluded",   NULL, return_retval},
    {SCRIPT_TYPE_CLEAR_EXCLUDED, "clear_excluded", NULL, return_retval},
    {SCRIPT_TYPE_PID,            "pid",            NULL, print_pid},
    {SCRIPT_TYPE_HOSTSTATE,      "gethoststate",   NULL, print_hoststate},

    {SCRIPT_TYPE_CANCEL_MASTER,  "cancel_master",  NULL, return_retval},
    {SCRIPT_TYPE_GETLOGMASK,     "getlogmask",     NULL, print_logmask},
    {SCRIPT_TYPE_SETLOGMASK,     "setlogmask",     req_set_logmask, print_logmask},
    {SCRIPT_TYPE_RESETLOGMASK,   "resetlogmask",   req_set_logmask, print_logmask},
    {SCRIPT_TYPE_DUMPCOM,        "dumpcom",        req_set_dumpcom, return_retval},
    {SCRIPT_TYPE_BUILDID,        "buildid",        NULL, print_buildid},
    {SCRIPT_TYPE_PRIVATELOG,     "privatelog",     req_privatelog, print_privatelog},
    {SCRIPT_TYPE_FIST,           "fist",           req_fist, return_retval},
    {SCRIPT_TYPE_RELOAD_HOST_WEIGHT,  "reload_host_weight",  NULL, return_retval},

    {0, NULL, NULL, NULL}};

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//



////
//
//
//  I N T E R N A L   F U N C T I O N
//
//
////

//
//
//  NAME:
//
//      type_to_socket_index();
//
//  DESCRIPTION:
//
//      get socket_index from script_type;
//
//  FORMAL PARAMETERS:
//
//      type - script_type
//          
//  RETURN VALUE:
//
//      socket_index
//
//  ENVIRONMENT:
//
//      dom0
//
//

static MTC_STATUS
type_to_socket_index(MTC_U32 type)
{
    SCRIPT_SOCKET_TABLE s_table[] = SCRIPT_SOCKET_TABLE_INITIALIZER;
    MTC_U32 i;
    for (i = 0; i < SCRIPT_TYPE_NUM; i++) 
    {
        if (type == s_table[i].type) 
        {
            return s_table[i].socket_index;
        }
    }
    assert(FALSE);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_call_daemon();
//
//  DESCRIPTION:
//
//      call ha daemon 
//
//  FORMAL PARAMETERS:
//
//      type - script type (IN)
//      req_len - length of request buffer (IN)
//      req_body - body of request buffer  (IN)
//      res_len - length of response buffer (IN/OUT)
//      res_body - body of response buffer  (OUT)
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

static MTC_STATUS
script_call_daemon(
    MTC_U32 type,
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    int message_socket;
    struct sockaddr_un message_socket_addr;
    char sockname[sizeof(((struct sockaddr_un *)0)->sun_path)] = "";
    char *sockname_list[] = SCRIPT_SOCK_NAME_ARRAY;
    MTC_U32 socket_index = 0;
    SCRIPT_DATA_HEAD head;

    int size, read_num, write_num, ret;
    char *bufptr;

    //
    // choose socket index for QUERY_LIVESET/OTHER
    //

    socket_index = type_to_socket_index(type);

    //
    // set abstract name to message_socket_addr
    //

    memset(sockname, 0, sizeof(sockname));
    memcpy(sockname+1, sockname_list[socket_index], strlen(sockname_list[socket_index]));

    message_socket_addr.sun_family = PF_UNIX;
    memset(message_socket_addr.sun_path, '\0', 
           sizeof(message_socket_addr.sun_path));
    memcpy(message_socket_addr.sun_path, sockname, sizeof(sockname));


    //
    // create socket and connect
    //

    if ((message_socket = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) 
    {
        return MTC_ERROR_SC_SOCKET;
    }
    if (connect(message_socket, (struct sockaddr *)&message_socket_addr, sizeof(message_socket_addr)) < 0) 
    {
        return MTC_ERROR_SC_CONNECT_TO_DAEMON;
    }

    //
    // head
    //

    head.magic = SCRIPT_MAGIC;
    head.response = 0;
    head.type = type;
    head.length = req_len;


    //
    // write request head
    //

    size = sizeof(head);
    write_num = 0;
    bufptr = (char *)&head;

    while (size - write_num > 0) 
    {

        // TODO: select before read

        if ((ret = write(message_socket, bufptr + write_num, size - write_num)) <= 0) 
        {
            // EOF or fail
            
            // log
            
            close (message_socket);
            return MTC_ERROR_SC_WRITE_ERROR;
        }
        write_num += ret;
    }
    
    //
    // write request body
    //

    size = req_len;
    write_num = 0;
    bufptr = (char *)req_body;

    while (size - write_num > 0) 
    {

        // TODO: select before read

        if ((ret = write(message_socket, bufptr + write_num, size - write_num)) <= 0) 
        {
            // EOF or fail
            
            // log
            
            close (message_socket);
            return MTC_ERROR_SC_WRITE_ERROR;
        }
        write_num += ret;
    }

    //
    // read response head
    //

    size = sizeof(head);
    read_num = 0;
    bufptr = (char *)&head;

    while (size - read_num > 0) 
    {

        // TODO: select before read

        if ((ret = read(message_socket, bufptr + read_num, size - read_num)) <= 0) 
        {
            // EOF or fail

            // log

            close (message_socket);
            return MTC_ERROR_SC_READ_ERROR;
        }
        read_num += ret;
    }

    //
    // check bufsize
    //

    if (*res_len < head.length) 
    {
        *res_len = head.length;
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    *res_len = head.length;

    //
    // read response body
    //

    size = head.length;
    read_num = 0;
    bufptr = (char *)res_body;

    while (size - read_num > 0) 
    {

        // TODO: select before read
        
        if ((ret = read(message_socket, bufptr + read_num, size - read_num)) <= 0) 
        {
            // EOF or fail

            // log

            close (message_socket);
            return MTC_ERROR_SC_READ_ERROR;
        }
        read_num += ret;
    }

    close (message_socket);

    return MTC_SUCCESS; // SUCCESS
}


//
//
//  NAME:
//
//      cleanup_string
//
//  DESCRIPTION:
//
//      copy string only VALID_CHARS.
//
//  FORMAL PARAMETERS:
//
//      dst - output string
//      src - imput string
//      len - max length
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

void 
cleanup_string(
    MTC_S8 *dst, 
    MTC_S8 *src, 
    MTC_U32 len)
{
    MTC_U32 c_index;
    MTC_S8 *dstptr;

    dstptr = dst;
    for (c_index = 0; c_index < len; c_index++)
    {
        if (strchr(VALID_CHARS, src[c_index]))
        {
            *dstptr = src[c_index];
            dstptr ++;
        }
        if (src[c_index] == '\0')
        {
            break;
        }
    }
    *dstptr = '\0';
}




//
//
//  NAME:
//
//      print_query_liveset
//
//  DESCRIPTION:
//
//      print the result of query liveset in xml format to stdout.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_QUERY_LIVESET
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_query_liveset(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_QUERY_LIVESET *l;
    MTC_U32 h_index, h_active_index;
    MTC_S8 err_string[XAPI_MAX_ERROR_STRING_LEN + 1];

    l = (SCRIPT_DATA_RESPONSE_QUERY_LIVESET *) param;

    if (size < sizeof(SCRIPT_DATA_RESPONSE_QUERY_LIVESET)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }


    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<ha_liveset_info version=\"1.0\">\n");
    printf("  <status>%s</status>\n", (l->status == LIVESET_STATUS_ONLINE)?"Online":"Starting");
    printf("  <localhost>\n");
    printf("    <HostID>%.8s-%.4s-%.4s-%.4s-%.12s</HostID>\n", 
           l->host[l->localhost_index].host_id,
           l->host[l->localhost_index].host_id + 8,
           l->host[l->localhost_index].host_id + 12,
           l->host[l->localhost_index].host_id + 16,
           l->host[l->localhost_index].host_id + 20);
    printf("    <HostIndex>%d</HostIndex>\n", l->localhost_index);
    printf("  </localhost>\n");
    if (l->status == LIVESET_STATUS_ONLINE) 
    {
        for (h_index = 0; h_index < l->hostnum; h_index++)
        {
            printf("  <host>\n");
            printf("    <HostID>%.8s-%.4s-%.4s-%.4s-%.12s</HostID>\n", 
                   l->host[h_index].host_id,
                   l->host[h_index].host_id + 8,
                   l->host[h_index].host_id + 12,
                   l->host[h_index].host_id + 16,
                   l->host[h_index].host_id + 20);
            printf("    <HostIndex>%d</HostIndex>\n", h_index);
            printf("    <liveness>%s</liveness>\n", 
                   (l->host[h_index].liveness)?"TRUE":"FALSE");
            printf("    <master>%s</master>\n", 
                   (l->host[h_index].master)?"TRUE":"FALSE");
            printf("    <statefile_access>%s</statefile_access>\n", 
                   (l->host[h_index].sf_access)?"TRUE":"FALSE");
            printf("    <statefile_corrupted>%s</statefile_corrupted>\n", 
                   (l->host[h_index].sf_corrupted)?"TRUE":"FALSE");
            printf("    <excluded>%s</excluded>\n", 
                   (l->host[h_index].excluded)?"TRUE":"FALSE");
            printf("  </host>\n");
        }
        printf("  <raw_status_on_local_host>\n");
        printf("    <statefile_latency>%d</statefile_latency>\n", l->sf_latency);
        printf("    <statefile_latency_max>%d</statefile_latency_max>\n", l->sf_latency_max);
        printf("    <statefile_latency_min>%d</statefile_latency_min>\n", l->sf_latency_min);
        printf("    <heartbeat_latency>%d</heartbeat_latency>\n", l->hb_latency);
        printf("    <heartbeat_latency_max>%d</heartbeat_latency_max>\n", l->hb_latency_max);
        printf("    <heartbeat_latency_min>%d</heartbeat_latency_min>\n", l->hb_latency_min);
        printf("    <Xapi_healthcheck_latency>%d</Xapi_healthcheck_latency>\n", l->xapi_latency);
        printf("    <Xapi_healthcheck_latency_max>%d</Xapi_healthcheck_latency_max>\n", l->xapi_latency_max);
        printf("    <Xapi_healthcheck_latency_min>%d</Xapi_healthcheck_latency_min>\n", l->xapi_latency_min);

        for (h_index = 0; h_index < l->hostnum; h_index++)
        {
            printf("    <host_raw_data>\n");
            printf("      <HostID>%.8s-%.4s-%.4s-%.4s-%.12s</HostID>\n", 
                   l->host[h_index].host_id,
                   l->host[h_index].host_id + 8,
                   l->host[h_index].host_id + 12,
                   l->host[h_index].host_id + 16,
                   l->host[h_index].host_id + 20);
            printf("      <HostIndex>%d</HostIndex>\n", h_index);
            printf("      <time_since_last_update_on_statefile>%d</time_since_last_update_on_statefile>\n", 
                   l->host[h_index].time_since_last_update_on_sf);
            printf("      <time_since_last_heartbeat>%d</time_since_last_heartbeat>\n",
                   l->host[h_index].time_since_last_hb);
                   
            printf("      <time_since_xapi_restart_first_attempted>%d</time_since_xapi_restart_first_attempted>\n", 
                   l->host[h_index].time_since_xapi_restart);
            
            cleanup_string(err_string, l->host[h_index].xapi_err_string, XAPI_MAX_ERROR_STRING_LEN);
            printf("      <xapi_error_string>%s</xapi_error_string>\n", 
                   err_string);

            printf("      <heartbeat_active_list_on_heartbeat>\n");
            for (h_active_index = 0; h_active_index < l->hostnum; h_active_index++)
            {
                if (l->host[h_index].hb_list_on_hb[h_active_index])
                {
                    printf("          %.8s-%.4s-%.4s-%.4s-%.12s\n", 
                           l->host[h_active_index].host_id,
                           l->host[h_active_index].host_id + 8,
                           l->host[h_active_index].host_id + 12,
                           l->host[h_active_index].host_id + 16,
                           l->host[h_active_index].host_id + 20);
                }
            }
            printf("      </heartbeat_active_list_on_heartbeat>\n");
            printf("      <heartbeat_active_list_on_statefile>\n");
            for (h_active_index = 0; h_active_index < l->hostnum; h_active_index++)
            {
                if (l->host[h_index].hb_list_on_sf[h_active_index])
                {
                    printf("          %.8s-%.4s-%.4s-%.4s-%.12s\n", 
                           l->host[h_active_index].host_id,
                           l->host[h_active_index].host_id + 8,
                           l->host[h_active_index].host_id + 12,
                           l->host[h_active_index].host_id + 16,
                           l->host[h_active_index].host_id + 20);
                }
            }
            printf("      </heartbeat_active_list_on_statefile>\n");
            printf("      <statefile_active_list_on_heartbeat>\n");
            for (h_active_index = 0; h_active_index < l->hostnum; h_active_index++)
            {
                if (l->host[h_index].sf_list_on_hb[h_active_index])
                {
                    printf("          %.8s-%.4s-%.4s-%.4s-%.12s\n", 
                           l->host[h_active_index].host_id,
                           l->host[h_active_index].host_id + 8,
                           l->host[h_active_index].host_id + 12,
                           l->host[h_active_index].host_id + 16,
                           l->host[h_active_index].host_id + 20);
                }
            }
            printf("      </statefile_active_list_on_heartbeat>\n");
            printf("      <statefile_active_list_on_statefile>\n");
            for (h_active_index = 0; h_active_index < l->hostnum; h_active_index++)
            {
                if (l->host[h_index].sf_list_on_sf[h_active_index])
                {
                    printf("          %.8s-%.4s-%.4s-%.4s-%.12s\n", 
                           l->host[h_active_index].host_id,
                           l->host[h_active_index].host_id + 8,
                           l->host[h_active_index].host_id + 12,
                           l->host[h_active_index].host_id + 16,
                           l->host[h_active_index].host_id + 20);
                }
            }
            printf("      </statefile_active_list_on_statefile>\n");
            printf("    </host_raw_data>\n");
        }
        printf("  </raw_status_on_local_host>\n");

        printf("  <timeout>\n");
        printf("    <T1>%d</T1>\n", l->T1);
        printf("    <T2>%d</T2>\n", l->T2);
        printf("    <T3>%d</T3>\n", l->T3);
        printf("    <Wh>%d</Wh>\n", l->Wh);
        printf("    <Ws>%d</Ws>\n", l->Ws);
        printf("  </timeout>\n");
        
        printf("  <warning_on_local_host>\n");
        printf("    <statefile_lost>%s</statefile_lost>\n", (l->sf_lost)?"TRUE":"FALSE");
        printf("    <heartbeat_approaching_timeout>%s</heartbeat_approaching_timeout>\n", (l->hb_approaching_timeout)?"TRUE":"FALSE");
        printf("    <statefile_approaching_timeout>%s</statefile_approaching_timeout>\n", (l->sf_approaching_timeout)?"TRUE":"FALSE");
        printf("    <Xapi_healthcheck_approaching_timeout>%s</Xapi_healthcheck_approaching_timeout>\n", (l->xapi_approaching_timeout)?"TRUE":"FALSE");
        printf("    <network_bonding_error>%s</network_bonding_error>\n", (l->bonding_error)?"TRUE":"FALSE");
        printf("  </warning_on_local_host>\n");
    }
    printf("</ha_liveset_info>\n");
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      print_propose_master
//
//  DESCRIPTION:
//
//      print the result of propose_master to stdout.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_PROPOSE_MASTER
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_propose_master(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_PROPOSE_MASTER *p;

    p = (SCRIPT_DATA_RESPONSE_PROPOSE_MASTER *) param;

    if (size < sizeof(SCRIPT_DATA_RESPONSE_PROPOSE_MASTER)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }

    if (p->retval == MTC_SUCCESS) 
    {
        printf("%s\n", (p->accepted_as_master)?"TRUE":"FALSE");
    }
    return p->retval;
}

//
//
//  NAME:
//
//      print_pid
//
//  DESCRIPTION:
//
//      print the pid of daemon to stdout.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_PID
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_pid(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_PID *p;

    if (size < sizeof(SCRIPT_DATA_RESPONSE_PID)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    p = (SCRIPT_DATA_RESPONSE_PID *) param;
    printf("%d\n", p->pid);
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      print_status
//
//  DESCRIPTION:
//
//      print the result of status to stdout.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_STATUS
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_hoststate(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_HOSTSTATE *l;

    l = (SCRIPT_DATA_RESPONSE_HOSTSTATE *) param;

    if (size < sizeof(SCRIPT_DATA_RESPONSE_HOSTSTATE)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }

    printf("%s\n", (l->status == LIVESET_STATUS_ONLINE)?"Online":"Starting");
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      print_logmask
//
//  DESCRIPTION:
//
//      print the result of getlogmask to stdout.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_STATUS
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_logmask(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_GETLOGMASK *l;
    MTC_U32 logmask_index;
    char *logmask_name[LOG_MASK_BITS] = LOG_MASK_NAMES;

    l = (SCRIPT_DATA_RESPONSE_GETLOGMASK *) param;

    if (size < sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }

    printf("logmask = %x\n", l->logmask);
    for (logmask_index = 0; logmask_index < LOG_MASK_BITS; logmask_index++)
    {
        MTC_S32 logmask_on;

        logmask_on = (1 << (logmask_index + LOG_MASK_BASE)) & l->logmask;
        if (logmask_name[logmask_index] == NULL && logmask_on == 0) 
        {
            continue;
        }
        printf("%s:%2.2d(%s)\n", 
               (logmask_on == 0)?"OFF":"ON ",
               logmask_index + LOG_MASK_BASE,
               (logmask_name[logmask_index] != NULL)?logmask_name[logmask_index]:"UNKNOWN_MASK");
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      print_buildid
//
//  DESCRIPTION:
//
//      print the build identification information of xhad.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_BUILDID
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_buildid(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_BUILDID *b;
    b = (SCRIPT_DATA_RESPONSE_BUILDID *) param;

    printf("build information\n");
    printf("  date:%s\n", b->build_date);
    printf("  id:%s\n", b->build_id);
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      return_retval
//
//  DESCRIPTION:
//
//      extract retval from response.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_RETVAL_ONLY
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
return_retval(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;
    
    if (size < sizeof(SCRIPT_DATA_RESPONSE_PID)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }

    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY *) param;
    return r->retval;
}

//
//
//  NAME:
//
//      req_set_pool_state
//
//  DESCRIPTION:
//
//      set request data for set_pool_state
//
//  FORMAL PARAMETERS:
//
//      argc - number of argument from command line except argv[0] and argv[1]
//      argv - pointer of arguments except argv[0] and argv[1]
//      len - in: buffer length
//            out: actual length of request data
//      buf - buffer for request data
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//


static MTC_STATUS
req_set_pool_state(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf)
{
    SCRIPT_DATA_REQUEST_SET_POOL_STATE *s = buf;

    if (argc != 1) {
        return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
    }
    if (*len < sizeof(SCRIPT_DATA_REQUEST_SET_POOL_STATE)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    if (!strcmp(argv[0], "init"))
    {
        s->state = SF_STATE_INIT;
    }
    else if (!strcmp(argv[0], "active"))
    {
        s->state = SF_STATE_ACTIVE;
    }
    else if (!strcmp(argv[0], "invalid"))
    {
        s->state = SF_STATE_INVALID;
    }
    else 
    {
        return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      req_set_logmask
//
//  DESCRIPTION:
//
//      set request data for setlogmask/resetlogmask
//
//  FORMAL PARAMETERS:
//
//      argc - number of argument from command line except argv[0] and argv[1]
//      argv - pointer of arguments except argv[0] and argv[1]
//      len - in: buffer length
//            out: actual length of request data
//      buf - buffer for request data
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//


static MTC_STATUS
req_set_logmask(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf)
{
    SCRIPT_DATA_REQUEST_SETLOGMASK *l = buf;
    MTC_U32 arg_index;
    MTC_U32 logmask_index;
    char *logmask_name[LOG_MASK_BITS] = LOG_MASK_NAMES;

    if (*len < sizeof(SCRIPT_DATA_REQUEST_SETLOGMASK)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    l->logmask = 0;

    for (arg_index = 0; arg_index < argc; arg_index++)
    {
        if (isdigit(argv[arg_index][0])) 
        {
            logmask_index = atoi(argv[arg_index]);
            if (logmask_index < LOG_MASK_BASE ||
                logmask_index - LOG_MASK_BASE >= LOG_MASK_BITS)
            {
                return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
            }
        }
        else if (!strcmp("all", argv[arg_index]))
        {
            for (logmask_index = 0; logmask_index < LOG_MASK_BITS; logmask_index++)
            {
                l->logmask |= (1<<(logmask_index + LOG_MASK_BASE));
            }
        }
        else
        {
            for (logmask_index = 0; logmask_index < LOG_MASK_BITS; logmask_index++)
            {
                if (logmask_name[logmask_index] == NULL) 
                {
                    return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
                }
                if (!strcmp(logmask_name[logmask_index], argv[arg_index]))
                {
                    break;
                }
            }
            if (logmask_index == LOG_MASK_BITS)
            {
                return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
            }
            logmask_index += LOG_MASK_BASE;
        }
        l->logmask |= (1<<logmask_index);
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      req_set_dumpcom
//
//  DESCRIPTION:
//
//      set request data for dumpcom
//
//  FORMAL PARAMETERS:
//
//      argc - number of argument from command line except argv[0] and argv[1]
//      argv - pointer of arguments except argv[0] and argv[1]
//      len - in: buffer length
//            out: actual length of request data
//      buf - buffer for request data
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//


static MTC_STATUS
req_set_dumpcom(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf)
{
    SCRIPT_DATA_REQUEST_DUMPCOM *d = buf;
    MTC_U32 arg_index;

    if (*len < sizeof(SCRIPT_DATA_REQUEST_DUMPCOM)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    d->dumpflag = FALSE;

    for (arg_index = 0; arg_index < argc; arg_index++)
    {
        if (!strcmp("dump", argv[arg_index]))
        {
            d->dumpflag = TRUE;
        }
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      req_privatelog
//
//  DESCRIPTION:
//
//      set request data for privatelog
//
//  FORMAL PARAMETERS:
//
//      argc - number of argument from command line except argv[0] and argv[1]
//      argv - pointer of arguments except argv[0] and argv[1]
//      len - in: buffer length
//            out: actual length of request data
//      buf - buffer for request data
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//


static MTC_STATUS
req_privatelog(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf)
{
    SCRIPT_DATA_REQUEST_PRIVATELOG *p = buf;
    MTC_U32 arg_index;

    if (*len < sizeof(SCRIPT_DATA_REQUEST_PRIVATELOG)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    p->cmd = PRIVATELOG_CMD_NONE;

    for (arg_index = 0; arg_index < argc; arg_index++)
    {
        if (!strcmp("on", argv[arg_index]))
        {
            p->cmd = PRIVATELOG_CMD_ENABLE;
            return MTC_SUCCESS;
        }
        else if (!strcmp("off", argv[arg_index]))
        {
            p->cmd = PRIVATELOG_CMD_DISABLE;
            return MTC_SUCCESS;
        }
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      print_privatelog
//
//  DESCRIPTION:
//
//      print the privatelog information of xhad.
//
//  FORMAL PARAMETERS:
//
//      param - pointer to the SCRIPT_DATA_RESPONSE_PRIVATELOG
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

static MTC_STATUS
print_privatelog(
    MTC_U32 size,
    void *param)
{
    SCRIPT_DATA_RESPONSE_PRIVATELOG *r;
    r = (SCRIPT_DATA_RESPONSE_PRIVATELOG *) param;

    printf("privatelog is %s\n", (r->privatelogflag == FALSE)?"off":"on");
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      req_fist
//
//  DESCRIPTION:
//
//      set request data for fist
//
//  FORMAL PARAMETERS:
//
//      argc - number of argument from command line except argv[0] and argv[1]
//      argv - pointer of arguments except argv[0] and argv[1]
//      len - in: buffer length
//            out: actual length of request data
//      buf - buffer for request data
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//


static MTC_STATUS
req_fist(
    int argc,
    char **argv,
    MTC_U32 *len,
    void *buf)
{
    SCRIPT_DATA_REQUEST_FIST *f = buf;

    if (*len < sizeof(SCRIPT_DATA_REQUEST_FIST)) 
    {
        return MTC_ERROR_SC_IMPROPER_DATA;
    }

    if (argc < 2)
    {
        return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
    }

    if (!strcmp(argv[0], "enable"))
    {
        f->set = TRUE;
    }
    else if (!strcmp(argv[0], "disable"))
    {
        f->set = FALSE;
    }
    else 
    {
        return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
    }
    if (strlen(argv[1]) < MAX_FIST_NAME_LEN)
    {
        strcpy(f->name, argv[1]);
    }
    else 
    {
        return MTC_ERROR_SC_INVALID_PARAMETER;  // invalid argument
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      printhelp
//
//  DESCRIPTION:
//
//      print help
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//
//  ENVIRONMENT:
//
//      none
//
//

void
printhelp(void)
{
    MTC_S32 cmd_index;
    printf("usage: calldaemon <command> [args]\n");
    printf("  command list\n");
    for (cmd_index = 0; cmd_info_list[cmd_index].name != NULL; cmd_index++)
    {
        if (cmd_info_list[cmd_index].req_func == NULL) 
        {
            printf("    %s\n", cmd_info_list[cmd_index].name);
        }
        else
        {
            if (!strcmp(cmd_info_list[cmd_index].name, "set_pool_state"))
            {
                printf("    %s init|active|invalid\n", cmd_info_list[cmd_index].name);
            }
            else if (!strcmp(cmd_info_list[cmd_index].name, "setlogmask")||
                     !strcmp(cmd_info_list[cmd_index].name, "resetlogmask"))
            {
                printf("    %s [all] [<MASKNAME>|<MASKNUMBER>] ... \n", cmd_info_list[cmd_index].name);
            }
            else if (!strcmp(cmd_info_list[cmd_index].name, "dumpcom"))
            {
                printf("    %s [dump]\n", cmd_info_list[cmd_index].name);
            }
            else if (!strcmp(cmd_info_list[cmd_index].name, "privatelog"))
            {
                printf("    %s [on|off]\n", cmd_info_list[cmd_index].name);
            }
            else if (!strcmp(cmd_info_list[cmd_index].name, "fist"))
            {
                printf("    %s enable|disable <fist_point_name>\n", cmd_info_list[cmd_index].name);
            }
            else
            {
                printf("    %s\n", cmd_info_list[cmd_index].name);
            }
        }
    }
}

//
//
//  NAME:
//
//      main
//
//  DESCRIPTION:
//
//      main of calldaemon
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//      MTC_EXIT_SUCCESS - success the call
//      MTC_EXIT_INVALID_PARAMETER - improper parameters
//      MTC_EXIT_DAEMON_IN_NOT_PRESENT
//      MTC_EXIT_SYSTEM_ERROR
//      MTC_EXIT_TRANSIENT_SYSTEM_ERROR
//      MTC_EXIT_INTERNAL_BUG
//
//  ENVIRONMENT:
//
//      none
//
//


int 
main(
    int argc,
    char **argv)
{
    MTC_STATUS ret;
    
    SCRIPT_DATA_REQUEST req;
    SCRIPT_DATA_RESPONSE res;
    CMD_INFO *c = NULL;

    if (argc < 2)
    {
        return MTC_EXIT_INVALID_PARAMETER;  // invalid argument;
    }

    if (!strcmp(argv[1], "help"))
    {
        printhelp();
        return MTC_EXIT_SUCCESS;
    }


    if (getuid() != 0)
    {
        // not root
        return MTC_EXIT_INVALID_ENVIRONMENT;
    }

    for (c = cmd_info_list; c->name != NULL; c++) 
    {
        if (!strcmp(argv[1], c->name))
        {
            break;
        }
    }

    if (c->name == NULL) 
    {
        return MTC_EXIT_INVALID_PARAMETER; // invalid argument;
    }

    if (c->req_func != NULL)
    {
        req.head.length = sizeof(req.body);
        ret = (c->req_func)(argc - 2, &(argv[2]), &(req.head.length), &(req.body));
        if (ret != MTC_SUCCESS) 
        {
            return MTC_EXIT_INVALID_PARAMETER; // invalid argument;
        }
    }
    else
    {
        req.head.length = 0;
    }

    res.head.length = sizeof(res.body);
    ret = script_call_daemon(c->type, 
                             req.head.length,&(req.body), 
                             &(res.head.length), &(res.body));
    
    if (ret != MTC_SUCCESS) 
    {
        // fprintf(stderr,"ret = %d\n", ret);
        return status_to_exit(ret);
    }
    if (c->res_func != NULL)
    {
        return status_to_exit((c->res_func)(res.head.length, &(res.body)));

    }
    else 
    {
        return MTC_EXIT_SUCCESS;
    }
}





