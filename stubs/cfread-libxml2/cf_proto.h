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
//      Config-File prototypes
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 4, 2008
//
//   


#include <netinet/in.h>
#include "mtctypes.h"

////
//
//
//  D E F I N I T I O N S
//
//
////

//
//
//  E X T E R N   D A T A   D E F I N I T I O N S
//
//

//
// return value of interpret_config_file()
//

#define  CFREAD_ERROR_SUCCESS      0
#define  CFREAD_ERROR_INVALID_PARAMETER   -1
#define  CFREAD_ERROR_FILEFORMAT   -2
#define  CFREAD_ERROR_OPEN         -3

////
//
//
//  E X T E R N A L   F U N C T I O N   P R O T O T Y P E S
//
//
////

MTC_S32
interpret_config_file(
    MTC_U8 *path,
    HA_CONFIG *c);


