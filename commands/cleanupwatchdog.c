//  MODULE: cleanupwatchdog.c

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
//      Apr 08, 2008
//
//   


//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//


#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>  // for mlock
#include <pthread.h>
#include <assert.h>
#include <unistd.h>


#define __XEN_TOOLS__
#include <xen/xen.h>
#include <xen/version.h>
#include <xen/sysctl.h>
#if XEN_SYSCTL_INTERFACE_VERSION < 4
#include <xen/linux/privcmd.h>
#else
#include <xen/sys/privcmd.h>
#endif
#include <xen/sched.h>


//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "watchdog.h"


//
//
//  S T A T I C   F U N C T I O N   P R O T O T Y P E S
//
//


////
//
//
//  I N T E R N A L   F U N C T I O N
//
//
////


#define PRIVCMD_PATH "/proc/xen/privcmd"
#define MAX_WATCHDOG_INSTANCE 8


#define XC_PAGE_SHIFT           12
#undef PAGE_SHIFT
#undef PAGE_SIZE
#undef PAGE_MASK
#define PAGE_SHIFT              XC_PAGE_SHIFT
#define PAGE_SIZE               (1UL << PAGE_SHIFT)
#define PAGE_MASK               (~(PAGE_SIZE-1))
#define WD_FENCE                (1)

#define BUFFER_SIZE             128


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
//
//  NAME:
//
//      lock_pages
//      unlock_pages
//
//  DESCRIPTION:
//
//      lock/unlock user memory area for hypercall
//
//  FORMAL PARAMETERS:
//
//      addr - addr to lock
//      len - size of area
//
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

MTC_STATIC  int 
lock_pages(
    void *addr, 
    size_t len)
{
    int e = 0;
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    e = mlock(laddr, llen);
    return e;
}

MTC_STATIC  void 
unlock_pages(
    void *addr, 
    size_t len)
{
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    munlock(laddr, llen);
}

//
//
//  NAME:
//
//      do_watchdog_hypercall
//
//  DESCRIPTION:
//
//      call hypervisor to create/update/close watchdog
//
//  FORMAL PARAMETERS:
//
//      id - watchdog id (0: create)
//      timeout - watchdog timeout (0: close)
//      currentstatus - current status for edge trriger logging.
//                      MTC_SUCCESS: enable log
//                      other: disable log
//
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      MTC_ERROR_WD_INSUFFICIENT_RESOURCE - No Memory available
//      MTC_ERROR_WD_INVALID_HANDLE - id is invalid for the operation
//      other - fail
//
//  ENVIRONMENT:
//
//      dom0
//


MTC_STATIC  MTC_STATUS
do_watchdog_hypercall(uint32_t *id, uint32_t timeout)
{
    int ret;
    int fd;

    privcmd_hypercall_t hypercall = {0};
    sched_watchdog_t arg;

    hypercall.op = __HYPERVISOR_sched_op;
    hypercall.arg[0] = SCHEDOP_watchdog;
    hypercall.arg[1] = (__u64) (unsigned int) &arg;  // pointer to u64
    arg.id = *id;
    arg.timeout = timeout;

    fd = open(PRIVCMD_PATH, O_RDWR);

    if (fd == -1) 
    {
        return MTC_ERROR_WD_OPEN;
    }
    if (lock_pages(&arg, sizeof(arg)) != 0) 
    {
        close(fd);
        return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
    }
    if (lock_pages(&hypercall, sizeof(hypercall)) != 0) 
    {
        close(fd);
        unlock_pages(&arg, sizeof(arg));
        return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
    }
    ret = ioctl(fd, IOCTL_PRIVCMD_HYPERCALL, &hypercall);

    if (ret < 0)
    {
        ret = errno;

        close(fd);
        unlock_pages(&hypercall, sizeof(hypercall));
        unlock_pages(&arg, sizeof(arg));

        if (ret == -EINVAL)
        {
            // This may happen because the slot is not in use
            // or because there is no slot with this id
            return MTC_ERROR_WD_INVALID_HANDLE;
        }
        return MTC_ERROR_WD_INSTANCE_UNAVAILABLE ;
    }

    close(fd);
    unlock_pages(&hypercall, sizeof(hypercall));
    unlock_pages(&arg, sizeof(arg));

    //
    // if id == 0 ret is new id should be > 0
    //

    if (*id == 0) 
    {
        if (ret > 0) 
        {
            *id = ret;
        }
        else 
        {
            return MTC_ERROR_WD_INSTANCE_UNAVAILABLE;
        }
    }
    return MTC_SUCCESS;
}




//
//
//  L O C A L   D E F I N I T I O N S
//
//

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
//      main
//
//  DESCRIPTION:
//
//      main of cleanupwatchdog.c
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
    MTC_STATUS status;
    MTC_U32 idindex, idnum = 0;;
    uint32_t id[MAX_WATCHDOG_INSTANCE];
    char *endptr;

    char buf[BUFFER_SIZE];

    FILE *fp;

    if ((fp = fopen(WATCHDOG_INSTANCE_ID_FILE, "r")) == NULL)
    {
        return MTC_EXIT_SUCCESS;
    }
    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        chop(buf);
        id[idnum++] = strtol(buf, &endptr, 10);
        if (*endptr != '\0') 
        {
            return MTC_EXIT_INVALID_PARAMETER;
        }
        if (idnum >= MAX_WATCHDOG_INSTANCE) 
        {
            return MTC_EXIT_INVALID_PARAMETER;
        }
    }
    for (idindex = 0; idindex < idnum; idindex++)
    {
        if (id[idindex] == 0) 
        {
            continue;
        }
        status = do_watchdog_hypercall(&(id[idindex]), 0);

        if (status == MTC_ERROR_WD_INSUFFICIENT_RESOURCE)
        {
            return MTC_EXIT_TRANSIENT_SYSTEM_ERROR;
        }
        if (status != MTC_SUCCESS)
        {
            return MTC_EXIT_INTERNAL_BUG;
        }
    }
    return MTC_EXIT_SUCCESS;
}





