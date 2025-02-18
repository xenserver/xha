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
//      This module contains State-File access library routines.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      March 18, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "com.h"
#include "config.h"
#include "sm.h"
#include "xapi_mon.h"
#include "watchdog.h"
#include "xha.h"
#include "statefile.h"
#include "fist.h"

//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

//
//
//  L O C A L   D E F I N I T I O N S
//
//

//  State-File buffer

extern STATE_FILE StateFile;

//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_STATIC void
sf_FIST_delay();

MTC_STATIC void
sf_FIST_delay_on_write();

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//


extern int
sf_open(
    char *path)
{
    return open(path, (O_RDWR | O_DIRECT));
}

extern int
sf_close(
    int desc)
{
    return close(desc);
}

extern MTC_STATUS
sf_read(
    int desc,
    char *buffer,
    int length,
    off_t offset)
{
    int n;
    MTC_CLOCK start;

    assert((offset & (IOUNIT - 1)) == 0);

    if (lseek(desc, offset, SEEK_SET) < 0)
    {
        return MTC_ERROR_SF_IO_ERROR;
    }

    length = _roundup(length, IOUNIT);
    start = _getms();

    sf_FIST_delay();

    while (length)
    {
        n = read(desc, buffer, length);
        if (n < 0)
        {
            return MTC_ERROR_SF_IO_ERROR;
        }
        length -= n;
        buffer += n;
    }

    //  report the access latency to main

    sf_reportlatency(_getms() - start, FALSE);

    return MTC_SUCCESS;
}

extern MTC_STATUS
sf_write(
    int desc,
    char *buffer,
    int length,
    off_t offset)
{
    MTC_CLOCK start;

    assert((offset & (IOUNIT - 1)) == 0);

    if (lseek(desc, offset, SEEK_SET) < 0)
    {
        return MTC_ERROR_SF_IO_ERROR;
    }

    length = _roundup(length, IOUNIT);
    start = _getms();

    sf_FIST_delay();
    sf_FIST_delay_on_write();

    if (write(desc, buffer, length) < 0)
    {
        return MTC_ERROR_SF_IO_ERROR;
    }

    //  report the access latency to main

    sf_reportlatency(_getms() - start, TRUE);

    return MTC_SUCCESS;
}

//
//  sf_readglobal - Read global section of the State-File.
//               Signature and checksum are validated here.
//

extern MTC_STATUS
sf_readglobal(
    int desc,
    PSF_GLOBAL_SECTION pglobal,
    MTC_UUID expected_uuid)
{
    MTC_U32 sum;
    MTC_STATUS status;

    //  Read the global section

    status = sf_read(desc,
                     (char *)&pglobal->data,
                     sizeof(pglobal->data),
                     _struct_offset(STATE_FILE, global.data));

    if (status != MTC_SUCCESS)
    {
        return MTC_ERROR_SF_IO_ERROR;
    }

    //  Validate the global section

    if (pglobal->data.sig != sf_create_sig(SIG_SF_GLOBAL) ||
        pglobal->data.sig_inv != sf_create_inverted_sig(SIG_SF_GLOBAL))
    {
        return MTC_ERROR_SF_CORRUPTION;
    }

    sum = sf_checksum(
            &pglobal->data.version, &pglobal->data.end_marker);

    if (sum != pglobal->data.checksum)
    {
        return MTC_ERROR_SF_CORRUPTION;
    }

    if (pglobal->data.version != SF_VERSION)
    {
        return MTC_ERROR_SF_VERSION_MISMATCH;
    }

    if (expected_uuid && UUID_comp(pglobal->data.gen_uuid, expected_uuid) != 0)
    {
        return MTC_ERROR_SF_GEN_UUID;
    }

    return MTC_SUCCESS;
}

//
//  sf_readhostspecific -
//              Read host specific element of the State-File.
//              Signature and checksum are validated here.
//

extern MTC_STATUS
sf_readhostspecific(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost)
{
    MTC_U32 sum;
    MTC_STATUS status;

    status = sf_read(desc,
                (char *)phost,
                sizeof(phost->data),
                _struct_offset(STATE_FILE, host[host_index].data));

    if (status != MTC_SUCCESS)
    {
        return MTC_ERROR_SF_IO_ERROR;
    }

    //  Validate the global section

    if (phost->data.sig != sf_create_sig(SIG_SF_HOST) ||
        phost->data.sig_inv != sf_create_inverted_sig(SIG_SF_HOST))
    {
        return MTC_ERROR_SF_CORRUPTION;
    }

    sum = sf_checksum(&phost->data.sequence, &phost->data.end_marker);

    if (sum != phost->data.checksum)
    {
        return MTC_ERROR_SF_CORRUPTION;
    }

    return MTC_SUCCESS;
}

