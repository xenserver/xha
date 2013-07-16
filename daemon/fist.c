//
//++
//
//  MODULE: fist.c
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
//      FIST.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      April 3, 2008
//
//--
//

#ifndef NDEBUG

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

#include "log.h"
#include "mtcerrno.h"
#include "fist.h"

FIST fisttab[] = {           // @ sticky
    {"init.license.fail",       0}, // License check fails

    {"sf.checksum.sticky",      1}, // checksum always fails
    {"sf.checksum.once",        0}, // checksum fails once on next read
    {"sf.ioerror.sticky",       1}, // I/O always fails
    {"sf.ioerror.once",         0}, // I/O fails once on next read or write
    {"sf.version",              1}, // version mismatch
    {"sf.genuuid",              1}, // gen uuid mismatch

    {"sf.time.=T2/2+t2",        0}, // next read or write completes T2/2+t2
    {"sf.time.=T2/2+t2.onwrite",0}, // next write completes T2/2+t2
    {"sf.time.<T2",             0}, // next read or write completes just before T2 expires
    {"sf.time.<T2.onwrite",     0}, // next write completes just before T2 expires
    {"sf.time.=T2",             0}, // next read or write completes almost at the same time T2 expires
    {"sf.time.>T2<Ws",          0}, // next read or write completes after average of T2 and Ws exipires
    {"sf.time.2Ws",             0}, // next read or write takes twice as long as Ws
    {"sf.time.forever",         1}, // all reads or writes hang until this FIST point is disabled
    {"sf.time.<T2 after write",  0},
                                    // state file thread will hang just before T2 after reading the specific section

    {"hb.receive.lostpacket",   0}, // the packet received next will be ignored and discarded (only this host sees the lost packet)
    {"hb.send.lostpacket",      0}, // the next send will not happen (all hosts see the lost packet)

    {"hb.time.=T1/2+t1.sticky", 1}, // HB send happens T1/2+t1
    {"hb.time.<T1",             0}, // next HB send or receive happens just before T1 expires
    {"hb.time.<T1.sticky",      1}, // HB send happens just before T1 expires
    {"hb.time.=T1",             0}, // next HB send or receive happens almost at the same time as T1 expires
    {"hb.time.>T1<Wh",          0}, // next HB send or receive happens after average of T1 and Wh expires
    {"hb.time.2Wh",             0}, // next HB send or receive happens far after T1 expires
    {"hb.isolate",              1}, // do not send any packets and ignore all received packets

    {"xm.time.<TXapi",          0}, // next Xapi health check delays for 3/4 TXapi
    {"xm.time.=TXapi",          0}, // next Xapi health check delays for TXapi
    {"xm.time.>TXapi",          0}, // next Xapi health check delays for twice as long as TXapi
    {"xm.xapi.error",           0}, // next Xapi health check reports an error

    {"bm.interface.down",       1}, // bonding interface reports 'down', which means
                                    // both of redundant NICs are offline.
    {"bm.interface.degraded",   1}, // bonding interface reports 'degraded', which means
                                    // one of redundant NICs is offline.

    {"wd.instance.unavailable", 1}, // watchdog instance is not avaliable
    {"wd.open",                 1}, // watchdog hypercall open error

    {"com.pthread",             0}, // general pthread error in the common object manager,
                                    // which is fatal

    {"sc.socket",               0}, // socket creation error in script service
    {"sc.pthread",              0}, // thread creation error in script service

    {"sm.sleep_in_FH3",         0}, // sleep long time in FH3
    {"sm.fence_in_FH3",         0}, // self fence in FH3
};

static int
_fist_genhas(
    char *name);

void
fist_initialize()
{
    int index;

    for (index = 0; index < sizeof(fisttab) / sizeof(struct _fisttab); index++)
    {
        fisttab[index].hash = _fist_genhas(fisttab[index].name);
    }
}

static int
_fist_genhas(
    char *name)
{
    int hash = 0;
    char *p = name;

    while (*p)
    {
        hash += *p++;
    }

    return hash;
}

//
//  _fist_set -
//
//  Enable or disable a FIST point
//

MTC_STATUS
_fist_set(
    char *name,
    MTC_BOOLEAN enabled)
{
    int index;
    int hash;

    hash = _fist_genhas(name);

    for (index = 0; index < sizeof(fisttab) / sizeof(struct _fisttab); index++)
    {
        if (fisttab[index].hash == hash && strcmp(name, fisttab[index].name) == 0)
        {
            fisttab[index].enabled = enabled;
            return MTC_SUCCESS;
        }
    }

    return MTC_ERROR_INVALID_PARAMETER;
}

//
//  _fist_on -
//
//  Returns TRUE if the specified FIST point is currently enabled
//

MTC_BOOLEAN
_fist_on(
    char *name)
{
    int index;
    int hash;
    MTC_BOOLEAN enabled;

    hash = _fist_genhas(name);

    for (index = 0; index < sizeof(fisttab) / sizeof(struct _fisttab); index++)
    {
        if (fisttab[index].hash == hash && strcmp(name, fisttab[index].name) == 0)
        {
            enabled = fisttab[index].enabled;
            if (fisttab[index].sticky == FALSE)
            {
                fisttab[index].enabled = FALSE;
            }
            return enabled;
        }
    }

    log_message(MTC_LOG_DEBUG, "FIST: fist_on is called for an invalid name \"%s\".\n", name);

    return FALSE;
}
#endif  // NDEBUG
