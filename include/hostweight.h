//  MODULE: hostweight.h

#ifndef HOSTWEIGHT_H
#define HOSTWEIGHT_H (1)	// Set flag indicating this file was included

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
//      host weight used for arbitration of the surviving partition.
//      
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Nov 11, 2008
//   


#include "mtctypes.h"
#include "config.h"

////
//
//
//  D E F I N I T I O N S
//
//
////

//
// file path
//

#define HA_HOST_WEIGHT_FILE "/var/run/xhad.weight"

//
// host_weight
//

#define MAX_HOST_WEIGHT_VALUE              65535
#define MAX_HOST_WEIGHT_CLASSNAME_LEN      (64 + 1)
#define MAX_HOST_WEIGHT_CLASS_NUM          16

#define BUILTIN_HOST_WEIGHT_CLASSNAME      "native"
#define BUILTIN_HOST_WEIGHT_VALUE          1

//
// weight table
//

typedef struct host_weight_table {
    MTC_S8  classname[MAX_HOST_WEIGHT_CLASSNAME_LEN];
    MTC_U32  weight;
}   HOST_WEIGHT_TABLE, *PHOST_WEIGHT_TABLE;


//
// function in weightio.o
//

extern
MTC_STATUS
open_hostweight_file(
    int *fd,
    int *err_no);

extern
MTC_STATUS
read_hostweight_file(
    int fd,
    int *err_no,
    PHOST_WEIGHT_TABLE wtable,
    MTC_U32 size);

extern
MTC_STATUS
write_hostweight_file(
    int fd,
    int *err_no,
    PHOST_WEIGHT_TABLE wtable,
    MTC_U32 size);

extern
MTC_STATUS
set_hostweight_table(
    PHOST_WEIGHT_TABLE wtable,
    MTC_U32 size,
    PMTC_S8 classname,
    MTC_U32 weight);

//
// functions in HA daemon
//

extern
MTC_STATUS
hostweight_reload(void);

extern 
MTC_S32
hostweight_initialize(
    MTC_S32  phase);

#endif // HOSTWEIGHT_H
