//
//  MODULE: mtctypgc.h
//

#ifndef MTCTYPES_H
#define MTCTYPES_H (1)    // Set flag indicating this file was included

//
//++
//
//  $Revision: $
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
//      This header file defines the intrinsic data types used by Marathon 
//      components. This header file must stand alone, and must not use any 
//      Linux specific definitions from other header files.
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: February 29, 2008
//
//  DESIGN ISSUES:
//
//  PORTABILITY ISSUES:
//
//      The definitions in this file have been tested with 32 and 64 bit
//      compilers. It is assumed that the 32 bit compilers have native 
//      support for 64 bit data.
//
//      GCC 4.1.1 or later was used to target 32 and 64 bit platforms.
//
//
//  REVISION HISTORY: Inserted automatically
//
//      $Log: mtctypgc.h $
//      
//  
//--
//

//
//
//  D E F I N I T I O N S
//
//

// These defines are for function prototype documentation

#ifndef IN
    #define IN
#endif

#ifndef OUT
    #define OUT
#endif

#ifndef OPTIONAL
    #define OPTIONAL
#endif

#ifndef MTC_DBG

    // If MTC_DBG is not defined, default to 0, so debug code is disabled.

    #define MTC_DBG 0

#endif

// 
// Define a symbol that indicates which compiler is in use.
// 

// Test for GCC 4.1 or greater

#if ((__GNUC__ < 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ < 1)))
    #error "Unsupported GCC compiler version"
#endif

// GCC Compiler

#define MTC_COMPILER_GCC 1

//
// GCC ignores the pragma macro arguments and doesn't report a compile
// time error. Borland code never used the macros. Make sure the
// macros are undefined, though Gcc may still not generate an error.
//

#undef MTC_PACKBYTE
#undef MTC_PACKDEFAULT


//
// Define MTC intrinsic data types
//

//
// 8 and 16 bit data sizes are the same across all tested compilers.
//

typedef unsigned char   MTC_U8;     // 8 bits
typedef signed char     MTC_S8;     // 8 bits

typedef unsigned short  MTC_U16;    // 16 bits
typedef signed short    MTC_S16;    // 16 bits

//
// For GCC, some data sizes differ across 32 and 64 bit compilers.
// long is 4 bytes for a 32 bit target, but 8 bytes for a 64 bit target.
// However, int is 4 bytes for both.
//

typedef unsigned int   MTC_U32;    // 32 bits
typedef signed int     MTC_S32;    // 32 bits

typedef unsigned long long MTC_U64; // 64 bits
typedef long long MTC_S64;          // 64 bits


//
// Built-in wide character width is platform specific. GCC uses 4 bytes,
// but Windows uses 2. For details, see wikipedia.org/wiki/Wide_character.
// To be safe, define a Marathon specific value that is 2 bytes.
//

typedef MTC_U16 MTC_WCHAR;

//
// Define a typedef for the 32 bit Marathon status codes.
// Windows NT message compiler (mc.exe) includes
// this as an explicit typecast with each message code #define.
//

typedef MTC_U32 MTC_STATUS;
typedef MTC_STATUS *PMTC_STATUS;


//
// The C99 standard provides a built-in boolean type _Bool and also "bool"
// via stdbool.h. Visual Studio 2005 does not support C99, though Windows has
// defined BOOL and BOOLEAN for years. Define a Marathon specific Boolean type.
// Normally bool or BOOL is defined as 1 byte.
//
// TBD - To avoid alignment issues in structures, we may want to use 4 bytes.
//
 
typedef MTC_U8 MTC_BOOLEAN;

//
// Define typedef for MTC PFN and physical addresses.
//
// Client code is responsible for knowing if contents is x86 or x64 oriented.
//

typedef MTC_U64 MTC_PFN, *PMTC_PFN;
typedef MTC_U64 MTC_PHYSICAL_ADDRESS, *PMTC_PHYSICAL_ADDRESS;

// 
// If they don't already exist, define our own symbols MTC_BOOLEAN.
//

#ifndef FALSE
    #define FALSE 0
#endif

#ifndef TRUE
    #define TRUE 1
#endif


//
// 128 bit support requires a structure. For convenience, make it a union
// with different size members. Since all the members use the same number
// of bytes, alignment pragmas are not needed.
//

