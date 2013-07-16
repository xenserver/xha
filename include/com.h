//
//  MODULE: com.h
//

#ifndef COM_H
#define COM_H (1)    // Set flag indicating this file was included

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
//      of Xen HA Common Object Manager.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Feb 22, 2008
//


#include "mtctypes.h"

//
//
// HA_COMMON_OBJECT_HANDLE
//


typedef void *HA_COMMON_OBJECT_HANDLE, **PHA_COMMON_OBJECT_HANDLE;

//
//
// Special handle value
//

#define HA_COMMON_OBJECT_INVALID_HANDLE_VALUE          NULL

// HA_COMMON_OBJECT_CALLBACK
//  callback function which is called when the object
//  has been modified. 
//  The Handle of the modified object is passed to the 
//  callback function.
//  The callback function is called in modifiers thread.
//
//  Do not modify the object data in this callback.
//
//  This callback is called with ownership of writerlock of the object.
//  Be careful about deadlock.
//  
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//    buffer: Data buffer of the HA Common Object
//

typedef void (*HA_COMMON_OBJECT_CALLBACK)(HA_COMMON_OBJECT_HANDLE object_handle,
                                          void *buffer);

//
// com_initialize
//
//  Initialize HA Common Object Manager.
//
//
//  paramaters
//    phase - 0: initialize
//            1: start
//           -1: terminate
//
//  return value
//    0: success
//    not 0: fail
//           fail in memory allocation
//           other fail
//

MTC_STATUS
com_initialize(
    MTC_S32 phase);

//
// com_create
//
//  Create a HA Common Object.
//  If the object already exists, Size and Buffer are ignored and 
//  the function returns success with valid ObjectHandle.
//
//
//  paramaters
//    object_id: HA Common Object ID
//    object_handle: Handle of the HA Common Object 
//    size: data size of the HA Common Object
//    buffer: initial data of the HA Common Object
//
//  return value
//    0: success
//    not 0: fail
//           fail in memory allocation
//           other fail
//

MTC_STATUS 
com_create(char *object_id,
           HA_COMMON_OBJECT_HANDLE *object_handle,
           MTC_U32 size,
           void *buffer);

//
// com_open
//
//  Open a HA Common Object.
//  If the object does not exist, the function returns success 
//  with valid ObjectHandle.
//
//  paramaters
//    object_id: HA Common Object ID
//    object_handle: Handle of the HA Common Object 
//
//  return value
//    0: success
//    not 0: fail
//           fail in memory allocation
//           other fail
//

MTC_STATUS 
com_open(
    char *object_id,
    HA_COMMON_OBJECT_HANDLE *object_handle);

//
// com_close
//
//  Close HA Common Object.
//
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           fail in memory allocation
//           other fail
//

MTC_STATUS
com_close(
    HA_COMMON_OBJECT_HANDLE object_handle);

//
// com_register_callback
//
//  Register callback function which is called when the object
//  has been modified. The ObjectHandle is passed to the callback function.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//    func: callback function
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail
//

MTC_STATUS
com_register_callback(
    HA_COMMON_OBJECT_HANDLE object_handle,
    HA_COMMON_OBJECT_CALLBACK func);


//
// com_deregister_callback
//
//  Deregister callback function.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//    func: callback function
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail
//

MTC_STATUS
com_deregister_callback(
    HA_COMMON_OBJECT_HANDLE object_handle,
    HA_COMMON_OBJECT_CALLBACK func);


//
// com_writer_lock
//
//  Acquire the writer lock for the object.
//  com_writer_unlock must be called as soon as possible.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//    buffer: pointer to the HA Common Object data is passed 
//            when this fucntion returns.
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail
//

MTC_STATUS
com_writer_lock(
    HA_COMMON_OBJECT_HANDLE object_handle,
    void **buffer);
                             
//
// com_writer_unlock
//
//  Release the writer lock for the object.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail
//

MTC_STATUS
com_writer_unlock(
    HA_COMMON_OBJECT_HANDLE object_handle);

//
// com_reader_lock
//
//  Acquire the reader lock for the object.
//  com_reader_unlock must be called as soon as possible.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//    buffer: pointer to the HA Common Object data is passed 
//            when this fucntion returns.
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail

MTC_STATUS
com_reader_lock(
    HA_COMMON_OBJECT_HANDLE object_handle,
    void **buffer);
                             
//
// com_reader_unlock
//
//  Release the reader lock for the object.
//
//  paramaters
//    object_handle: Handle of the HA Common Object 
//
//  return value
//    0: success
//    not 0: fail
//           The object is not found
//           other fail

MTC_STATUS
com_reader_unlock(
    HA_COMMON_OBJECT_HANDLE object_handle);


//
// log all objects
//
//

MTC_STATUS
com_log_all_objects(
    MTC_U32 dumpflag);


#endif // COM_H
