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
//      This module reads and writes when HA daemon is not present.
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

static MTC_STATUS
global_init_state(
    int sf);

static MTC_STATUS
global_read_and_set_state(
    int sf,
    MTC_U32 newstate);

static MTC_STATUS
host_specific_ex(
    int sf,
    MTC_BOOLEAN excluded);

//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//

//
//  main
//
//  writestatefile [setinit|setactive|setinvalid|setex|clearex] config-file-path
//

int
main(
    int argc,
    char *argv[],
    char *envp[])
{
    MTC_STATUS status;
    int sf;

    if (argc != 3)
    {
        exit(MTC_EXIT_INVALID_PARAMETER);
    }

    if ((status = interpret_config_file(argv[2], &ha_config)) < 0)
    {
        exit(status_to_exit(status));
    }

    bzero(&StateFile, sizeof(StateFile));

    if ((sf = sf_open(_sf_path)) < 0)
    {
        fprintf(stderr, "can not open %s\n", _sf_path);
        exit(status_to_exit(MTC_ERROR_SF_OPEN));
    }

    if (strcmp(argv[1], "setinit") == 0)
    {
        status = global_init_state(sf);
    }
    else if (strcmp(argv[1], "setactive") == 0)
    {
        status = global_read_and_set_state(sf, SF_STATE_ACTIVE);
    }
    else if (strcmp(argv[1], "setinvalid") == 0)
    {
        status = global_read_and_set_state(sf, SF_STATE_INVALID);
    }
    else if (strcmp(argv[1], "setex") == 0)
    {
        status = host_specific_ex(sf, TRUE);
    }
    else if (strcmp(argv[1], "clearex") == 0)
    {
        status = host_specific_ex(sf, FALSE);
    }
    else
    {
        status = MTC_ERROR_INVALID_PARAMETER;
    }

    sf_close(sf);

    exit(status_to_exit(status));
}

static MTC_STATUS
global_init_state(
    int sf)
{
    int host;
    MTC_STATUS status;

    //  clear all

    status = sf_write(sf, (char *)&StateFile, sizeof(StateFile), (off_t)0);

    if (status != MTC_SUCCESS)
    {
        return status;
    }

    //  build the global section maually

    UUID_cpy(StateFile.global.data.gen_uuid, _gen_UUID);
    StateFile.global.data.pool_state = SF_STATE_INIT;
    UUID_cpy(StateFile.global.data.master, UUID_zero);
    StateFile.global.data.config_hosts = _num_host;
    // StateFile.global.data.config = ha_config.common;

    status = sf_writeglobal(sf, &StateFile.global);

    if (status != MTC_SUCCESS)
    {
        return status;
    }

    //  Clear host-specific section

    for (host = 0; host < MAX_HOST_NUM; host++)
    {
        status = sf_writehostspecific(sf, host, &StateFile.host[host]);
        if (status != MTC_SUCCESS)
        {
            fprintf(stderr, "can not write %s for the host %d\n", _sf_path, host);
            return status;
        }
    }

    return MTC_SUCCESS;
}

static MTC_STATUS
global_read_and_set_state(
    int sf,
    MTC_U32 newstate)
{
    MTC_STATUS status;

    status = sf_readglobal(sf, &StateFile.global, NULL);

    if (status != MTC_SUCCESS)
    {
        return status;
    }

    StateFile.global.data.pool_state = newstate;

    return sf_writeglobal(sf, &StateFile.global);
}

static MTC_STATUS
host_specific_ex(
    int sf,
    MTC_BOOLEAN excluded)
{
    MTC_STATUS status;

    status = sf_readglobal(sf, &StateFile.global, NULL);

    if (status != MTC_SUCCESS)
    {
        return status;
    }

    status = sf_readhostspecific(sf, _my_index, &StateFile.host[_my_index]);

    if (status != MTC_SUCCESS)
    {
        return status;
    }

    if (StateFile.global.data.pool_state != SF_STATE_ACTIVE)
    {
        return MTC_ERROR_SM_INVALID_POOL_STATE;
    }

    StateFile.host[_my_index].data.excluded = (excluded? 1: 0);

    return sf_writehostspecific(sf, _my_index, &StateFile.host[_my_index]);
}

void
sf_reportlatency(
    MTC_CLOCK latency,
    MTC_BOOLEAN write)
{
    // void
}