typedef struct _MTC_U128 {
    union {
        MTC_U64 u64[2];
        MTC_U32 u32[4];
        MTC_U16 u16[8];
        MTC_U8  u8[16];
    };
} MTC_U128, *PMTC_U128;


//++
// MTC_UUID
//
//      Universally unique identifier. Also known as a GUID. Defined as union 
//      to allow access as either a byte array or two U64s.
//
//--

// UUID size in bytes

#define MTC_K_UUID_SIZE (16)

typedef union _MTC_UUID {

    struct  {
        MTC_U64 low;
        MTC_U64 high;
    } u64;

    struct {
        MTC_U8 byte[MTC_K_UUID_SIZE];
    } u8;

} MTC_UUID, *PMTC_UUID;


// 
// Define target architecture symbols for the pointer size (in bits), and
// a simple boolean.
// 

#define MTC_TARGET_LINUX    (1)

#if defined(__amd64)

    #define MTC_POINTER_SIZE    (8)
    #define MTC_TARGET_64       (1)

#elif defined(__i386)

    #define MTC_POINTER_SIZE    (4)
    #undef MTC_TARGET_64

#else
    #error "GCC __WORDSIZE compiler symbol is not defined or is invalid"
#endif


// 
// ****************************************************************************
// * Pointer typedefs are defined here for convenience. NOTE: Since the sizes *
// * will differ for 32 bit and 64 bit platforms, they must NOT be used in    *
// * data structures that will be passed between 32 and 64 bit architectures. *
// ****************************************************************************
//

typedef MTC_U8  *PMTC_U8;
typedef MTC_S8  *PMTC_S8;

typedef MTC_U16 *PMTC_U16;
typedef MTC_S16 *PMTC_S16;

typedef MTC_U32 *PMTC_U32;
typedef MTC_S32 *PMTC_S32;

typedef MTC_U64 *PMTC_U64;
typedef MTC_S64 *PMTC_S64;

typedef MTC_WCHAR *PMTC_WCHAR;
typedef MTC_BOOLEAN *PMTC_BOOLEAN;

//
// ****************************************************************************
// * Typedefs for "opaque" access to private data structures. These are       *
// * typically pointers to structures containing component (RAPI, PAPI)       *
// * specific information.                                                    *
// *                                                                          *
// * NOTE: Since the sizes will differ for 32 bit and 64 bit platforms,       *
// * they must NOT be used in data structures that will be passed between     *
// * 32 and 64 bit architectures.                                             *
// ****************************************************************************
//

typedef void *MTC_CONTEXT, **PMTC_CONTEXT;
typedef void *MTC_HANDLE, **PMTC_HANDLE;

//
// Structure to hold a doubly linked list entry, or list head.
//

typedef struct _MTC_LIST_ENTRY_T {
   struct _MTC_LIST_ENTRY_T *Flink;         // Forward Link
   struct _MTC_LIST_ENTRY_T *Blink;         // Back Link
} MTC_LIST_ENTRY_T, *PMTC_LIST_ENTRY_T;

// For performing arithmetic on pointers (such as aligning it to a particular
// boundary), one needs to convert the pointer to integer, perform the operation
// and then convert it back to pointer.  Because there isn't a type of integer that
// automatically matches the size of the pointer, I am creating one here to be
// used for those instances.  This avoids having a programmer know what architecture
// the code is being compiled for and using the appropriately size integer (U32 or U64).

#if (MTC_POINTER_SIZE == 4)
typedef MTC_U32 MTC_UINT_PTR_SIZED;
typedef MTC_S32 MTC_SINT_PTR_SIZED;
#elif (MTC_POINTER_SIZE == 8)
typedef MTC_U64 MTC_UINT_PTR_SIZED;
typedef MTC_S64 MTC_SINT_PTR_SIZED;
#else
#error "Unknown pointer size in mtctypes.h"
#endif

//
// Useful 32 and 64 bit conversion macros.
//

//
// Round down an integer to the specified byte boundary.
//

#define MTC_ROUNDDOWN_INTEGER_TO_BOUNDARY(val, size) ( (val) & ~((size)-1)) 

//
// Round up an integer to the next specified byte boundary.
//

