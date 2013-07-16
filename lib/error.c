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
//      This module contains a code to translate an error code to
//      exit status.
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      March 24, 2008
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
#define MTC_NEED_ERROR_TABLE
#include "mtcerrno.h"
#include "log.h"
#include "com.h"
#include "config.h"
#include "sm.h"
#include "xapi_mon.h"
#include "watchdog.h"
#include "xha.h"
#include "statefile.h"

int
status_to_exit(
    MTC_STATUS status)
{
    int index;
    int exit_code = -1;

    for (index = 0; index < sizeof(mtc_errtable) / sizeof(mtc_errtable[0]); index++)
    {
        if (mtc_errtable[index].status == status)
        {
            if ((exit_code = mtc_errtable[index].exit_code) == -1)
            {
                break;
            }

            if (exit_code == 0)
            {
                exit_code = (int)status;
            }

            return exit_code;
        }
    }

    assert(exit_code != -1);
    return -1;
}

char *
status_to_message(
    MTC_STATUS status)
{
    int index;

    for (index = 0; index < sizeof(mtc_errtable) / sizeof(mtc_errtable[0]); index++)
    {
        if (mtc_errtable[index].status == status)
        {
            return mtc_errtable[index].message;
        }
    }

    return "";
}
