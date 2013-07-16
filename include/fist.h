//
//  MODULE: fist.h
//

#ifndef FIST_H
#define FIST_H (1)    // Set flag indicating this file was included

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
//      Fault Insertion Self Test (FIST)
//
//  AUTHORS:
//
//      Satoshi Watanabe
//
//  CREATION DATE: 
//
//      April 3, 2008
//
//   

#ifdef NDEBUG

#define fist_initialize()
#define fist_enable(name)   (MTC_SUCCESS)
#define fist_disable(name)  (MTC_SUCCESS)
#define fist_on(name)       (FALSE)

#else

typedef struct _fisttab {
    char        *name;
    MTC_BOOLEAN sticky;
    MTC_BOOLEAN enabled;
    int         hash;
} FIST, *PFIST;

#define fist_enable(name)   _fist_set((name), TRUE)
#define fist_disable(name)  _fist_set((name), FALSE)
#define fist_on(name)       _fist_on(name)

extern void
fist_initialize();

extern MTC_STATUS
_fist_set(
    char *name,
    MTC_BOOLEAN enabled);

extern MTC_BOOLEAN
_fist_on(
    char *name);

#endif  // NDEBUG
#endif	// FIST_H
