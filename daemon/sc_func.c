//  MODULE: sc_func.c

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
//      Xen HA script service routine.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 12, 2008
//
//   

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "com.h"
#include "script.h"
#include "sm.h"
#include "xapi_mon.h"
#include "bond_mon.h"
#include "lock_mgr.h"
#include "statefile.h"
#include "buildid.h"
#include "fist.h"

//
//
//  NAME:
//
//      get_hoststate
//
//  DESCRIPTION:
//
//      get localhost state
//
//  FORMAL PARAMETERS:
//
//      None
//          
//  RETURN VALUE:
//
//
//      LIVESET_STATUS_STARTING
//      LIVESET_STATUS_ONLINE
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATIC MTC_U32
get_hoststate(
    void)
{
    MTC_U32 state;

    HA_COMMON_OBJECT_HANDLE h_sm = NULL;
    COM_DATA_SM *sm = NULL;

    //
    // fill from sm
    //
    
    com_open(COM_ID_SM, &h_sm);
    com_reader_lock(h_sm, (void *)&sm);
    if (sm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
        assert(FALSE);
        state = LIVESET_STATUS_STARTING;
    }
    else 
    {
        //state = MTC_HOSTMAP_ISON(sm->current_liveset, _my_index)?LIVESET_STATUS_ONLINE:LIVESET_STATUS_STARTING;

        state = (sm->phase == SM_PHASE_STARTING || 
                 sm->phase == SM_PHASE_ABORTED)
            ? LIVESET_STATUS_STARTING
            : LIVESET_STATUS_ONLINE;
    }
    com_reader_unlock(h_sm);
    com_close(h_sm);
    return state;
}


