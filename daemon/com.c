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
//      This file is implementation
//      of Xen HA Common Object Manager.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Feb 23, 2008
//
//   


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>

#include "mtctypes.h"
#include "mtcerrno.h"
#include "xha.h"
#include "log.h"
#include "com.h"
#include "fist.h"

#define FIST_PTHREAD_ERRCODE 999


#define HASH_TABLE_SIZE 1024
#define HASH_TABLE_PLACE_CONSTANT 137 /* 2^7+2^3+2^0 */
#define HASH_TABLE_PRIME_NUMBER 1021

#define ENTER_CS {pthread_mutex_lock(&com_mutex);}
#define LEAVE_CS {pthread_mutex_unlock(&com_mutex);}

#define HA_COMMON_OBJECT_MAGIC "HACMNOB"
#define HA_COMMON_OBJECT_MAGIC_SZ 8

#define com_exit_process(status)   exit(status_to_exit(status))



//
// static variables
//

static pthread_mutex_t com_mutex;


//
// thread-id reacord for diag of dead-lock
//

#define LOCK_STATE_NONE 0
#define LOCK_STATE_READER_ACQUIREING 1
#define LOCK_STATE_READER_ACQUIRED   2
#define LOCK_STATE_WRITER_ACQUIREING 3
#define LOCK_STATE_WRITER_ACQUIRED   4

#define THREAD_ID_RECORD_NUM 16

typedef struct thread_id_record {
    MTC_U32 lock_state;
    MTC_CLOCK changed_time;
    pthread_t thread_id;
}   THREAD_ID_RECORD;

//
// list of callback function
//

typedef struct ha_common_object_callback_list_item
{
    struct ha_common_object_callback_list_item *next;
    HA_COMMON_OBJECT_HANDLE object_handle;
    HA_COMMON_OBJECT_CALLBACK func;
} HA_COMMON_OBJECT_CALLBACK_LIST_ITEM;


//
// HA Common Object
//

typedef struct ha_common_object
{
    struct ha_common_object *next;
    char *object_id;
    MTC_U32  size;
    void *buffer;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *callback_list_head;
    pthread_rwlock_t rwlock;
    MTC_U32 in_use;
    MTC_U32 ref_count;
    MTC_U32 checksum;   // to detect modification by reader
    THREAD_ID_RECORD thread_id_record_table[THREAD_ID_RECORD_NUM];
} HA_COMMON_OBJECT;

//
// Handle to Common Object (for Internal)
//

typedef struct ha_common_object_handle_internal 
{
    HA_COMMON_OBJECT *object;
    char magic[HA_COMMON_OBJECT_MAGIC_SZ];
} HA_COMMON_OBJECT_HANDLE_INTERNAL;


//
// Hash Table
//

static HA_COMMON_OBJECT *common_object_hash[HASH_TABLE_SIZE] = {NULL};

//
// Internal Functions
//

#ifndef NDEBUG
MTC_STATIC MTC_U32
calc_checksum_object_buffer(
    HA_COMMON_OBJECT *object)
{
    MTC_U32 checksum = 0;
    MTC_U32 i;
    unsigned char *buf;

    if (object == NULL || object->buffer == NULL)
    {
        return 0;
    }
    buf = object->buffer;
    for (i = 0; i < object->size; i++) 
    {
        checksum ^= (MTC_U32) buf[i];
    }
    return checksum;
}
#endif  //NDEBUG






//
// return hash value 0 to HASH_TABLE_SIZE - 1
//

MTC_STATIC MTC_U32 
calc_hash(char *object_id)
{
    MTC_U32 h = 0;
    char *c;
    for (c = object_id; *c != '\0'; c++) 
    {
        h = h * HASH_TABLE_PLACE_CONSTANT + (MTC_U32)*c;
    }
    return (h % HASH_TABLE_PRIME_NUMBER);
}

//
// Allocate Buffer for the Object
//

MTC_STATIC MTC_STATUS
new_object_buffer(
    HA_COMMON_OBJECT *object,
    MTC_U32 size, 
    void *buffer)
{
    MTC_STATUS ret = MTC_SUCCESS;

    object->size = size;
    object->buffer = malloc(size);
    if (object->buffer == NULL) 
    {
        object->size = 0;
        log_internal(MTC_LOG_ERR, "COM: cannot allocate buffer for object (size=%d).\n", size);
        return MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
    }
    memcpy(object->buffer, buffer, size);
    return ret;
}


