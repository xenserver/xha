//
//  MODULE: lock_mgr.h
//

#ifndef LOCK_MGR_H
#define LOCK_MGR_H (1)    // Set flag indicating this file was included

//
//++
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
//      This is lock manager interface header file.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: February 29, 2008
//
//  DESIGN ISSUES:
//
//--
//

#include "sm.h"


extern MTC_S32
lm_initialize(
    MTC_S32  phase);

extern void
lm_initialize_lm_fields(
    PCOM_DATA_SF psf);

extern MTC_BOOLEAN
lm_request_lock(
    MTC_UUID uuidMaster);

extern void
lm_cancel_lock();

#endif	// LOCK_MGR_H
