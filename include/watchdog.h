//  MODULE: watchdog.h

#ifndef WATCHDOG_H
#define WATCHDOG_H (1)    // Set flag indicating this file was included

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
//      This header file containts 
//         external constants
//         data structures
//         functions
//      of Xen HA watchdog
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 6, 2008
//   


#include "mtctypes.h"

////
//
//
//  D E F I N I T I O N S
//
//
////

#define WATCHDOG_INSTANCE_ID_FILE "/var/run/xhad.wd.id"

//
//
// MAX_TIMEOUT_VALUE
//
//

#define WATCHDOG_TIMEOUT_MAX     ((MTC_U32)0xFFFFFFFFUL)


//
//
// WATCHDOG_HANDLE
//

typedef void *WATCHDOG_HANDLE;

//
//
// Special handle value
//

#define INVALID_WATCHDOG_HANDLE_VALUE          NULL

////
//
//
//  E X T E R N A L   F U N C T I O N   P R O T O T Y P E S
//
//
////

//
//
//  NAME:
//
//      watchdog_create
//
//  DESCRIPTION:
//
//      Create new watchdog instance.
//      Set very big timer value (0xFFFFFFFF sec/136 years) to the watchdog
//
//  FORMAL PARAMETERS:
//
//      label - name label of the watchdog owner (for log only)
//      watchdog_handle: handle for the watchdog timer instance
//
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      others - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATUS
watchdog_create(
    char *label,  // for log only
    WATCHDOG_HANDLE *watchdog_handle);

//
//
//  NAME:
//
//      watchdog_close
//
//  DESCRIPTION:
//
//      Stop and cleanup the watchdog timer.
//
//  FORMAL PARAMETERS:
//
//      watchdog_handle: handle for the watchdog timer instance.
//
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      others - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATUS
watchdog_close(
    WATCHDOG_HANDLE watchdog_handle);

//
//
//  NAME:
//
//      watchdog_set
//
//  DESCRIPTION:
//
//      Set new timout value for the watchdog instance.
//      The timeout value which was set in previous call is overwritten.
//      Caller should call next watchdog_set or watchdog_close within 
//      the timeout. Otherwise, the host is reset by the hypervisor.
//
//  FORMAL PARAMETERS:
//
//      watchdog_handle: handle for the watchdog timer instance.
//      timeout: timeout value(sec).
//               1 to WATCHDOG_TIMEOUT_MAX
//
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      others - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATUS
watchdog_set(
    WATCHDOG_HANDLE watchdog_handle,
    MTC_U32 timeout);

//
//
//  NAME:
//
//      watchdog_selffence
//
//  DESCRIPTION:
//
//      Do self fence.
//
//  FORMAL PARAMETERS:
//
//      None
//
//          
//  RETURN VALUE:
//
//      Never returns
//
//  ENVIRONMENT:
//
//      dom0
//
//

void
watchdog_selffence(void);


#endif // WATCHDOG_H
