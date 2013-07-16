//
//++
//
//  MODULE: hostweight.c
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
//      HOST WEIGHT.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Novenber 11, 2008
//
//--
//

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
#include <sys/wait.h>
#include <pthread.h>

#include "log.h"
#include "mtcerrno.h"
#include "com.h"
#include "sm.h"
#include "hostweight.h"



static
MTC_BOOLEAN
error_reported = FALSE;   // true if error of open/lock/read/write for weightfile reported

static
HOST_WEIGHT_TABLE 
weight_table[MAX_HOST_WEIGHT_CLASS_NUM];





//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//

//
//  hostweight_initialize0
//
//  initialize weight_table
//
//

MTC_STATIC
MTC_STATUS
hostweight_initialize0(void)
{
    memset(weight_table, 0, sizeof(weight_table));
    return MTC_SUCCESS;
}


//
//  log_hostweight -
//
//  log all class and weight on weight_table
//
//

MTC_STATIC
void
log_hostweight(void)
{
    int index;

    log_message(MTC_LOG_INFO, "SC:     builtinclass=%s weight=%d\n",
                BUILTIN_HOST_WEIGHT_CLASSNAME,
                BUILTIN_HOST_WEIGHT_VALUE);

    for (index = 0; index < MAX_HOST_WEIGHT_CLASS_NUM; index++)
    {
        if (!strcmp(weight_table[index].classname, "")) continue;
        log_message(MTC_LOG_INFO, "SC:     class=%s weight=%d\n",
                    weight_table[index].classname,
                    weight_table[index].weight);
    }
    return;
}

//
//  calc_hostweight -
//
//  calculate the sum of weight on weight_table plus BUILTIN_HOST_WEIGHT_VALUE
//
//

MTC_STATIC
MTC_U32
calc_hostweight(void)
{
    int index;
    MTC_U32 weight;

    weight = BUILTIN_HOST_WEIGHT_VALUE;
    for (index = 0; index < MAX_HOST_WEIGHT_CLASS_NUM; index++)
    {
        if (!strcmp(weight_table[index].classname, "")) continue;
        weight += weight_table[index].weight;
    }
    return weight;
}

//
//  hostweight_get_weight -
//
//  get weight from classname 
//
//

MTC_STATIC
MTC_U32
hostweight_get_weight(
    PHOST_WEIGHT_TABLE wtable,
    MTC_U32 size,
    PMTC_S8 classname)
{
    int index;

    for (index = 0; index < size; index++)
    {
        if (!strcmp(wtable[index].classname, classname))
        {
            return wtable[index].weight;
        }
    }
    return 0;
}

//
//  hostweight_update -
//
//  update wtable with newwtable.
//  report if weight has changed or removed.
//  wtable and newwtable must have same size;
//
//

MTC_STATIC
MTC_STATUS
hostweight_update(
    PHOST_WEIGHT_TABLE wtable,
    PHOST_WEIGHT_TABLE newwtable,
    MTC_U32 size)
{
    int index;
    MTC_STATUS rs = MTC_SUCCESS;
    MTC_U32    currentweight;

    // report removed weight
    for (index = 0; index < size; index ++)
    {
        if (strcmp(wtable[index].classname, ""))
        {
            if (hostweight_get_weight(newwtable, 
                                      size,
                                      wtable[index].classname) == 0)
            {
                // the classname is not found on newwtable
                // report
                log_message(MTC_LOG_INFO, "SC: class=%s weight has changed from %d to %d\n",
                            wtable[index].classname, 
                            wtable[index].weight, 0);
            }
        }
    }
    // report changed weight
    for (index = 0; index < size; index ++)
    {
        if (strcmp(newwtable[index].classname, ""))
        {
            currentweight = hostweight_get_weight(wtable, 
                                                  size,
                                                  newwtable[index].classname);
            if (currentweight != newwtable[index].weight)
            {
                // weight changed
                log_message(MTC_LOG_INFO, "SC: class=%s weight has changed from %d to %d\n",
                            newwtable[index].classname, 
                            currentweight, 
                            newwtable[index].weight);
            }
        }
    }
    // update table
    for (index = 0; index < size; index ++)
    {
        strcpy(wtable[index].classname, newwtable[index].classname);
        wtable[index].weight = newwtable[index].weight;
    }
    return rs;
}

