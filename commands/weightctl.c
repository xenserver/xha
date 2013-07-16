//  MODULE: weightctl.c

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
//      Xen HA host weight control command.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Nov 12, 2008
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
#include <fcntl.h>

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
#include "hostweight.h"

//
//
//  S T A T I C   F U N C T I O N   P R O T O T Y P E S
//
//


//
//
//  L O C A L   D E F I N I T I O N S
//
//

HOST_WEIGHT_TABLE wtable[MAX_HOST_WEIGHT_CLASS_NUM];

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
//      weight_set
//
//  DESCRIPTION:
//
//      service routine for "weightctl set"
//
//  FORMAL PARAMETERS:
//
//      classname
//      weight
//          
//  RETURN VALUE:
//
//      MTC_EXIT_SUCCESS
//      MTC_EXIT_INVALID_PARAMETER too many classes are defined
//      MTC_EXIT_SYSTEM_ERROR      error in open, lock, read, write
//
//
//  ENVIRONMENT:
//
//      none
//
//

int 
weight_set(PMTC_S8 classname, MTC_U32 weight)
{
    int fd;
    int err_no;

    MTC_STATUS rs = MTC_SUCCESS;
    MTC_S8 cname[MAX_HOST_WEIGHT_CLASSNAME_LEN];
    PMTC_S8 chr;

    // check weight value
    //   max value
    if (weight > MAX_HOST_WEIGHT_VALUE)
    {
        return MTC_EXIT_INVALID_PARAMETER;
    }

    // check class name

    //   check builtin
    if (!strcmp(classname, BUILTIN_HOST_WEIGHT_CLASSNAME))
    {
        return MTC_EXIT_INVALID_PARAMETER;
    }

    //   check isgraph
    for (chr = classname; *chr != '\0'; chr++)
    {
        if (!isgraph(*chr))
        {
            return MTC_EXIT_INVALID_PARAMETER;
        }
    }

    //   truncate long classname
    if (strlen(classname) >= MAX_HOST_WEIGHT_CLASSNAME_LEN)
    {
        strncpy(cname, classname, MAX_HOST_WEIGHT_CLASSNAME_LEN - 1);
        cname[MAX_HOST_WEIGHT_CLASSNAME_LEN - 1] = '\0';
    }
    else
    {
        strcpy(cname, classname);
    }

    // open wfile
    if ((rs = open_hostweight_file(&fd, &err_no)) != MTC_SUCCESS)
    {
        return status_to_exit(rs);
    }

    // read
    rs = read_hostweight_file(fd, &err_no, wtable, MAX_HOST_WEIGHT_CLASS_NUM);
    if (rs != MTC_SUCCESS)
    {
        goto error_return;
    }
    
    // update
    rs = set_hostweight_table(wtable, MAX_HOST_WEIGHT_CLASS_NUM, 
                              cname, weight);
    if (rs != MTC_SUCCESS)
    {
        goto error_return;
    }

    // write
    rs = write_hostweight_file(fd, &err_no, wtable, MAX_HOST_WEIGHT_CLASS_NUM);
    if (rs != MTC_SUCCESS)
    {
        goto error_return;
    }

 error_return:
    close(fd);
    return status_to_exit(rs);
}

//
//
//  NAME:
//
//      printhelp
//
//  DESCRIPTION:
//
//      service routine for "weightctl help"
//
//  FORMAL PARAMETERS:
//
//          
//  RETURN VALUE:
//
//      MTC_EXIT_SUCCESS
//
//  ENVIRONMENT:
//
//      none
//
//

void printhelp(void)
{
    printf("usage: weightctl <command> [args]\n");
    printf("  command list\n");
    printf("    set <class> <weight>\n");
    printf("    help\n");
}


//
//
//  NAME:
//
//      main
//
//  DESCRIPTION:
//
//      main of weightctl
//
//  FORMAL PARAMETERS:
//
//      set classname weight
//      help
//          
//  RETURN VALUE:
//
//      MTC_EXIT_SUCCESS - success the call
//      MTC_EXIT_INVALID_PARAMETER - improper parameters / too many classes are defined
//      MTC_EXIT_SYSTEM_ERROR      - weight file error
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
    if (argc < 2)
    {
        return MTC_EXIT_INVALID_PARAMETER;  // invalid argument;
    }

    if (!strcmp(argv[1], "help"))
    {
        printhelp();
        return MTC_EXIT_SUCCESS;
    }
    if (!strcmp(argv[1], "set"))
    {
        if (argc != 4)
        {
            return MTC_EXIT_INVALID_PARAMETER;  // invalid argument;
        }
        return weight_set(argv[2], atol(argv[3]));
    }
    return MTC_EXIT_INVALID_PARAMETER;
}