//
// examin object handle is Valid or not
// TRUE: Valid
// FALSE: Invalid
//

MTC_STATIC MTC_STATUS
valid_object_handle(
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle)
{
    if (handle == HA_COMMON_OBJECT_INVALID_HANDLE_VALUE) 
    {
        log_message(MTC_LOG_WARNING, "COM: invalid_handle_value.\n");
        assert(FALSE);
        return FALSE;
    }
    if (strcmp(handle->magic, HA_COMMON_OBJECT_MAGIC) != 0) 
    {
        log_message(MTC_LOG_WARNING, "COM: magic_mismatch.\n");
        assert(FALSE);
        return FALSE;
    }
    return TRUE;
}

//
// examin object is safe to delete
// TRUE: Valid
// FALSE: Invalid
//

MTC_STATIC MTC_STATUS 
object_safe_to_delete(
    HA_COMMON_OBJECT *object)
{
    if (object->ref_count != 0) 
    {
        return FALSE;
    }
    if (object->in_use != 0) 
    {
        return FALSE;
    }
    if (object->callback_list_head != NULL) 
    {
        return FALSE;
    }
    return TRUE;
}

//
// Allocate New Object Handle
//
//

MTC_STATIC HA_COMMON_OBJECT_HANDLE_INTERNAL 
*new_object_handle(
    HA_COMMON_OBJECT *object)
{
    HA_COMMON_OBJECT_HANDLE_INTERNAL *new_handle;
    new_handle = malloc(sizeof(HA_COMMON_OBJECT_HANDLE_INTERNAL));
    if (new_handle == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: cannot allocate for object_handle (size=%zu).\n", sizeof(HA_COMMON_OBJECT_HANDLE_INTERNAL));
        return NULL;
    }
    new_handle->object = object;
    strcpy(new_handle->magic, HA_COMMON_OBJECT_MAGIC);
    return new_handle;
}

//
// Free Object
//

MTC_STATIC void 
free_object_handle(
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle)
{
    free(handle);
    return;
}


//
// Allocate New Object
//
//

MTC_STATIC HA_COMMON_OBJECT *
new_object(
    char *object_id, 
    MTC_U32 size, 
    void *buffer)
{
    HA_COMMON_OBJECT *new;
    MTC_U32 i;

    new = malloc(sizeof(HA_COMMON_OBJECT));
    if (new == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: cannot allocate for object (size=%zu).\n", sizeof(HA_COMMON_OBJECT));
        return NULL;
    }
    new->next = NULL;
    new->object_id = strdup(object_id);
    if (new->object_id == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: cannot strdup for object_id (%s).\n", object_id);
        free (new);
        return NULL;
    }
    new->size = 0;
    new->buffer = NULL;
    if (buffer != NULL) 
    {
        if (new_object_buffer(new, size, buffer) != 0) 
        {
            free(new->object_id);
            free(new);
            return NULL;
        }
    }
    new->callback_list_head = NULL;
    //new->rwlock = PTHREAD_RWLOCK_INITIALIZER;
    new->in_use = 0;
    new->ref_count = 0;
#ifndef NDEBUG
    new->checksum = calc_checksum_object_buffer(new);
#endif //NDEBUG
    for (i = 0 ; i < THREAD_ID_RECORD_NUM; i++) {
        new->thread_id_record_table[i].lock_state = LOCK_STATE_NONE;
    }
    return new;
}

//
// Free Object
//

MTC_STATIC void 
free_object(
    HA_COMMON_OBJECT *object)
{
    if (object->buffer) 
    {
        free(object->buffer);
    }
    if (object->object_id) 
    {
        free(object->object_id);
    }
    free(object);
    return;
}

//
// Find Object from Table
//

MTC_STATIC HA_COMMON_OBJECT *
find_common_object(
    char *object_id)
{
    MTC_U32 hash_value;
    HA_COMMON_OBJECT *object;

    hash_value = calc_hash(object_id);
    
    // walk the hash table

    for (object = common_object_hash[hash_value];
         object != NULL;
         object = object->next) 
    {
        if (strcmp(object->object_id, object_id) == 0) 
        {
            return object;
        }
    }
    return NULL;
}

