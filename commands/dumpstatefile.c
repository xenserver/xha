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
//      This module dumps State-File contents
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      April 1, 2008
//
//   

//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

HA_CONFIG ha_config;

#define INV_SIG     (1 << 0)
#define INV_VER     (1 << 1)
#define INV_GUUID   (1 << 2)
#define INV_CSUM    (1 << 3)

//
//  Utility macros
//

//  State-File buffer

// int x __attribute__ ((aligned (16))) = 0;
static STATE_FILE StateFile __attribute__ ((aligned (IOALIGN)));

//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

char *
sig_string(
    MTC_U32 sig);

void
dump_global(
    PSF_GLOBAL_SECTION pglobal,
    int invmask);

void
dump_host(
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost,
    int invmask);

char *
uuid_string(
    MTC_UUID uuid);

char *
hostmap_string(
    MTC_HOSTMAP hostmap);

static MTC_STATUS
sf_readglobal_nocheck(
    int desc,
    PSF_GLOBAL_SECTION pglobal,
    MTC_UUID expected_uuid,
    int *pinvmask);

static MTC_STATUS
sf_readhostspecific_nocheck(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost,
    int *pinvmask);

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//

//
//  main
//
//  dumpstatefile config-file-path
//

int
main(
    int argc,
    char *argv[],
    char *envp[])
{
    MTC_STATUS status;
    int sf;
    int host_index, invmask;

    if (argc != 2)
    {
        exit(MTC_EXIT_INVALID_PARAMETER);
    }

    if ((status = interpret_config_file(argv[1], &ha_config)) < 0)
    {
        exit(status_to_exit(status));
    }

    bzero(&StateFile, sizeof(StateFile));

    if ((sf = sf_open(_sf_path)) < 0)
    {
        fprintf(stderr, "can not open %s\n", _sf_path);
        exit(status_to_exit(MTC_ERROR_SF_OPEN));
    }

    status = sf_readglobal_nocheck(sf, &StateFile.global, _gen_UUID, &invmask);

    if (status != MTC_SUCCESS)
    {
        fprintf(stderr, "can not read the global section (%d)\n", status);
        exit(status_to_exit(status));
    }

    dump_global(&StateFile.global, invmask);
    printf("---------------------------\n");

    for (host_index = 0; _is_configured_host(host_index); host_index++)
    {
        status = sf_readhostspecific_nocheck(sf, host_index, &StateFile.host[host_index], &invmask);

        if (status != MTC_SUCCESS)
        {
            fprintf(stderr, "can not read the host specific element for index %d (%d)\n",
                    host_index,
                    status);
            exit(status_to_exit(status));
        }
        
        dump_host(host_index, &StateFile.host[host_index], invmask);
        printf("---------------------------\n");
    }

    sf_close(sf);

    exit(status_to_exit(status));
}

#define HEADER(inv)      printf((inv)? "**": "  ");

void
dump_global(
    PSF_GLOBAL_SECTION pglobal,
    int invmask)
{
    HEADER(invmask & INV_SIG);  printf("global.sig = %s\n", sig_string(pglobal->data.sig));
    HEADER(invmask & INV_SIG);  printf("global.sig_inv = 0x%x\n", pglobal->data.sig_inv);
    HEADER(invmask & INV_CSUM); printf("global.checksum = 0x%x\n", pglobal->data.checksum);
    HEADER(invmask & INV_VER);  printf("global.version = %d\n", pglobal->data.version);

    HEADER(0); printf("global.length_global = %d\n", pglobal->data.length_global);
    HEADER(0); printf("global.length_host_specfic = %d\n", pglobal->data.length_host_specfic);
    HEADER(0); printf("global.max_hosts = %d\n", pglobal->data.max_hosts);

    HEADER(invmask &INV_GUUID); printf("global.gen_uuid = %s\n", uuid_string(pglobal->data.gen_uuid));

    HEADER(0); printf("global.pool_state = %d\n", pglobal->data.pool_state);
    HEADER(0); printf("global.master = %s\n", uuid_string(pglobal->data.master));
    HEADER(0); printf("global.config_hosts = %d\n", pglobal->data.config_hosts);
}

