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
//      This is bonding status check module
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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "log.h"
#include "mtcerrno.h"
#include "bond_mon.h"
#include "com.h"
#include "config.h"
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

//
// In the kernel source (drivers/net/bonding/bond_main.c),
// Bonding driver writes its status in /proc/net/bonding/bondX,
// MII Status is printed like "MII Status: %s\n", ok? "up": "down", and
// The string "Slave Interface:" is only appeared just before printing
// the slave status.
// 

#define PATH_PROC_NET_BONDING   "/proc/net/bonding/"
#define MII_STATUS_LINE         "MII Status:"
#define MII_STATUS_UP           "up"
#define INTERFACE_LINE          "Slave Interface:"


//
//
//  L O C A L   D E F I N I T I O N S
//
//

#define BOND_CHECK_INTERVAL 10

static HA_COMMON_OBJECT_HANDLE bm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;

static struct {
    MTC_BOOLEAN         terminate;
} bmvar = {
    .terminate = FALSE,
};


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC void *
bm(
    void *ignore);

MTC_STATIC MTC_BOOLEAN
bm_check_fist(
    PMTC_BOND_STATUS    pstatus);

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//

//
//
//  NAME:
//
//      chop
//
//  DESCRIPTION:
//
//      remove '\n'
//
//  FORMAL PARAMETERS:
//
//      line - string
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//
//

void chop(MTC_S8 *line)
{
    MTC_U32 len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
    {
        line[len - 1] = '\0';
    }
}



//
//++
//
//  NAME:
//
//      check_bonding_status
//
//  DESCRIPTION:
//
//      Check bonding status.
//
//  FORMAL PARAMETERS:
//
//
//          
//  RETURN VALUE:
//
//      BOND_STATUS_NOBOND:     the interface is not bonding
//      BOND_STATUS_ERR:        bonding interface is in error state
//      BOND_STATUS_DEGRADED:   bonding interface is in normal state, but
//                              one or more slave interfaces are in error state
//      BOND_STATUS_NOERR:      bonding interface and its all slave interfaces
//                              are good
//
//  ENVIRONMENT:
//
//      This function is not thread safe.
//
//
//--
//

MTC_BOND_STATUS
check_bonding_status()
{
    FILE            *fp;
    MTC_S8          bonding_status[PATH_MAX] = PATH_PROC_NET_BONDING;
    MTC_S8          device[PATH_MAX];
    MTC_S8          line[256];
    MTC_BOND_STATUS ret = BOND_STATUS_NOERR;
    static MTC_BOND_STATUS lastret = BOND_STATUS_NOERR;

    if (bm_check_fist(&ret))
    {
        return ret;
    }

    if (!strcmp(_hb_pif, ""))
    {
        log_message(MTC_LOG_INFO, "BM: HeartbeatPhysicalInterface is not configured.\n");
        return BOND_STATUS_NOBOND;
    }

    strcpy(device, _hb_pif);
    strcat(bonding_status, _hb_pif);

    if ((fp = fopen(bonding_status, "r")) == NULL)
    {
        log_message(MTC_LOG_WARNING,
            "BM: cannot open bonding status file (%s). (%d)\n", bonding_status, errno);
        assert(errno == ENOENT);
        return BOND_STATUS_NOBOND;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        chop(line);
        if (!strncasecmp(line, MII_STATUS_LINE, strlen(MII_STATUS_LINE)))
        {
            if (strcasestr(&line[strlen(MII_STATUS_LINE)],
                           MII_STATUS_UP) == NULL)
            {
                //
                // detect bonding error
                // log_message only status has been changed.
                // 

                if ((lastret == BOND_STATUS_NOERR) ||
                    (lastret == BOND_STATUS_DEGRADED && !strcmp(device, _hb_pif)))
                {
                    log_message(MTC_LOG_DEBUG,
                                "BM: %s (interface %s)\n", line, device);
                    
                    //
                    // force lastret to BOND_STATUS_NOERR 
                    // to report all interface error
                    //

                    lastret = BOND_STATUS_NOERR;

                }

                if (!(ret == BOND_STATUS_ERR || ret == BOND_STATUS_DEGRADED))
                {
                    ret = (!strcmp(device, _hb_pif))?
                            BOND_STATUS_ERR: BOND_STATUS_DEGRADED;
                }
            }
            else 
            {
                if (lastret == BOND_STATUS_ERR && !strcmp(device, _hb_pif))
                {
                    //
                    // recover from ERROR
                    // force lastret to BOND_STATUS_NOERR 
                    // to report all interface error
                    // 

                    lastret = BOND_STATUS_NOERR;
                    
                }

            }
        }
        else if (!strncasecmp(line, INTERFACE_LINE, strlen(INTERFACE_LINE)))
        {
            strcpy(device, line + strlen(INTERFACE_LINE));
        }
    }

    fclose(fp);

    lastret = ret;
    return ret;
}