//
//
//  NAME:
//
//      script_service_do_query_liveset();
//
//  DESCRIPTION:
//
//      script service function for query_liveset
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_query_liveset(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_QUERY_LIVESET *l;
    MTC_U32 host_index;

    HA_COMMON_OBJECT_HANDLE h_sm = NULL;
    COM_DATA_SM *sm = NULL;

    HA_COMMON_OBJECT_HANDLE h_hb = NULL;
    COM_DATA_HB *hb = NULL;
    
    HA_COMMON_OBJECT_HANDLE h_sf = NULL;
    COM_DATA_SF *sf = NULL;
    
    HA_COMMON_OBJECT_HANDLE h_xapimon = NULL;
    COM_DATA_XAPIMON *xapimon = NULL;
    
    HA_COMMON_OBJECT_HANDLE h_bm = NULL;
    COM_DATA_BM *bm = NULL;

    MTC_CLOCK now;
    struct timespec ts;

    MTC_BOOLEAN         hb_approaching_timeout_reported = FALSE;
    MTC_BOOLEAN         sf_approaching_timeout_reported = FALSE;
    MTC_BOOLEAN         xapi_approaching_timeout_reported = FALSE;


    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = tstoms(ts);

    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_QUERY_LIVESET)) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_QUERY_LIVESET);
    memset(res_body, 0, *res_len);
    l = (SCRIPT_DATA_RESPONSE_QUERY_LIVESET *)res_body;

    //
    // fill from conf
    //


    l->hostnum = _num_host;
    l->localhost_index = _my_index;
    l->T1 = _T1 * 1000;
    l->T2 = _T2 * 1000;
    l->T3 = _TXapi * 1000;
    l->Wh = _Wh * 1000;
    l->Ws = _Ws * 1000;

    for (host_index = 0; host_index < _num_host; host_index++) 
    {
        UUID_cpy(l->host[host_index].host_id, _host_info[host_index].host_id);
    }

    //
    // fill from sm
    //
    
    com_open(COM_ID_SM, &h_sm);
    com_reader_lock(h_sm, (void *)&sm);
    if (sm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
        assert(FALSE);
        l->status = LIVESET_STATUS_STARTING;
    }
    else 
    {
        // l->status = MTC_HOSTMAP_ISON(sm->current_liveset, _my_index)?LIVESET_STATUS_ONLINE:LIVESET_STATUS_STARTING;

        l->status = (sm->phase == SM_PHASE_STARTING) 
            ? LIVESET_STATUS_STARTING
            : LIVESET_STATUS_ONLINE;
        
        for (host_index = 0; host_index < _num_host; host_index++) 
        {
            l->host[host_index].liveness = MTC_HOSTMAP_ISON(sm->current_liveset, host_index)?TRUE:FALSE;
            // l->host[host_index].excluded = MTC_HOSTMAP_ISON(sm->excluded, host_index)?TRUE:FALSE;
            // l->host[host_index].sf_access = (l->host[host_index].liveness)?(MTC_HOSTMAP_ISON(sm->sf_access, host_index)?TRUE:FALSE):FALSE;
            l->host[host_index].sf_corrupted = MTC_HOSTMAP_ISON(sm->sf_corrupted, host_index)?TRUE:FALSE;
        }
        l->sf_lost = sm->SR2;

        hb_approaching_timeout_reported = sm->hb_approaching_timeout;
        sf_approaching_timeout_reported = sm->sf_approaching_timeout;
        xapi_approaching_timeout_reported = sm->xapi_approaching_timeout;
    }
    com_reader_unlock(h_sm);
    com_close(h_sm);

    //
    // fill from heartbeat
    // rewrite latency, latency_max, latency_min
    //
    
    com_open(COM_ID_HB, &h_hb);
    com_writer_lock(h_hb, (void *)&hb);
    if (hb == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) hb data is NULL.\n", __func__);
        assert(FALSE);
        l->status = LIVESET_STATUS_STARTING;
    }
    else 
    {
        l->hb_latency = hb->latency;
        l->hb_latency_max = hb->latency_max;
        l->hb_latency_min = hb->latency_min;

        // reset latency
        if (l->status == LIVESET_STATUS_ONLINE)
        {
            hb->latency_max = -1;
            hb->latency_min = -1;
        }

        // check approaching timeout

        if (l->hb_latency_max != -1 &&
            l->hb_latency_max > l->T1 / 2)
        {
            if (!hb_approaching_timeout_reported ||
                (logmask & MTC_LOG_MASK_SC_WARNING))
            {

                log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"delay in heartbeat thread approaching limit\". hb_latency_max=%d.\n", __func__, l->hb_latency_max);
            }
            l->hb_approaching_timeout = TRUE;
        }

        for (host_index = 0; host_index < _num_host; host_index++) 
        {

            if (hb->time_last_HB[host_index] < 0)
            {
	        /* it must be a signed value */
                l->host[host_index].time_since_last_hb = -1;
            }
            else
            {
	        /* signed 64bits -> signed 32bits, but since it is time in ms, signed 32bits can contain a 23 days interval */
                l->host[host_index].time_since_last_hb = now - hb->time_last_HB[host_index];
            }
            if (l->host[host_index].liveness)
            {
                MTC_U32 hi;
                if (host_index != _my_index) 
                {
                    for (hi = 0; hi < _num_host; hi++)
                    {
                        l->host[host_index].hb_list_on_hb[hi] = 
                            MTC_HOSTMAP_ISON(hb->raw[host_index].hbdomain, hi)?TRUE:FALSE;
                        l->host[host_index].sf_list_on_hb[hi] = 
                            MTC_HOSTMAP_ISON(hb->raw[host_index].sfdomain, hi)?TRUE:FALSE;
                    }
                }

                // check approaching timeout
                if (l->host[host_index].time_since_last_hb >= 0 &&
                    l->host[host_index].time_since_last_hb > l->T1 / 2 )
                {
                    if (!hb_approaching_timeout_reported ||
                        (logmask & MTC_LOG_MASK_SC_WARNING))
                    {
                        log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"heartbeat approaching timeout\". host[%d].time_since_last_hb=%d.\n", __func__, host_index, l->host[host_index].time_since_last_hb);
                    }
                    l->hb_approaching_timeout = TRUE;
                }
            }

            // time_since_xapi_restart comes on HB.
            // actual time of time_since_xapi_restart is 
            // time since last HB received from the host + time_since_xapi_restart (recorded on HB packet)

            l->host[host_index].time_since_xapi_restart 
                = (hb->raw[host_index].time_since_xapi_restart < 0)
                ? -1
                : l->host[host_index].time_since_last_hb + hb->raw[host_index].time_since_xapi_restart;

            // xapi error string
            strncpy(l->host[host_index].xapi_err_string, hb->err_string[host_index], sizeof(hb->err_string[host_index]));
        }

        // for my_index, get from hb directory

        for (host_index = 0; host_index < _num_host; host_index++) 
        {
            l->host[_my_index].hb_list_on_sf[host_index] = 
                l->host[_my_index].hb_list_on_hb[host_index] = 
                MTC_HOSTMAP_ISON(hb->hbdomain, host_index)?TRUE:FALSE;
        }

    }
    com_writer_unlock(h_hb);
    com_close(h_hb);

    //
    // fill from statefile
    // rewrite latency, latency_max, latency_min
    //
    
    com_open(COM_ID_SF, &h_sf);
    com_writer_lock(h_sf, (void *)&sf);
    if (sf == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
        assert(FALSE);
        l->status = LIVESET_STATUS_STARTING;
    }
    else 
    {
        l->sf_latency = sf->latency;
        l->sf_latency_max = sf->latency_max;
        l->sf_latency_min = sf->latency_min;

        // reset latency
        if (l->status == LIVESET_STATUS_ONLINE)
        {
            sf->latency_min = -1;
            sf->latency_max = -1;
        }

        // check approaching timeout
        // not report approaching timeout if sf_lost is TRUE

        if (l->sf_lost == FALSE &&
            l->sf_latency_max >= 0 &&
            l->sf_latency_max > l->T2 / 2)
        {
            if (!sf_approaching_timeout_reported ||
                (logmask & MTC_LOG_MASK_SC_WARNING))
            {
                log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"State-File approaching timeout\". sf_latency_max=%d.\n", __func__, l->sf_latency_max);
            }
            l->sf_approaching_timeout = TRUE;
        }


        for (host_index = 0; host_index < _num_host; host_index++) 
        {
            l->host[host_index].master = 
                UUID_comp(sf->master, l->host[host_index].host_id)?FALSE:TRUE;

            // sf_access

            if (l->host[host_index].liveness)
            {
                if (l->sf_lost) 
                {

                    // SR-2

                    if (host_index == _my_index) 
                    {
                        l->host[host_index].sf_access = (sf->SF_access)?TRUE:FALSE;
                    }
                    else
                    {
                        l->host[host_index].sf_access =
                            l->host[host_index].sf_list_on_hb[host_index];
                    }
                }
                else 
                {
                    // sf_access is always TRUE for the host 
                    // which is in the liveset in SR-1

                    l->host[host_index].sf_access = TRUE;
                }
            }
            else
            {
                // sf_access is always FALSE for the host 
                // which is not in the liveset
                
                l->host[host_index].sf_access = FALSE;
            }

            l->host[host_index].excluded = MTC_HOSTMAP_ISON(sf->excluded, host_index)?TRUE:FALSE;

            if (sf->time_last_SF[host_index] < 0)
            {
                l->host[host_index].time_since_last_update_on_sf = -1;
            }
            else
            {
                l->host[host_index].time_since_last_update_on_sf = 
                    now - sf->time_last_SF[host_index];
            }
            if (l->host[host_index].liveness)
            {
                MTC_U32 hi;
                if (host_index != _my_index) 
                {
                    for (hi = 0; hi < _num_host; hi++)
                    {
                        if (MTC_HOSTMAP_ISON(sf->sfdomain, host_index)) {
                            l->host[host_index].hb_list_on_sf[hi] = 
                                MTC_HOSTMAP_ISON(sf->raw[host_index].hbdomain, hi)?TRUE:FALSE;
                            l->host[host_index].sf_list_on_sf[hi] = 
                                MTC_HOSTMAP_ISON(sf->raw[host_index].sfdomain, hi)?TRUE:FALSE;
                        }
                        else {

                            // hb_list_on_sf and sf_list_on_sf should be NULL
                            // if the host is not in the sfdomain
                            // (in the case of SR-2)

                            l->host[host_index].hb_list_on_sf[hi] = 
                                l->host[host_index].sf_list_on_sf[hi] = 
                                FALSE;
                        }
                    }
                }

                // check approaching timeout
                // not report approaching timeout if sf_lost is TRUE

                if (l->sf_lost == FALSE &&
                    l->host[host_index].time_since_last_update_on_sf >= 0 &&
                    l->host[host_index].time_since_last_update_on_sf > l->T2 / 2)
                {
                    if (!sf_approaching_timeout_reported ||
                        (logmask & MTC_LOG_MASK_SC_WARNING))
                    {
                        log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"State-File approaching timeout\". host[%d].time_since_last_update_on_sf=%d.\n", __func__, host_index, l->host[host_index].time_since_last_update_on_sf);
                    }
                    l->sf_approaching_timeout = TRUE;
                }
            }
        }

        // for my_index, get from sf directory

        for (host_index = 0; host_index < _num_host; host_index++) 
        {
            l->host[_my_index].sf_list_on_sf[host_index] = 
                l->host[_my_index].sf_list_on_hb[host_index] = 
                MTC_HOSTMAP_ISON(sf->sfdomain, host_index)?TRUE:FALSE;
        }
        l->host[_my_index].sf_corrupted = (sf->SF_corrupted)?TRUE:FALSE;

        // l->sf_lost = (sf->SF_access)?FALSE:TRUE;
    }
    com_writer_unlock(h_sf);
    com_close(h_sf);

    //
    // fill from xapi_mon
    // rewrite latency, latency_max, latency_min
    //
    
    com_open(COM_ID_XAPIMON, &h_xapimon);
    com_writer_lock(h_xapimon, (void *)&xapimon);
    if (xapimon == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) xapimon data is NULL.\n", __func__);
        assert(FALSE);
        l->xapi_latency = -1;
        l->xapi_latency_max = -1;
        l->xapi_latency_min = -1;
    }
    else 
    {
        l->xapi_latency = xapimon->latency;
        l->xapi_latency_max = xapimon->latency_max;
        l->xapi_latency_min = xapimon->latency_min;

        // reset latency
        if (l->status == LIVESET_STATUS_ONLINE)
        {
            xapimon->latency_max = -1;
            xapimon->latency_min = -1;
        }

        // xapi error string for my index
        strncpy(l->host[_my_index].xapi_err_string, xapimon->err_string, sizeof(xapimon->err_string));

        // check approaching timeout
        if (l->xapi_latency_max >= 0 &&
            l->xapi_latency_max > l->T3 / 2)
        {
            if (!xapi_approaching_timeout_reported ||
                (logmask & MTC_LOG_MASK_SC_WARNING))
            {
                log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"Xapi approaching timeout\". xapi_latency_max=%d.\n", __func__, l->xapi_latency_max);
            }
            l->xapi_approaching_timeout = TRUE;
        }

        // check executing healthcheck 
        if (xapimon->time_healthcheck_start >= 0 &&
            now - xapimon->time_healthcheck_start >= l->T3 / 2)
        {
            if (!xapi_approaching_timeout_reported ||
                (logmask & MTC_LOG_MASK_SC_WARNING))
            {
                log_message(MTC_LOG_WARNING, "SC: (%s) reporting \"Xapi approaching timeout\". now=%"PRId64" start=%"PRId64".\n", __func__, now, xapimon->time_healthcheck_start);
            }
            l->xapi_approaching_timeout = TRUE;
        }


        if (xapimon->time_Xapi_restart < 0) {
            l->host[_my_index].time_since_xapi_restart = -1;
        }
        else {
            l->host[_my_index].time_since_xapi_restart = 
                now - xapimon->time_Xapi_restart;        
        }
    }
    com_writer_unlock(h_xapimon);
    com_close(h_xapimon);

    //
    // fill from bond_mon
    //
    
    com_open(COM_ID_BM, &h_bm);
    com_reader_lock(h_bm, (void *)&bm);
    if (bm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) bm data is NULL.\n", __func__);
        assert(FALSE);
        l->bonding_error = FALSE;
    }
    else 
    {
        if (bm->mtc_bond_status == BOND_STATUS_ERR ||
            bm->mtc_bond_status == BOND_STATUS_DEGRADED) 
        {
            l->bonding_error = TRUE;
        }
        else 
        {
        l->bonding_error = FALSE;
        }
    }
    com_reader_unlock(h_bm);
    com_close(h_bm);

    //
    // write back warning (hb/sf/xapi approaching timeout) to SM
    //
    
    com_open(COM_ID_SM, &h_sm);
    com_writer_lock(h_sm, (void *)&sm);
    if (sm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
        assert(FALSE);
    }
    else 
    {
        if (sm->hb_approaching_timeout != FALSE && 
            l->hb_approaching_timeout == FALSE)
        {
            log_message(MTC_LOG_INFO, "SC: (%s) \"Heartbeat approaching timeout\" turned FALSE \n", __func__);
        }
        if (sm->sf_approaching_timeout != FALSE && 
            l->sf_approaching_timeout == FALSE)
        {
            log_message(MTC_LOG_INFO, "SC: (%s) \"State-file approaching timeout\" turned FALSE \n", __func__);
        }
        if (sm->xapi_approaching_timeout != FALSE && 
            l->xapi_approaching_timeout == FALSE)
        {
            log_message(MTC_LOG_INFO, "SC: (%s) \"Xapi approaching timeout\" turned FALSE \n", __func__);
        }
        sm->hb_approaching_timeout = l->hb_approaching_timeout;
        sm->sf_approaching_timeout = l->sf_approaching_timeout;
        sm->xapi_approaching_timeout = l->xapi_approaching_timeout;
    }
    com_writer_unlock(h_sm);
    com_close(h_sm);

    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_propose_master();