//
// Insert Object to the Table
//

MTC_STATIC void 
insert_common_object(
    HA_COMMON_OBJECT *new)
{
    MTC_U32 hash_value;

    hash_value = calc_hash(new->object_id);

    new->next = common_object_hash[hash_value];
    common_object_hash[hash_value] = new;

    return;
}

//
// Delete Object from the Table
//

MTC_STATIC HA_COMMON_OBJECT 
*delete_common_object(
    char *object_id)
{
    MTC_U32 hash_value;
    HA_COMMON_OBJECT **object, *ret;
    
    hash_value = calc_hash(object_id);
    
    // walk the hash table

    for (object = &common_object_hash[hash_value];
         *object != NULL;
         object = &((*object)->next)) 
    {
        if (strcmp((*object)->object_id, object_id) == 0) 
        {
            // Found it
            // Remove Object from the Table
            ret = (*object);
            (*object) = (*object)->next;
            return ret;
        }
    }
    return NULL;
}


//
// Allocate New Callback List Item
//

MTC_STATIC HA_COMMON_OBJECT_CALLBACK_LIST_ITEM 
*new_callback_list_item(
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle,
    HA_COMMON_OBJECT_CALLBACK func)
{
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *new;
    new = malloc(sizeof(HA_COMMON_OBJECT_CALLBACK_LIST_ITEM));
    if (new == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: cannot allocate for callback (size=%zu).\n", sizeof(HA_COMMON_OBJECT_CALLBACK_LIST_ITEM));
        return NULL;
    }
    new->next = NULL;
    new->object_handle = handle;
    new->func = func;
    return new;
}

//
// Free Callback List Item
//

MTC_STATIC void 
free_callback_list_item(
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *callback_list_item)
{
    free(callback_list_item);
    return;
}


//
// Insert callback to the object
//

MTC_STATIC void 
insert_callback_list_item(
    HA_COMMON_OBJECT *object, 
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *callback_list_item)
{
    callback_list_item->next = object->callback_list_head;
    object->callback_list_head = callback_list_item;

    return;
}

//
// Delete Object from the Table
//

MTC_STATIC HA_COMMON_OBJECT_CALLBACK_LIST_ITEM 
*delete_callback_list_item(
    HA_COMMON_OBJECT *object, 
    HA_COMMON_OBJECT_CALLBACK func)
                                         
{
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM **item, *ret;

    for (item = &object->callback_list_head;
         *item != NULL;
         item = &((*item)->next)) 
    {
        if ((*item)->func == func) 
        {
            // Found it
            // Remove Item from the List
            ret = (*item);
            (*item) = (*item)->next;
            return ret;
        }
    }
    return NULL;
}

//
// Set/Reset thread_id_record
//
// call in critical section.
//

void
set_thread_id_record(
    HA_COMMON_OBJECT *object, 
    MTC_U32 lock_state)
{
    MTC_U32 i;
    pthread_t self = pthread_self();
    MTC_CLOCK now;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = tstoms(ts);

    switch (lock_state) {
    case LOCK_STATE_READER_ACQUIREING:
    case LOCK_STATE_WRITER_ACQUIREING:
        // find lock_state == NULL
        for (i = 0 ; i < THREAD_ID_RECORD_NUM; i++) 
        {
            if (object->thread_id_record_table[i].lock_state == LOCK_STATE_NONE)
            {
                //
                // found
                //
                object->thread_id_record_table[i].lock_state = lock_state;
                object->thread_id_record_table[i].thread_id = self;
                object->thread_id_record_table[i].changed_time = now;
                return;
            }
        }
        log_message(MTC_LOG_WARNING, "COM: thraed_id_record_table is full.\n");
        break;
    default:
        // find thread_id == self;
        for (i = 0 ; i < THREAD_ID_RECORD_NUM; i++) 
        {
            if (object->thread_id_record_table[i].lock_state == LOCK_STATE_NONE) 
            {
                continue;
            }
            if (object->thread_id_record_table[i].thread_id == self)
            {
                //
                // found
                //
                object->thread_id_record_table[i].lock_state = lock_state;
                object->thread_id_record_table[i].changed_time = now;
                return;
            }
        }
        log_message(MTC_LOG_WARNING, "COM: thread_id %lu not found in thread_id_record_table.\n", self);
        break;
    }
    assert(FALSE);
    return ;
}