MTC_S32
bm_initialize(
    MTC_S32  phase)
{
    static pthread_t        bm_thread = 0;
    MTC_S32                 ret;

    assert(-1 <= phase && phase <= 1);

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "BM: bm_initialize(0).\n");

        {
            COM_DATA_BM bm = {
                .status = FALSE,
                .mtc_bond_status = BOND_STATUS_NOERR};

            ret = com_create(COM_ID_BM, &bm_object, sizeof(COM_DATA_BM), &bm);
            if (ret != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "BM: cannot create COM object. (%d)\n", ret);
                bm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
            }
        }
        break;

    case 1:
        log_message(MTC_LOG_INFO, "BM: bm_initialize(1).\n");

        // start heartbeat thread
        bmvar.terminate = FALSE;
        ret = pthread_create(&bm_thread, xhad_pthread_attr, bm, NULL);
        if (ret)
        {
            log_internal(MTC_LOG_ERR, "BM: pthread_create failed. (%d)\n", ret);
            ret = MTC_ERROR_BM_PTHREAD;
        }
        else 
        {
            ret = MTC_SUCCESS;
        }

        break;

    case -1:
    default:
        log_message(MTC_LOG_INFO, "BM: bm_initialize(-1).\n");

        if (bm_thread)
        {
            bmvar.terminate = TRUE;

#if 0
            ret = pthread_join(bm_thread, NULL);
            if (ret)
            {
                log_message(MTC_LOG_WARNING,
                    "BM: cannot terminate bond thread. (%d)\n", ret);
                ret = pthread_kill(bm_thread, SIGKILL);
                if (ret)
                {
                    log_internal(MTC_LOG_ERR, "BM: cannot kill bond thread. (%d)\n", ret);
                    ret = MTC_ERROR_SYSTEM_LEVEL_FAILURE;
                }
            }
#endif
        }
#if 0
        com_close(bm_object);
        bm_object = HA_COMMON_OBJECT_INVALID_HANDLE_VALUE;
#endif
        ret = MTC_ERROR_INVALID_PARAMETER;

        break;
    }

    return ret;
}


MTC_STATIC void *
bm(
    void *ignore)
{
    struct timespec         ts, ts_rem;
    static MTC_BOND_STATUS  bond_status = BOND_STATUS_NOERR;
    PCOM_DATA_BM            pbm;

    log_message(MTC_LOG_INFO, "BM: thread ID: %ld.\n", syscall(SYS_gettid));
    do
    {
        log_maskable_debug_message(TRACE, "BM: bonding monitor thread activity log.\n");

        ts = ts_rem = mstots(BOND_CHECK_INTERVAL * ONE_SEC);
        while (nanosleep(&ts, &ts_rem)) ts = ts_rem;

        switch (check_bonding_status())
        {
        case BOND_STATUS_NOBOND:
            com_writer_lock(bm_object, (void **) &pbm);
            pbm->status = FALSE;
            pbm->mtc_bond_status = BOND_STATUS_NOBOND;
            com_writer_unlock(bm_object);

            // this is not bonding configuration, no need to cycle check.
            log_message(MTC_LOG_INFO,
                        "BM: this is not bonded. Terminating bonding thread.\n");
            bond_status = BOND_STATUS_NOBOND;
            bmvar.terminate = TRUE;
            break;

        case BOND_STATUS_NOERR:
            com_writer_lock(bm_object, (void **) &pbm);
            pbm->status = FALSE;
            pbm->mtc_bond_status = BOND_STATUS_NOERR;
            com_writer_unlock(bm_object);

            if (bond_status != BOND_STATUS_NOERR)
            {
                log_message(MTC_LOG_NOTICE,
                    "BM: bonding status has changed to GOOD.\n");
                bond_status = BOND_STATUS_NOERR;
            }
            break;

        case BOND_STATUS_DEGRADED:
            com_writer_lock(bm_object, (void **) &pbm);
            pbm->status = TRUE;
            pbm->mtc_bond_status = BOND_STATUS_DEGRADED;
            com_writer_unlock(bm_object);

            if (bond_status != BOND_STATUS_DEGRADED)
            {
                log_both((bond_status == BOND_STATUS_ERR)?
                            MTC_LOG_NOTICE: MTC_LOG_WARNING,
                    "BM: bonding status has changed to DEGRADED.\n");
                bond_status = BOND_STATUS_DEGRADED;
            }
            break;

        case BOND_STATUS_ERR:
        default:
            com_writer_lock(bm_object, (void **) &pbm);
            pbm->status = TRUE;
            pbm->mtc_bond_status = BOND_STATUS_ERR;
            com_writer_unlock(bm_object);

            if (bond_status != BOND_STATUS_ERR)
            {
                log_message(MTC_LOG_ERR,
                    "BM: bonding status has changed to ERROR.\n");
                bond_status = BOND_STATUS_ERR;
            }
            break;
        }
    } while (!bmvar.terminate);

    return NULL;
}


MTC_STATIC MTC_BOOLEAN
bm_check_fist(
    PMTC_BOND_STATUS    pstatus)
{
    MTC_BOOLEAN fiston = TRUE;

    if (fist_on("bm.interface.down"))
    {
        *pstatus = BOND_STATUS_ERR;
    }
    else if (fist_on("bm.interface.degraded"))
    {
        *pstatus = BOND_STATUS_DEGRADED;
    }
    else
    {
        fiston = FALSE;
        *pstatus = BOND_STATUS_NOERR;
    }

    return fiston;
}