//
//  hostweight_set_sm -
//
//  update SMOBJECT if the hostweight has changed.
//
//

MTC_STATIC
MTC_STATUS
hostweight_set_sm(void)
{
    MTC_STATUS rs = MTC_SUCCESS;
    MTC_U32 hostweight;
    HA_COMMON_OBJECT_HANDLE h_sm = NULL;
    COM_DATA_SM *sm = NULL;
    

    // calclate hostweight
    hostweight = calc_hostweight();

    // update SMOBJECT
    com_open(COM_ID_SM, &h_sm);
    com_writer_lock(h_sm, (void *)&sm);
    if (sm == NULL) 
    {
        log_internal(MTC_LOG_WARNING, "SC: (%s) sm data is NULL.\n", __func__);
        assert(FALSE);
        rs = MTC_ERROR_SC_INVALID_LOCALHOST_STATE;
    }
    else 
    {
        if (sm->weight != hostweight) 
        {
            log_message(MTC_LOG_INFO, "SC: host weight has changed from %d to %d\n",
                        sm->weight, hostweight);
            log_hostweight();
            sm->weight = hostweight;
        }
    }
    com_writer_unlock(h_sm);
    com_close(h_sm);

    return rs;
}

//
//  hostweight_reload -
//
//  reload hostweight file
//
//  reload hostweight file and update SM OBJECT
//
//  service routine for calldaemon reload_host_weight
//  This function is also called in initialize phase 1 after script service.
//
//

MTC_STATUS
hostweight_reload(void)
{
    HOST_WEIGHT_TABLE newwtable[MAX_HOST_WEIGHT_CLASS_NUM];

    int fd;
    int err_no;
    MTC_STATUS rs = MTC_SUCCESS;

    // open
    if ((rs = open_hostweight_file(&fd, &err_no)) != MTC_SUCCESS)
    {
        if (!error_reported) 
        {
            log_message(MTC_LOG_ERR, "open hostweight file error. %d (sys %d)\n",
                        rs, err_no);
            error_reported = TRUE;
        }
        return rs;
    }

    // read
    rs = read_hostweight_file(fd, &err_no, newwtable, MAX_HOST_WEIGHT_CLASS_NUM);
    if (rs != MTC_SUCCESS)
    {
        if (!error_reported) 
        {
            log_message(MTC_LOG_ERR, "open hostweight file error. %d (sys %d)\n",
                        rs, err_no);
            error_reported = TRUE;
        }
        goto error_return;
    }

    //reset error_reported
    error_reported = FALSE;

    //
    // do not close fd here.
    // lock for fd is also used for weight_table access between threads
    //

    // update weight_table
    rs = hostweight_update(weight_table, newwtable, 
                           MAX_HOST_WEIGHT_CLASS_NUM);
    if (rs != MTC_SUCCESS)
    {
        goto error_return;
    }

    // update SM OBJECT
    hostweight_set_sm();

 error_return:
    // close
    close(fd);
    return rs;
}

//
//  hostweight_initialize -
//

MTC_S32
hostweight_initialize(
    MTC_S32  phase)
{
    MTC_STATUS ret;

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "SC: hostweight_initialize(0).\n");

        // initialize tables
        ret = hostweight_initialize0();
        break;

    case 1:
        log_message(MTC_LOG_INFO, "SC: hostweight_initialize(1).\n");

        // reload weightfile
        ret = hostweight_reload();
        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "SC: hostweight_initialize(-1).\n");

        // NOTHING TODO
        ret = MTC_SUCCESS;
        break;
    }
    return ret;
}