#define MTC_ROUNDUP_INTEGER_TO_BOUNDARY(val, size) \
    (MTC_ROUNDDOWN_INTEGER_TO_BOUNDARY((val) + (size)-1, size)) 

//
// Because pointers have different sizes in different architectures and the
// bitwise AND operator does not work with pointers, they must first be converted
// to an integer large enough to hold the entire pointer value.  This is
// accomplished by using the MTC_UINT_PTR_SIZED type which changes in size based
// on the native pointer size.  These macros were created to facilitate
// making such conversions and making the code more understandable.
//

//
// The size argument is given in the number of bytes to round down to.  For
// example 4-bytes will round down to a longword aligned pointer.
//

#define MTC_ROUNDDOWN_PTR_TO_BOUNDARY(ptr, size) \
    (void *) MTC_ROUNDDOWN_INTEGER_TO_BOUNDARY((MTC_UINT_PTR_SIZED)(ptr), size)

#define MTC_ROUNDDOWN_PTR_TO_32BIT_BOUNDARY(ptr) MTC_ROUNDDOWN_PTR_TO_BOUNDARY(ptr, 4)

#define MTC_ROUNDDOWN_PTR_TO_64BIT_BOUNDARY(ptr) MTC_ROUNDDOWN_PTR_TO_BOUNDARY(ptr, 8)

#define MTC_ROUNDUP_PTR_TO_BOUNDARY(ptr, size) \
    (void *) MTC_ROUNDUP_INTEGER_TO_BOUNDARY((MTC_UINT_PTR_SIZED)(ptr), size)

#define MTC_ROUNDUP_PTR_TO_32BIT_BOUNDARY(ptr) MTC_ROUNDUP_PTR_TO_BOUNDARY(ptr, 4)

#define MTC_ROUNDUP_PTR_TO_64BIT_BOUNDARY(ptr) MTC_ROUNDUP_PTR_TO_BOUNDARY(ptr, 8)

//
// When casting pointers between 32-bit to 64-bit OSs, we found that the
// Microsoft compiler treats pointers as a signed quantity.  When you
// extend a 32-bit pointer to fill a 64-bit integer to be passed to the
// 64-bit OS, you'll get the sign (bit 31) of your 32-bit value propagated
// into the upper 32bits of the 64-bit integer.  To do the conversion in
// an OS-agnostic way, use the macro below that first converts the native
// pointer (which can be 64-bit when compiled for 64-bits) to an unsigned
// quantity before doing the expansion.
//

#define MTC_CAST_PTR_TO_UINT64(ptr) ((MTC_U64)(MTC_UINT_PTR_SIZED)(ptr))

//
// The following macro generates a compile time error if the assertion
// is false. The error is misleading, but at least the compile dies:
//
//  Windows:    error C2118: negative subscript
//  GCC:        error: size of array __C_ASSERT__ is negative
//

#define MTC_ASSERT(e) extern char __C_ASSERT__[(e)?1:-1]
#define MTC_ASSERT_SIZE(e) extern char __C_ASSERT__[(e)?1:-1]

MTC_ASSERT_SIZE(sizeof (MTC_U8) == 1);
MTC_ASSERT_SIZE(sizeof (MTC_S8) == 1);
MTC_ASSERT_SIZE(sizeof (MTC_U16) == 2);
MTC_ASSERT_SIZE(sizeof (MTC_S16) == 2);
MTC_ASSERT_SIZE(sizeof (MTC_U32) == 4);
MTC_ASSERT_SIZE(sizeof (MTC_S32) == 4);
MTC_ASSERT_SIZE(sizeof (MTC_U64) == 8);
MTC_ASSERT_SIZE(sizeof (MTC_S64) == 8);
MTC_ASSERT_SIZE(sizeof (MTC_U128) == 16);
MTC_ASSERT_SIZE(sizeof (MTC_WCHAR) == 2);
MTC_ASSERT_SIZE(sizeof (MTC_BOOLEAN) == 1);
MTC_ASSERT_SIZE(sizeof(MTC_UUID) == MTC_K_UUID_SIZE);
MTC_ASSERT_SIZE(sizeof (void *) == MTC_POINTER_SIZE);


//
// C extention of function name string
//

#define __MTC_FUNCTION__ __func__

#endif  // MTCTYPES_H