//
// log all objects
//
//

MTC_STATUS
com_log_all_objects(
    MTC_U32 dumpflag)
{
    MTC_U32 hash_index;
    HA_COMMON_OBJECT *object;
    MTC_CLOCK now;
    struct timespec ts;
    MTC_U32 owned;
    int pthread_ret;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *c;
    MTC_U32 callbacknum;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = tstoms(ts);


    log_message(MTC_LOG_DEBUG, "COM: ----------------<COM DUMP>----------------.\n");
    for (hash_index = 0; hash_index < HASH_TABLE_SIZE; hash_index++)
    {
        // try lock
        pthread_ret = pthread_mutex_trylock(&com_mutex);

        for (object = common_object_hash[hash_index]; object != NULL ; object = object->next)
        {
            MTC_U32 tid_index;

            owned = FALSE;
            log_message(MTC_LOG_DEBUG, "COM: object_id=%s.\n",object->object_id);
            log_message(MTC_LOG_DEBUG, "COM:   size=%d ref_count=%d in_use=%d.\n",object->size, object->ref_count, object->in_use);
            for (tid_index = 0; tid_index < THREAD_ID_RECORD_NUM; tid_index++) 
            {
                if (object->thread_id_record_table[tid_index].lock_state != LOCK_STATE_NONE)
                {
                    log_message(MTC_LOG_DEBUG, "COM:     lock_state=%d thread_id=0x%lx changed_time=%"PRId64"(ms) .\n",
                                object->thread_id_record_table[tid_index].lock_state,
                                object->thread_id_record_table[tid_index].thread_id,
                                now - object->thread_id_record_table[tid_index].changed_time
                                );
                    owned = TRUE;
                }
            }
            if (!owned)
            {
                log_message(MTC_LOG_DEBUG, "COM:   no lock owner.\n");
            }
            for (callbacknum = 0, c = object->callback_list_head; c != NULL; callbacknum ++, c = c->next);
            log_message(MTC_LOG_DEBUG, "COM:   %d callback functions are registered.\n",callbacknum);
            if (dumpflag && object->buffer)
            {
                log_bin(MTC_LOG_DEBUG, object->buffer, object->size);
            }
        }
        // unlock only if trylock suceeded
        if (pthread_ret == 0) 
        {
            pthread_mutex_unlock(&com_mutex);
        }
    }
    log_message(MTC_LOG_DEBUG, "COM: ----------------<COM DUMP>----------------.\n");
    return MTC_SUCCESS;
}



