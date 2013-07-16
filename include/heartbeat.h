//
//  MODULE: heartbeat.h
//

#ifndef HEARTBEAT_H
#define HEARTBEAT_H (1)    // Set flag indicating this file was included

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


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//


//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//

extern MTC_S32
hb_initialize(
    MTC_S32  phase);

void
hb_SF_accelerate();

void
hb_SF_cancel_accelerate();

void
hb_send_hb_now(
    MTC_S32 count);

#endif	// HEARTBEAT_H