//
//  sf_writeglobal - Write global section of the State-File.
//                   Constants and checksum are set here.
//

extern MTC_STATUS
sf_writeglobal(
    int desc,
    PSF_GLOBAL_SECTION pglobal)
{
    pglobal->data.sig = sf_create_sig(SIG_SF_GLOBAL);
    pglobal->data.sig_inv = sf_create_inverted_sig(SIG_SF_GLOBAL);
    pglobal->data.version = SF_VERSION;
    pglobal->data.length_global = LENGTH_GLOBAL;
    pglobal->data.length_host_specfic = LENGTH_HOST_SPECIFIC;
    pglobal->data.max_hosts = MAX_HOST_NUM;
    pglobal->data.end_marker = SIG_END_MARKER_GLOBAL;

    pglobal->data.checksum =
            sf_checksum(&pglobal->data.version, &pglobal->data.end_marker);

    //  Write the global section

    return sf_write(desc,
                (char *)&pglobal->data,
                sizeof(pglobal->data),
                _struct_offset(STATE_FILE, global.data));
}

//
//  sf_writehostspecific -
//              Write host specific element of the State-File.
//              Signature and checksum are set here.
//

extern MTC_STATUS
sf_writehostspecific(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost)
{
    phost->data.sig = sf_create_sig(SIG_SF_HOST);
    phost->data.sig_inv = sf_create_inverted_sig(SIG_SF_HOST);
    phost->data.end_marker = SIG_END_MARKER_HOST;

    phost->data.checksum =
            sf_checksum(&phost->data.sequence, &phost->data.end_marker);

    return sf_write(desc,
                    (char *)phost,
                    sizeof(phost->data),
                    _struct_offset(STATE_FILE, host[host_index].data));

}

extern MTC_U32
sf_checksum(
    MTC_U32 *p,
    MTC_U32 *end)
{
    MTC_U32 sum = 0;

    assert((((uintptr_t)p) & 3) == 0 && (((uintptr_t)end) & 3) == 0);

    while (p < end)
    {
        sum += *p++;
    }

    assert(p == end);

    return sum;
}

//
//  sf_sleep -
//
//  Sleep milliseconds
//

extern void
sf_sleep(
    MTC_U32 msec)
{
    struct timespec t, t_rem;

    t = t_rem = mstots(msec);
    while (nanosleep(&t, &t_rem)) t = t_rem;
}

//
//  FIST points
//

MTC_STATIC void
sf_FIST_delay()
{
    MTC_S32 target_delay;

    if (fist_on("sf.time.forever"))
    {
        log_message(MTC_LOG_DEBUG,
                    "SF(FIST): SF thread is now hung for \"sf.time.forever\"\n");

        while (fist_on("sf.time.forever"))
        {
            sf_sleep(1000);
        }

        log_message(MTC_LOG_DEBUG,
                    "SF(FIST): SF thread is now resuming its operation\n");
        return;
    }

    if (fist_on("sf.time.<T2"))
    {
        if ((target_delay = _T2 * 1000 * 3 / 4) == 0)
        {
            target_delay = 1000;
        }
    }
    else if (fist_on("sf.time.=T2"))
    {
        target_delay = _T2 * 1000;
    }
    else if (fist_on("sf.time.>T2<Ws"))
    {
        target_delay = (_Ws + _T2) * 1000 / 2;
    }
    else if (fist_on("sf.time.2Ws"))
    {
        target_delay = 2 * _Ws * 1000;
    }
    else if (fist_on("sf.time.=T2/2+t2"))
    {
        target_delay = _T2 * 1000 / 2;
    }
    else
    {
        return; // no FIST is enabled
    }

    log_message(MTC_LOG_DEBUG,
                "SF(FIST): sf_read/write delay %d ms\n", target_delay);

    sf_sleep(target_delay);

    log_message(MTC_LOG_DEBUG,
                "SF(FIST): sf_read/write delay is done\n");
}

//
//  FIST points on write
//

MTC_STATIC void
sf_FIST_delay_on_write()
{
    MTC_S32 target_delay;

    if (fist_on("sf.time.<T2.onwrite"))
    {
        if ((target_delay = _T2 * 1000 * 3 / 4) == 0)
        {
            target_delay = 1000;
        }
    }
    else if (fist_on("sf.time.=T2/2+t2.onwrite"))
    {
        target_delay = _T2 * 1000 / 2;
    }
    else
    {
        return; // no FIST is enabled
    }

    log_message(MTC_LOG_DEBUG,
                "SF(FIST): sf_write delay %d ms\n", target_delay);

    sf_sleep(target_delay);

    log_message(MTC_LOG_DEBUG,
                "SF(FIST): sf_write delay is done\n");
}