//
//
// public functions
//

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
    MTC_S32 phase)
{
    int pthread_ret;

    switch (phase) 
    {
    case 0: // initialize
        log_message(MTC_LOG_INFO, "COM: com_initialize(0).\n");
        pthread_ret = pthread_mutex_init(&com_mutex, NULL);
        if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
        if (pthread_ret != 0) 
        {
            log_internal(MTC_LOG_ERR, "COM: pthread_mutex_init failed (sys %d).\n", pthread_ret);
                         
            return MTC_ERROR_COM_PTHREAD;
        }
        break;
    case 1: // start
        log_message(MTC_LOG_INFO, "COM: com_initialize(1).\n");
        // do nothing
        break;
    case -1: // terminate
        log_message(MTC_LOG_INFO, "COM: com_initialize(-1).\n");
        // do nothing
        break;
    }
    return MTC_SUCCESS;
}


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
com_create(
    char *object_id,
    HA_COMMON_OBJECT_HANDLE *object_handle,
    MTC_U32 size,
    void *buffer)
{
    MTC_STATUS ret = MTC_SUCCESS;
    int pthread_ret;
    HA_COMMON_OBJECT *object;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *c;

    ENTER_CS;
    if ((object = find_common_object(object_id)) != NULL) 
    {

        // already exist

        *object_handle = new_object_handle(object);
        if (*object_handle == NULL) 
        {
            ret = MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
            goto error_return;
        }
        handle = (HA_COMMON_OBJECT_HANDLE_INTERNAL *) *object_handle;
        object->ref_count ++;
        handle->object->in_use++;
        set_thread_id_record(handle->object, LOCK_STATE_WRITER_ACQUIREING);
        LEAVE_CS;

        if (object->buffer == NULL && buffer != NULL) 
        {

            // Object Exists with no data 
            // 1. Acquire wrlock
            // 2. Allocate Object->buffer 
            // 3. Copy Buffer
            // 4. run callbacks
            // 5. Release wrlock

            pthread_ret = pthread_rwlock_wrlock(&handle->object->rwlock);
            if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
            
            ENTER_CS;
            set_thread_id_record(handle->object, LOCK_STATE_WRITER_ACQUIRED);
            if (pthread_ret != 0) 
            {
                log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_wrlock failed (sys %d).\n", __func__, pthread_ret);
                ret = MTC_ERROR_COM_PTHREAD;
                goto error_return;
            }
            ret = new_object_buffer(object, size, buffer);
            if (ret != 0) 
            {
                ret = MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
                goto error_return;
            }
#ifndef NDEBUG
            object->checksum = calc_checksum_object_buffer(object);
#endif //NDEBUG
            for (c = handle->object->callback_list_head; c != NULL; c = c->next) 
            {
                c->func(c->object_handle, handle->object->buffer);
#ifndef NDEBUG
                assert(object->checksum == calc_checksum_object_buffer(object));
#endif //NDEBUG
            }
            pthread_ret = pthread_rwlock_unlock(&handle->object->rwlock);
            if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;

            if (pthread_ret != 0) 
            {
                log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_unlock failed (sys %d).\n", __func__, pthread_ret);
                ret = MTC_ERROR_COM_PTHREAD;
                goto error_return;
            }
            handle->object->in_use--;
            set_thread_id_record(handle->object, LOCK_STATE_NONE);
            LEAVE_CS;
        }
        else 
        {
            ENTER_CS;
            set_thread_id_record(handle->object, LOCK_STATE_NONE);
            handle->object->in_use--;
            LEAVE_CS;
        }

        // OPEN SUCCESS

        return MTC_SUCCESS; //SUCCESS
    }

    /* not exist: Create new Object */

    object = new_object(object_id, size, buffer);
    if (object == NULL) 
    {
        ret = MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
        goto error_return;
    }
    pthread_ret = pthread_rwlock_init(&object->rwlock, NULL);
    if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;

    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_init failed (sys %d).\n", __func__, pthread_ret);
        ret = MTC_ERROR_COM_PTHREAD;
        goto error_return;
    }
    insert_common_object(object);
    *object_handle = new_object_handle(object);
    if (*object_handle == NULL) 
    {
        ret = MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
        goto error_return;
    }
    object->ref_count ++;

    // CREATE SUCCESS

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}

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
    HA_COMMON_OBJECT_HANDLE *object_handle)
{
    return com_create(object_id, object_handle, 0, NULL);
}

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
    HA_COMMON_OBJECT_HANDLE object_handle)
{
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    HA_COMMON_OBJECT *object; 
    int pthread_ret;

    if (!valid_object_handle(object_handle)) 
    {
        return MTC_ERROR_COM_INVALID_HANDLE;
    }
    object = handle->object;
    ENTER_CS;
    if (object->ref_count != 0) 
    {
        object->ref_count --;
    }
    if (object_safe_to_delete(object)) 
    {
        if (delete_common_object(object->object_id) == NULL)
        {
            log_message(MTC_LOG_WARNING, "COM: object (%s) not found in the table.\n",
                        object->object_id);
            assert(FALSE);
            goto error_return;

        }
        pthread_ret = pthread_rwlock_destroy(&object->rwlock);
        if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
        if (pthread_ret != 0) 
        {
            log_message(MTC_LOG_WARNING, "COM: pthread_rwlock_destroy failed (sys %d).\n", pthread_ret);
        }
        free_object(object);
    }
 error_return:
    LEAVE_CS;
    free_object_handle(handle);
    return MTC_SUCCESS; //ignore error return SUCCESS 
}

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
    HA_COMMON_OBJECT_CALLBACK func)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *new;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret =  MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }
    if (func == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) func is NULL.\n", __func__);
        assert(FALSE);
        ret =  MTC_ERROR_COM_CALLBACK_NOT_EXIST;
        goto error_return;
    }
    new = new_callback_list_item(handle, func);
    if (new == NULL) 
    {
        ret = MTC_ERROR_COM_INSUFFICIENT_RESOURCE;
        goto error_return;
    }
    insert_callback_list_item(handle->object, new);
    
 error_return:
    LEAVE_CS; 
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
   return ret;
}


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
    HA_COMMON_OBJECT_CALLBACK func)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *item;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret =  MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }
    if (func == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) func is NULL.\n", __func__);
        assert(FALSE);
        ret =  MTC_ERROR_COM_CALLBACK_NOT_EXIST;
        goto error_return;
    }
    item = delete_callback_list_item(handle->object, func);
    if (item == NULL) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) func not found.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_COM_CALLBACK_NOT_EXIST;
        goto error_return;
    }
    free_callback_list_item(item);

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}


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
    void **buffer)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    int pthread_ret;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }
    handle->object->in_use++;
    set_thread_id_record(handle->object, LOCK_STATE_WRITER_ACQUIREING);
    LEAVE_CS;
    pthread_ret = pthread_rwlock_wrlock(&handle->object->rwlock);
    if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
    ENTER_CS;
    set_thread_id_record(handle->object, LOCK_STATE_WRITER_ACQUIRED);
    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_wrlock failed (sys %d).\n", __func__, pthread_ret);
        ret = MTC_ERROR_COM_PTHREAD;
        goto error_return;

    }
    *buffer = handle->object->buffer;

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}
                             
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
    HA_COMMON_OBJECT_HANDLE object_handle)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    int pthread_ret;
    HA_COMMON_OBJECT_CALLBACK_LIST_ITEM *c;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }

    for (c = handle->object->callback_list_head; c != NULL; c = c->next) 
    {
        c->func(c->object_handle, handle->object->buffer);
    }
