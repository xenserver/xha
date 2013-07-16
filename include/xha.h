//
//  MODULE: xha.h
//

#ifndef XHA_H
#define XHA_H (1)    // Set flag indicating this file was included

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
//      This header file defines the common constant of HA
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: March 13, 2008
//

//
//
//  D E F I N I T I O N S
//
//

#define MAX_HOST_NUM 64

extern void
main_terminate(
    MTC_STATUS status);

extern void
main_steady_state(void);

extern void
main_reset_scheduler(void);

extern pthread_attr_t * 
xhad_pthread_attr;

#endif  // XHA_H