void
dump_host(
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost,
    int invmask)
{
    int i;

    HEADER(invmask & INV_SIG);
    printf("host[%2d].sig = %s\n", host_index, sig_string(phost->data.sig));

    HEADER(invmask & INV_SIG);
    printf("host[%2d].sig_inv = 0x%x\n", host_index, phost->data.sig_inv);

    HEADER(invmask & INV_CSUM);
    printf("host[%2d].checksum = 0x%x\n", host_index, phost->data.checksum);
    HEADER(0); printf("host[%2d].sequence = %d\n", host_index, phost->data.sequence);
    HEADER(0); printf("host[%2d].host_index = %d\n", host_index, phost->data.host_index);
    HEADER(0); printf("host[%2d].host_uuid = %s\n", host_index, uuid_string(phost->data.host_uuid));
    HEADER(0); printf("host[%2d].excluded = %d\n", host_index, phost->data.excluded);
    HEADER(0); printf("host[%2d].starting = %d\n", host_index, phost->data.starting);

    for (i = 0; _is_configured_host(i); i++)
    {
        HEADER(0); printf("host[%2d].since_last_hb_receipt[%2d] = %d\n", host_index, i, phost->data.since_last_hb_receipt[i]);
    }

    for (i = 0; _is_configured_host(i); i++)
    {
        HEADER(0); printf("host[%2d].since_last_sf_update[%2d] = %d\n", host_index, i, phost->data.since_last_sf_update[i]);
    }

    HEADER(0); printf("host[%2d].current_liveset = %s\n", host_index, hostmap_string(phost->data.current_liveset));
    HEADER(0); printf("host[%2d].proposed_liveset = %s\n", host_index, hostmap_string(phost->data.proposed_liveset));
    HEADER(0); printf("host[%2d].hbdomain = %s\n", host_index, hostmap_string(phost->data.hbdomain));
    HEADER(0); printf("host[%2d].sfdomain = %s\n", host_index, hostmap_string(phost->data.sfdomain));
    HEADER(0); printf("host[%2d].lock_request = %d\n", host_index, phost->data.lock_request);
    HEADER(0); printf("host[%2d].lock_grant = %s\n", host_index, hostmap_string(phost->data.lock_grant));

    HEADER(0); printf("host[%2d].since_xapi_restart_first_attempted = %d\n", host_index, phost->data.since_xapi_restart_first_attempted);
    HEADER(0); printf("host[%2d].sequence = %d\n", host_index,  phost->data.sequence);
}

char *
sig_string(
    MTC_U32 sig)
{
    static char buf[64];

    sprintf(buf, "0x%x(%c%c%c%c)",
                sig,
                ((sig >>  0) & 0xff),
                ((sig >>  8) & 0xff),
                ((sig >> 16) & 0xff),
                ((sig >> 24) & 0xff));

    return buf;
}

char *
uuid_string(
    MTC_UUID uuid)
{
    static char buf[MTC_UUID_SIZE + 1];

    memcpy(buf, uuid, MTC_UUID_SIZE);
    buf[sizeof(buf) - 1] = '\0';

    return buf;
}

char *
hostmap_string(
    MTC_HOSTMAP hostmap)
{
    static char buf[MAX_HOST_NUM * 2];
    int i, count;

    strcpy(buf, "bitmap l->h(");

    count = 0;
    for (i = 0; _is_configured_host(i); i++)
    {
        if (++count >= 4)
        {
            count = 0;
            strcat(buf, "-");
        }
        strcat(buf, (MTC_HOSTMAP_ISON(hostmap, i)? "1": "0"));
    }

    strcat(buf, ")");

    return buf;
}

void
sf_reportlatency(
    MTC_CLOCK latency,
    MTC_BOOLEAN write)
{
    // void
}

static MTC_STATUS
sf_readglobal_nocheck(
    int desc,
    PSF_GLOBAL_SECTION pglobal,
    MTC_UUID expected_uuid,
    int *pinvmask)
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

    *pinvmask = 0;
    
    //  Validate the global section

    if (pglobal->data.sig != sf_create_sig(SIG_SF_GLOBAL) ||
        pglobal->data.sig_inv != sf_create_inverted_sig(SIG_SF_GLOBAL))
    {
        *pinvmask |= INV_SIG;
    }

    sum = sf_checksum(
            &pglobal->data.version, &pglobal->data.end_marker);

    if (sum != pglobal->data.checksum)
    {
        *pinvmask |= INV_CSUM;
    }

    if (pglobal->data.version != SF_VERSION)
    {
        *pinvmask |= INV_VER;
    }

    if (UUID_comp(expected_uuid, UUID_zero) != 0 &&
        UUID_comp(pglobal->data.gen_uuid, expected_uuid) != 0)
    {
        *pinvmask |= INV_GUUID;
    }

    return MTC_SUCCESS;
}

//
//  sf_readhostspecific -
//              Read host specific element of the State-File.
//              Signature and checksum are validated here.
//

static MTC_STATUS
sf_readhostspecific_nocheck(
    int desc,
    int host_index,
    PSF_HOST_SPECIFIC_SECTION phost,
    int *pinvmask)
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

    *pinvmask = 0;

    //  Validate the global section

    if (phost->data.sig != sf_create_sig(SIG_SF_HOST) ||
        phost->data.sig_inv != sf_create_inverted_sig(SIG_SF_HOST))
    {
        *pinvmask |= INV_SIG;
    }

    sum = sf_checksum(&phost->data.sequence, &phost->data.end_marker);

    if (sum != phost->data.checksum)
    {
        *pinvmask |= INV_CSUM;
    }

    return MTC_SUCCESS;
}