#ifndef NDEBUG
    handle->object->checksum = calc_checksum_object_buffer(handle->object);
#endif //NDEBUG


    handle->object->in_use--;
    set_thread_id_record(handle->object, LOCK_STATE_NONE);
    pthread_ret = pthread_rwlock_unlock(&handle->object->rwlock);
    if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_unlock failed (sys %d).\n", __func__, pthread_ret);
        ret = MTC_ERROR_COM_PTHREAD;
        goto error_return;
    }

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}

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
    void **buffer)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    int pthread_ret;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }
    handle->object->in_use++;
    set_thread_id_record(handle->object, LOCK_STATE_READER_ACQUIREING);
    LEAVE_CS;
    pthread_ret = pthread_rwlock_rdlock(&handle->object->rwlock);
    if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
    ENTER_CS;
    set_thread_id_record(handle->object, LOCK_STATE_READER_ACQUIRED);
    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_rdlock failed (sys %d).\n", __func__, pthread_ret);
        ret = MTC_ERROR_COM_PTHREAD;
        goto error_return;
    }
    *buffer = handle->object->buffer;

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}
                             
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
    HA_COMMON_OBJECT_HANDLE object_handle)
{
    MTC_STATUS ret = MTC_SUCCESS;
    HA_COMMON_OBJECT_HANDLE_INTERNAL *handle = object_handle;
    int pthread_ret;

    ENTER_CS;
    if (!valid_object_handle(handle)) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) invalid handle.\n", __func__);
        assert(FALSE);
        ret = MTC_ERROR_COM_INVALID_HANDLE;
        goto error_return;
    }

#ifndef NDEBUG
    assert(handle->object->checksum == calc_checksum_object_buffer(handle->object));
#endif //NDEBUG
    handle->object->in_use--;
    set_thread_id_record(handle->object, LOCK_STATE_NONE);
    pthread_ret = pthread_rwlock_unlock(&handle->object->rwlock);
    if (fist_on("com.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "COM: (%s) pthread_rwlock_unlock failed (sys %d).\n", __func__, pthread_ret);
        ret = MTC_ERROR_COM_PTHREAD;
        goto error_return;
    }

 error_return:
    LEAVE_CS;
    if (ret != MTC_SUCCESS) 
    {
        log_status(ret, NULL);
        log_message(MTC_LOG_WARNING, "COM: (%s) exit process.\n", __func__);
        log_backtrace(MTC_LOG_WARNING);
        com_exit_process(ret);
    }
    return ret;
}
    
