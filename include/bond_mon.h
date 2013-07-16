//
//  MODULE: bond_mon.h
//

#ifndef BOND_MON_H
#define BOND_MON_H (1)    // Set flag indicating this file was included

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

typedef enum {
    BOND_STATUS_NOBOND = -1,
    BOND_STATUS_ERR,
    BOND_STATUS_DEGRADED,
    BOND_STATUS_NOERR
} MTC_BOND_STATUS, *PMTC_BOND_STATUS;


//
//
//  F U N C T I O N   P R O T O T Y P E S
//
//

MTC_BOND_STATUS
check_bonding_status();

MTC_S32
bm_initialize(
    MTC_S32  phase);



//
// Status Manager
//
#define COM_ID_BM "bm"

typedef struct _COM_DATA_BM {
    MTC_BOOLEAN     status;
    MTC_BOND_STATUS mtc_bond_status;
} COM_DATA_BM, *PCOM_DATA_BM;

#endif	// BOND_MON_H