//
//  DESCRIPTION:
//
//      script service function for propose_master
//
//  FORMAL PARAMETERS:
//
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


MTC_STATUS
script_service_do_propose_master(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_PROPOSE_MASTER *p;

    log_message(MTC_LOG_INFO, "SC: propose_master enter.\n");
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_PROPOSE_MASTER)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_PROPOSE_MASTER);
    memset(res_body, 0, *res_len);
    p = (SCRIPT_DATA_RESPONSE_PROPOSE_MASTER *)res_body;

    //
    // check host state is online
    //

    if (get_hoststate() != LIVESET_STATUS_ONLINE) {
        log_message(MTC_LOG_WARNING, "SC: propose_master failed. The local host is not in ONLINE state.\n");
        p->accepted_as_master = FALSE;
        p->retval = MTC_ERROR_SC_INVALID_LOCALHOST_STATE;
        goto error_return;
    }

    //
    // aquire lock
    //

    p->accepted_as_master = lm_request_lock(_my_UUID);
    p->retval = MTC_SUCCESS;

 error_return:
    if (p->retval == MTC_SUCCESS)
    {
        log_message(MTC_LOG_NOTICE, "propose_master returns %s.\n",
                    (p->accepted_as_master)?"TRUE":"FALSE");
    }
    else
    {
        log_status(p->retval, "propose_master failed");
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_cancel_master();
//
//  DESCRIPTION:
//
//      script service function for propose_master
//
//  FORMAL PARAMETERS:
//
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


MTC_STATUS
script_service_do_cancel_master(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    log_message(MTC_LOG_INFO, "SC: cancel_master enter.\n");
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY *)res_body;

    //
    // check host state is online
    //

    if (get_hoststate() != LIVESET_STATUS_ONLINE) {
        log_message(MTC_LOG_WARNING, "%s: the local host is not in ONLINE state.\n", __func__);
        r->retval = MTC_ERROR_SC_INVALID_LOCALHOST_STATE;
        goto error_return;
    }

    //
    // calcel lock
    //

    lm_cancel_lock();
    r->retval = MTC_SUCCESS;

 error_return:
    log_message(MTC_LOG_INFO, "SC: cancel_master returns.\n");
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      script_service_do_disarm_fencing();
//
//  DESCRIPTION:
//
//      script service function for disarm_fencing
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_disarm_fencing(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    HA_COMMON_OBJECT_HANDLE h_sm = NULL;
    COM_DATA_SM *sm = NULL;
    HA_COMMON_OBJECT_HANDLE h_hb = NULL;
    COM_DATA_HB *hb = NULL;
    HA_COMMON_OBJECT_HANDLE h_sf = NULL;
    COM_DATA_SF *sf = NULL;

    MTC_FENCING_MODE    fencing;

    log_message(MTC_LOG_INFO, "SC: disarm_fencing enter.\n");
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;

    //
    // check host state is online
    //

    if (get_hoststate() != LIVESET_STATUS_ONLINE) {
        log_message(MTC_LOG_WARNING, "SC: disarm_fencing failed. The local host is not in ONLINE state.\n");
        r->retval = MTC_ERROR_SC_INVALID_LOCALHOST_STATE;
        goto error_return;
    }

    //
    // open objects
    //

    com_open(COM_ID_SM, &h_sm);
    com_open(COM_ID_HB, &h_hb);
    com_open(COM_ID_SF, &h_sf);

    //
    // set fencing FENCING_DISARM_REQUESTED to SM
    //

    com_writer_lock(h_sm, (void *)&sm);
    if (sm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
        assert(FALSE);
    }
    else 
    {
        if (sm->fencing != FENCING_ARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) SM fencing_mode is not ARMED (%d).\n", __func__, sm->fencing);
        }
        if (sm->fencing == FENCING_ARMED || sm->fencing == FENCING_NONE)
        {
            sm->fencing = FENCING_DISARM_REQUESTED;
        }
    }
    com_writer_unlock(h_sm);

    //
    // set fencing FENCING_DISARM_REQUESTED to HB
    //
    
    com_writer_lock(h_hb, (void *)&hb);
    if (hb == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) hb data is NULL.\n", __func__);
        assert(FALSE);
    }
    else 
    {
        if (hb->fencing != FENCING_ARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) HB fencing_mode is not ARMED (%d).\n", __func__, hb->fencing);
        }
        if (hb->fencing == FENCING_ARMED || hb->fencing == FENCING_NONE)
        {
            hb->fencing = FENCING_DISARM_REQUESTED;
        }
    }
    com_writer_unlock(h_hb);

    //
    // set fencing FENCING_DISARM_REQUESTED to SF
    //
    
    com_writer_lock(h_sf, (void *)&sf);
    if (sf == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
        assert(FALSE);
    }
    else 
    {
        if (sf->fencing != FENCING_ARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) SF fencing_mode is not ARMED (%d).\n", __func__, sf->fencing);
        }
        if (sf->fencing == FENCING_ARMED || sf->fencing == FENCING_NONE)
        {
            sf->fencing = FENCING_DISARM_REQUESTED;
        }
    }
    com_writer_unlock(h_sf);

    //
    // wait disarm fencing complation to SM
    //

    fencing = FENCING_NONE;
    while (TRUE)
    {
        com_reader_lock(h_sm, (void *)&sm);
        if (sm == NULL) 
        {
            log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
            assert(FALSE);
        }
        else 
        {
            fencing = sm->fencing;
        }
        com_reader_unlock(h_sm);
        if (fencing == FENCING_DISARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) disarm_fencing completed for SM.\n", __func__);
            break;
        }
        sleep (1);
    }

    //
    // wait disarm fencing completion to HB
    //

    fencing = FENCING_NONE;
    while (TRUE)
    {
        com_reader_lock(h_hb, (void *)&hb);
        if (hb == NULL) 
        {
            log_internal(MTC_LOG_WARNING, "SC: (%s) hb data is NULL.\n", __func__);
            assert(FALSE);
        }
        else 
        {
            fencing = hb->fencing;
        }
        com_reader_unlock(h_hb);
        if (fencing == FENCING_DISARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) disarm_fencing completed for HB.\n", __func__);
            break;
        }
        sleep (1);
    }

    //
    // wait disarm fencing complation to SF
    //

    fencing = FENCING_NONE;
    while (TRUE)
    {
        com_reader_lock(h_sf, (void *)&sf);
        if (sf == NULL) 
        {
            log_internal(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
            assert(FALSE);
        }
        else 
        {
            fencing = sf->fencing;
        }
        com_reader_unlock(h_sf);
        if (fencing == FENCING_DISARMED)
        {
            log_message(MTC_LOG_DEBUG, "SC: (%s) disarm_fencing completed for SF.\n", __func__);
            break;
        }
        sleep (1);
    }

    //
    // close objects
    //

    com_close(h_sf);
    com_close(h_hb);
    com_close(h_sm);

    r->retval = MTC_SUCCESS;

 error_return:
    if (r->retval == MTC_SUCCESS)
    {
        log_message(MTC_LOG_NOTICE, "disarm_fencing returns SUCCESS.\n");
    }
    else
    {
        log_status(r->retval, "disarm_fencing failed");
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_set_pool_state();
//
//  DESCRIPTION:
//
//      script service for set_pool_state
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_set_pool_state(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_SET_POOL_STATE *s;
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    HA_COMMON_OBJECT_HANDLE h_sf = NULL;
    COM_DATA_SF *sf = NULL;
    MTC_CLOCK   time_start, time_last_SF;
    char *pool_state_string;


    if (req_len < sizeof(SCRIPT_DATA_REQUEST_SET_POOL_STATE)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);

    s = (SCRIPT_DATA_REQUEST_SET_POOL_STATE*) req_body;
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;

    //
    // set pool_state_string for log
    //


    switch (s->state)
    {
    case SF_STATE_INIT:
        pool_state_string = "init";
        break;
    case SF_STATE_ACTIVE:
        pool_state_string = "active";
        break;
    case SF_STATE_INVALID:
        pool_state_string = "invalid";
        break;
    default:
        pool_state_string = "unknown";
        break;
    }
    
    log_message(MTC_LOG_INFO, "SC: set_pool_state \"%s\" enter.\n",pool_state_string);

    r->retval = MTC_SUCCESS;

    //
    // check statefile access
    //

    com_open(COM_ID_SF, &h_sf);
    com_reader_lock(h_sf, (void *)&sf);
    if (sf == NULL) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
        assert(FALSE);
        r->retval = MTC_ERROR_SC_STATEFILE_ACCESS;
    }
    else 
    {
        if (sf->SF_access == FALSE)
        {
            log_message(MTC_LOG_WARNING, "SC: (%s) SF_access is FALSE.\n", __func__);
            r->retval = MTC_ERROR_SC_STATEFILE_ACCESS;
        }
    }
    com_reader_unlock(h_sf);
    if (r->retval != MTC_SUCCESS)
    {
        goto skip_return;
    }

    // do set_pool_state

    sf_set_pool_state(s->state);

    time_start = _getms();
    
    //
    // wait until written on disk
    //

    do {
        com_reader_lock(h_sf, (void *)&sf);
        if (sf == NULL) 
        {
            log_message(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
            assert(FALSE);
            r->retval = MTC_ERROR_SC_STATEFILE_ACCESS;
        }
        else 
        {
            if (sf->SF_access == FALSE)
            {
                log_message(MTC_LOG_WARNING, "SC: (%s) SF_access is FALSE.\n", __func__);
                r->retval = MTC_ERROR_SC_STATEFILE_ACCESS;
            }
            time_last_SF = sf->time_last_SF[_my_index];
        }
        com_reader_unlock(h_sf);
        if (r->retval != MTC_SUCCESS)
        {
            goto skip_return;
        }
        if (time_last_SF > time_start)
        {
            // excluded flag written to SF
            goto skip_return;
        }
        sleep (1);
    } while (TRUE);

 skip_return:
    com_close(h_sf);
    if (r->retval == MTC_SUCCESS)
    {
        log_message(MTC_LOG_NOTICE, "set_pool_state \"%s\" returns SUCCESS.\n", pool_state_string);
    }
    else
    {
        log_status(r->retval, "set_pool_state failed");
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      do_set_clear_excluded_sub();
//
//  DESCRIPTION:
//
//      set/clear excluded
//
//  FORMAL PARAMETERS:
//
//      set - TRUE: set_excluded
//            FALSE: clear_excluded
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

MTC_STATIC  MTC_STATUS
do_set_clear_excluded_sub(
    MTC_U32 set)
{
    HA_COMMON_OBJECT_HANDLE h_sf = NULL;
    COM_DATA_SF *sf = NULL;
    MTC_CLOCK   time_start, time_last_SF;
    MTC_STATUS ret;

    ret = MTC_SUCCESS;

    //
    // check statefile access
    //

    com_open(COM_ID_SF, &h_sf);
    com_reader_lock(h_sf, (void *)&sf);
    if (sf == NULL) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_SC_STATEFILE_ACCESS;
    }
    else 
    {
        if (sf->SF_access == FALSE)
        {
            log_message(MTC_LOG_WARNING, "SC: (%s) SF_access is FALSE.\n", __func__);
            ret = MTC_ERROR_SC_STATEFILE_ACCESS;
        }
        else if (sf->pool_state != SF_STATE_ACTIVE)
        {
            log_message(MTC_LOG_WARNING, "SC: (%s) pool_state %d is not \"Active\".\n", __func__, sf->pool_state);
            ret = MTC_ERROR_SC_INVALID_POOL_STATE;
        }
    }
    com_reader_unlock(h_sf);
    if (ret != MTC_SUCCESS)
    {
        goto skip_return;
    }

    //
    // do set excluded
    //

    ret = sf_set_excluded(set);

    time_start = _getms();
    
    //
    // wait until written on disk
    //

    do {
        com_reader_lock(h_sf, (void *)&sf);
        if (sf == NULL) 
        {
            log_message(MTC_LOG_WARNING, "SC: (%s) sf data is NULL.\n", __func__);
            assert(FALSE);
            ret = MTC_ERROR_SC_STATEFILE_ACCESS;
        }
        else 
        {
            if (sf->SF_access == FALSE)
            {
                log_message(MTC_LOG_WARNING, "SC: (%s) SF_access is FALSE.\n", __func__);
                ret = MTC_ERROR_SC_STATEFILE_ACCESS;
            }
            time_last_SF = sf->time_last_SF[_my_index];
        }
        com_reader_unlock(h_sf);
        if (ret != MTC_SUCCESS)
        {
            goto skip_return;
        }
        if (time_last_SF > time_start)
        {
            // excluded flag written to SF
            goto skip_return;
        }
        sleep (1);
    } while (TRUE);

 skip_return:
    com_close(h_sf);
    return ret;
}



//
//
//  NAME:
//
//      script_service_do_set_excluded();
//
//  DESCRIPTION:
//
//      script service for set_excluded
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_set_excluded(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    log_message(MTC_LOG_INFO, "SC: set_excluded enter.\n");
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);

    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;
    r->retval = do_set_clear_excluded_sub(TRUE);

    if (r->retval == MTC_SUCCESS)
    {
        log_message(MTC_LOG_NOTICE, "set_excluded returns SUCCESS.\n");
    }
    else
    {
        log_status(r->retval, "set_excluded failed");
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      script_service_do_clear_excluded();
//
//  DESCRIPTION:
//
//      script service for clear_excluded
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_clear_excluded(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    log_message(MTC_LOG_INFO, "SC: clear_excluded enter.\n");
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);

    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;
    r->retval = do_set_clear_excluded_sub(FALSE);

    if (r->retval == MTC_SUCCESS)
    {
        log_message(MTC_LOG_NOTICE, "clear_excluded returns SUCCESS.\n");
    }
    else
    {
        log_status(r->retval, "clear_excluded failed");
    }
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      script_service_do_pid();
//
//  DESCRIPTION:
//
//      script service for pid
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_pid(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_PID *p;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_PID)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_PID);
    memset(res_body, 0, *res_len);

    p = (SCRIPT_DATA_RESPONSE_PID *)res_body;
    p->pid = getpid();
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_hoststate();
//
//  DESCRIPTION:
//
//      script service function for status
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_hoststate(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_HOSTSTATE *l;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_HOSTSTATE)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_HOSTSTATE);
    memset(res_body, 0, *res_len);
    l = (SCRIPT_DATA_RESPONSE_HOSTSTATE *)res_body;

    l->status = get_hoststate();
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      script_service_do_getlogmask();
//
//  DESCRIPTION:
//
//      script service function for getlogmask
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_getlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_GETLOGMASK *l;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK);
    memset(res_body, 0, *res_len);
    l = (SCRIPT_DATA_RESPONSE_GETLOGMASK *)res_body;

    l->logmask = logmask;
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_setlogmask();
//
//  DESCRIPTION:
//
//      script service function for setlogmask
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_setlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_SETLOGMASK *r;
    SCRIPT_DATA_RESPONSE_GETLOGMASK *l;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (req_len < sizeof(SCRIPT_DATA_REQUEST_SETLOGMASK)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    r = (SCRIPT_DATA_REQUEST_SETLOGMASK *)req_body;
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK);
    memset(res_body, 0, *res_len);
    l = (SCRIPT_DATA_RESPONSE_GETLOGMASK *)res_body;

    logmask |= r->logmask;
    l->logmask = logmask;
    log_logmask();
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_resetlogmask();
//
//  DESCRIPTION:
//
//      script service function for resetlogmask
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_resetlogmask(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_RESETLOGMASK *r;
    SCRIPT_DATA_RESPONSE_GETLOGMASK *l;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (req_len < sizeof(SCRIPT_DATA_REQUEST_RESETLOGMASK)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    r = (SCRIPT_DATA_REQUEST_RESETLOGMASK *)req_body;
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_GETLOGMASK);
    memset(res_body, 0, *res_len);
    l = (SCRIPT_DATA_RESPONSE_GETLOGMASK *)res_body;

    logmask &= ~(r->logmask);
    l->logmask = logmask;
    log_logmask();
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_privatelog();
//
//  DESCRIPTION:
//
//      script service function for resetlogmask
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_privatelog(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_PRIVATELOG *p;
    SCRIPT_DATA_RESPONSE_PRIVATELOG *r;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (req_len < sizeof(SCRIPT_DATA_REQUEST_PRIVATELOG)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    p = (SCRIPT_DATA_REQUEST_PRIVATELOG *)req_body;
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_PRIVATELOG)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_PRIVATELOG);
    memset(res_body, 0, *res_len);
    r = (SCRIPT_DATA_RESPONSE_PRIVATELOG *)res_body;

    switch (p->cmd)
    {
    case PRIVATELOG_CMD_ENABLE:
        privatelogflag = TRUE;
        log_message(MTC_LOG_INFO, "SC: privatelog turned on.\n");
        break;
    case PRIVATELOG_CMD_DISABLE:
        log_message(MTC_LOG_INFO, "SC: privatelog turned off.\n");
        privatelogflag = FALSE;
        break;
    case PRIVATELOG_CMD_NONE:
    default:
        log_maskable_debug_message(SCRIPT, "SC: get privatelog flag %d.\n",privatelogflag);
        break;
    }
    r->privatelogflag = privatelogflag;
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      script_service_do_dumpcom();
//
//  DESCRIPTION:
//
//      script service function for getlogmask
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_dumpcom(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_DUMPCOM *d;
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (req_len < sizeof(SCRIPT_DATA_REQUEST_DUMPCOM)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    d = (SCRIPT_DATA_REQUEST_DUMPCOM *)req_body;
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);

    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;

    r->retval = com_log_all_objects(d->dumpflag);

    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_buildid();
//
//  DESCRIPTION:
//
//      script service for pid
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_buildid(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_BUILDID *b;
    
    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_BUILDID)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }

    *res_len = sizeof(SCRIPT_DATA_RESPONSE_BUILDID);
    memset(res_body, 0, *res_len);

    b = (SCRIPT_DATA_RESPONSE_BUILDID*) res_body;

    log_maskable_debug_message(SCRIPT, "SC: builddate:%s build_id:%s.\n", BUILD_DATE, BUILD_ID);
    if (strlen(BUILD_ID) < BUILD_ID_LEN) 
    {
        strcpy(b->build_id, BUILD_ID);
    }
    else
    {
        strncpy(b->build_id, BUILD_ID, BUILD_ID_LEN - 1);
        b->build_id[BUILD_ID_LEN - 1] = '\0';
    }
    if (strlen(BUILD_DATE) < BUILD_DATE_LEN) 
    {
        strcpy(b->build_date, BUILD_DATE);
    }
    else
    {
        strncpy(b->build_date, BUILD_DATE, BUILD_DATE_LEN - 1);
        b->build_date[BUILD_DATE_LEN - 1] = '\0';
    }

    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_fist();
//
//  DESCRIPTION:
//
//      script service for fist
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_fist(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_REQUEST_FIST *f;
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;
    
    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (req_len < sizeof(SCRIPT_DATA_REQUEST_FIST)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) req_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    f = (SCRIPT_DATA_REQUEST_FIST *)req_body;
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;

    r->retval = (f->set)?fist_enable(f->name):fist_disable(f->name);

    log_message(MTC_LOG_DEBUG, "SC: fist \"%s\" %s returns %d.\n", f->name, (f->set)?"enable":"disable", r->retval);
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      script_service_do_set_host_weight();
//
//  DESCRIPTION:
//
//      script service for set_host_weight
//
//  FORMAL PARAMETERS:
//
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

MTC_STATUS
script_service_do_reload_host_weight(
    MTC_U32 req_len,
    void *req_body,
    MTC_U32 *res_len,
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;
    
    log_maskable_debug_message(SCRIPT, "SC: enter %s.\n", __func__);
    if (*res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return MTC_ERROR_SC_INSUFFICIENT_RESOURCE;
    }
    *res_len = sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY);
    memset(res_body, 0, *res_len);
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;

    
    r->retval = MTC_SUCCESS;

    //
    // set value to sm
    //

    log_maskable_debug_message(SCRIPT, "SC: reload_host_weight\n");
    r->retval = hostweight_reload();

    log_maskable_debug_message(SCRIPT, "SC: reload_host_weight returns %d.\n", r->retval);
    log_maskable_debug_message(SCRIPT, "SC: leave %s.\n", __func__);
    return MTC_SUCCESS;
}

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
    void *res_body)
{
    SCRIPT_DATA_RESPONSE_RETVAL_ONLY *r;

    if (type != SCRIPT_TYPE_SET_EXCLUDED) 
    {
        return;
    }
    if (res_len < sizeof(SCRIPT_DATA_RESPONSE_RETVAL_ONLY)) 
    {
        log_message(MTC_LOG_WARNING, "SC: (%s) res_len is too small.\n", __func__);
        assert(FALSE);
        return;
    }
    r = (SCRIPT_DATA_RESPONSE_RETVAL_ONLY*) res_body;
    if (r->retval != MTC_SUCCESS)
    {
        return;
    }

    //
    // ha_set_excluded succeeded.
    //
    // xhad process shall exit immediately without closing watchdog.
    // the host will be fenced by watchdog timer expires.
    //
    
    log_message(MTC_LOG_NOTICE, "Excluded flag is set while HA daemon is operating.\n");
    log_message(MTC_LOG_NOTICE, "HA daemon terminated.\n");
    exit(MTC_EXIT_SET_EXCLUDED);

    // never returns

    return;
}

