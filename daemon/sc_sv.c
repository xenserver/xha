//  MODULE: sc_sv.c

//
//
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
//      Xen HA script server.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 12, 2008
//
//   

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <sys/syscall.h>



#include "mtctypes.h"
#include "mtcerrno.h"
#include "log.h"
#include "config.h"
#include "script.h"
#include "fist.h"

////
//
//
//  D E F I N I T I O N S
//
//
////


#define FIST_PTHREAD_ERRCODE 999

//
//
//  struct
//
//

#define THREAD_NAME_LEN 80

typedef struct script_service_thread_param
{
    char thread_name[THREAD_NAME_LEN];
    int socket;
    int funcnum;
    SCRIPT_SERVICE_FUNC func[SCRIPT_TYPE_NUM];
}   SCRIPT_SERVICE_THREAD_PARAM;


//
//
//  static data
//
//

static int sc_listening_socket[SCRIPT_SOCKET_NUM];
static pthread_t sc_thread[SCRIPT_SOCKET_NUM];
static MTC_BOOLEAN terminate = FALSE;
static pthread_spinlock_t lock;


////
//
//
//  I N T E R N A L   F U N C T I O N
//
//
////


//
//++
//
//  NAME:
//
//      check_head_vaild();
//
//  DESCRIPTION:
//
//      check request header from script.
//
//
//  FORMAL PARAMETERS:
//
//      head - pointer to request head.
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - valid
//      other - invalid
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

MTC_STATIC MTC_STATUS
check_head_valid(
    SCRIPT_DATA_HEAD *head)
{
    if (head->magic != SCRIPT_MAGIC) 
    {
        log_internal(MTC_LOG_WARNING, "SC: head->magic is invalid (%x).\n", head->magic);
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    if (head->response != 0) 
    {
        log_internal(MTC_LOG_WARNING, "SC: head->response is invalid (%x).\n", head->response);
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    if (head->type >= SCRIPT_TYPE_NUM) 
    {
        log_internal(MTC_LOG_WARNING, "SC: head->type is invalid (%x).\n", head->type);
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    if (head->length > sizeof(((SCRIPT_DATA_REQUEST *)NULL)->body)) 
    {
        log_internal(MTC_LOG_WARNING, "SC: head->length is invalid (%x).\n", head->length);
        return MTC_ERROR_SC_IMPROPER_DATA;
    }
    return MTC_SUCCESS; // SUCCESS
}

//
//++
//
//  NAME:
//
//      create_sockets();
//
//  DESCRIPTION:
//
//      create 2 sockets for listen
//      one is for ha_query_liveset
//      the other one is for the other scripts
//
//
//  FORMAL PARAMETERS:
//
//      None
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      other - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

MTC_STATIC  MTC_STATUS
create_sockets(void) 
{
    struct sockaddr_un listening_socket_addr;
    char sockname[sizeof(((struct sockaddr_un *)0)->sun_path)] = "";
    char *sockname_list[] = SCRIPT_SOCK_NAME_ARRAY;
    MTC_S32 i;


    if (fist_on("sc.socket"))
    {
        log_internal(MTC_LOG_INFO, "SC: FIST socket error\n");
        goto error_return;
    }

    for (i = 0; i < SCRIPT_SOCKET_NUM; i++) 
    {

        // make abstract namespace for PF_UNIX socket

        memset(sockname, 0, sizeof(sockname));
        memcpy(sockname+1, sockname_list[i], strlen(sockname_list[i]));


        // Create a listening socket

        listening_socket_addr.sun_family = PF_UNIX;
        memset(listening_socket_addr.sun_path, '\0', 
               sizeof(listening_socket_addr.sun_path));

        // ASSERT(sizeof(listening_socket_addr.sun_path) >= sizeof(sockname));

        memcpy(listening_socket_addr.sun_path, sockname, sizeof(sockname));

        if ((sc_listening_socket[i] = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
        {
            log_internal(MTC_LOG_ERR, "SC: cannnot create socket for %s. (sys %d)\n", sockname_list[i], errno);
            goto error_return;
        }

        //  Bind a name to the listening socket

        if (bind(sc_listening_socket[i], (struct sockaddr *)&listening_socket_addr, sizeof(listening_socket_addr)) < 0)
        {
            log_internal(MTC_LOG_ERR, "SC: cannnot bind socket for %s. (sys %d)\n", sockname_list[i], errno);
            goto error_return;
        }

        //  Listen on the listening socket

        if (listen(sc_listening_socket[i], 5) == -1)
        {
            log_internal(MTC_LOG_ERR, "SC: cannnot listen socket for %s. (sys %d)\n", sockname_list[i], errno);
            goto error_return;
        }
    }
    return MTC_SUCCESS;

 error_return:
    return MTC_ERROR_SC_SOCKET;
}



//
//++
//
//  NAME:
//
//      service_service_thread();
//
//  DESCRIPTION:
//
//      service thread
//
//  FORMAL PARAMETERS:
//
//      None
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

#define POLLING_TIMEOUT 1000 /* ms */

MTC_STATIC  void *
script_service_thread(
    void *param)
{
    int listening_socket, message_socket;
    struct sockaddr_un message_socket_addr;
    socklen_t message_socket_len;

    char thread_name[THREAD_NAME_LEN];
    MTC_BOOLEAN         term = FALSE;
    SCRIPT_DATA_REQUEST  request;
    SCRIPT_DATA_RESPONSE response;
    MTC_U32 service_func_num;
    SCRIPT_SERVICE_FUNC *service_func;

    MTC_BOOLEAN reported = FALSE;

    int read_num, write_num, size;
    int ret;

    listening_socket = ((SCRIPT_SERVICE_THREAD_PARAM *) param)->socket;
    strcpy(thread_name, ((SCRIPT_SERVICE_THREAD_PARAM *) param)->thread_name);
    service_func_num = ((SCRIPT_SERVICE_THREAD_PARAM *) param)->funcnum;
    service_func = ((SCRIPT_SERVICE_THREAD_PARAM *) param)->func;

    log_message(MTC_LOG_INFO, "SC: thread ID: %ld.\n", syscall(SYS_gettid));
    do
    {
        fd_set fds;
        int nfds;
        struct timeval wait;
        int selected;
        char *bufptr;

        //
        // wait new connection
        //

        memset((void *)&request, 0, sizeof(request));
        memset((void *)&response, 0, sizeof(response));

        FD_ZERO(&fds);
        nfds = 0;

        FD_SET(listening_socket, &fds);
        nfds = listening_socket;

        wait = mstotv(POLLING_TIMEOUT);

        selected = select (nfds+1, &fds, NULL, NULL, &wait);

        if (selected == 0) 
        {
            // select timeout
            goto continue_loop;
        }

        if (selected < 0) 
        {
            // select failed
            log_message(MTC_LOG_WARNING, "SC: select failed (thread:%s). (sys %d)\n", thread_name, errno);

            // try to continue;
            sleep(10);
            goto continue_loop;
        }
        if (!FD_ISSET(listening_socket, &fds)) 
        {
            log_message(MTC_LOG_WARNING, "SC: socket is not set after select (thread:%s).\n", thread_name);
            // only bug, ignore
        }

        //
        // accept
        //
        
        message_socket_len = sizeof(message_socket_addr);
        if ((message_socket = accept(listening_socket, (struct sockaddr *) &message_socket_addr, &message_socket_len)) < 0)
        {
            log_message(MTC_LOG_WARNING, "SC: accept failed (thread:%s). (sys %d)\n", thread_name, errno);
            goto continue_loop;
        }
                
        //
        // read request head
        //

        size = sizeof(request.head);
        read_num = 0;
        bufptr = (char *)&request;

        while (size - read_num > 0) 
        {

            // select before read
            
            FD_ZERO(&fds);
            FD_SET(message_socket, &fds);
            nfds = message_socket;
            wait = mstotv(_Wh * 1000);
            selected = select (nfds+1, &fds, NULL, NULL, &wait);
            if (selected == 0) 
            {
                // select timeout
                log_message(MTC_LOG_WARNING, "SC: select timeout (thread:%s).\n", thread_name);
                close(message_socket);
                goto continue_loop;
            }

            if (selected < 0) 
            {
                // select failed
                log_message(MTC_LOG_WARNING, "SC: select failed (thread:%s). (sys %d)\n", thread_name, errno);
                close(message_socket);
                goto continue_loop;
            }

            // read

            if ((ret = read(message_socket, bufptr + read_num, size - read_num)) <= 0) 
            {
                // EOF or fail
                log_message(MTC_LOG_WARNING, "SC: read failed in request head (thread:%s). (sys %d)\n", thread_name, errno);

                close (message_socket);
                goto continue_loop;
            }
            read_num += ret;

        }

        //
        // check head
        //

        if (check_head_valid(&request.head) != MTC_SUCCESS) 
        {
                
            // invalid head
            if (reported == FALSE)
            {
                log_message(MTC_LOG_ERR, "Script service received an invalid message.\n");
                reported = TRUE;
            }
            log_message(MTC_LOG_WARNING, "SC: invalild head (thread:%s).\n", thread_name);
                
            close (message_socket);
            goto continue_loop;
        }

        //
        // read request body
        //

        size = request.head.length;
        read_num = 0;
        bufptr = (char *)&request.body;

        while (size - read_num > 0) 
        {

            // select before read

            FD_ZERO(&fds);
            FD_SET(message_socket, &fds);
            nfds = message_socket;
            wait = mstotv(_Wh * 1000);
            selected = select (nfds+1, &fds, NULL, NULL, &wait);
            if (selected == 0) 
            {
                // select timeout
                log_message(MTC_LOG_WARNING, "SC: select timeout (thread:%s).\n", thread_name);
                close(message_socket);
                goto continue_loop;
            }

            if (selected < 0) 
            {
                // select failed
                log_message(MTC_LOG_WARNING, "SC: select failed (thread:%s). (sys %d)\n", thread_name, errno);
                close(message_socket);
                goto continue_loop;
            }

            // read

            if ((ret = read(message_socket, bufptr + read_num, size - read_num)) <= 0) 
            {
                // EOF or fail

                log_message(MTC_LOG_WARNING, "SC: read failed in request body (thread:%s). (sys %d)\n", thread_name, errno);
                close (message_socket);
                goto continue_loop;
            }
            read_num += ret;

        }

        //
        // call script service function;
        //
            
        response.head.magic = SCRIPT_MAGIC;
        response.head.response = 1;
        response.head.type = request.head.type;
        response.head.length = sizeof(response.body);

        if (request.head.type > service_func_num || service_func[request.head.type] == NULL) 
        {
            // invalid type
            if (reported == FALSE)
            {
                log_message(MTC_LOG_ERR, "Script service received an invalid message.\n");
                reported = TRUE;
            }
            log_message(MTC_LOG_WARNING, "SC: invalid type(%d) in head (thread:%s).\n", request.head.type, thread_name);

            close (message_socket);
            goto continue_loop;
        }

        ret = service_func[request.head.type](request.head.length,
                                              (void *) &request.body,
                                              &(response.head.length),
                                              (void *) &response.body);
        if (ret != MTC_SUCCESS) 
        {
            // service func failed
            // internal error

            log_internal(MTC_LOG_ERR, "SC: service func for type %d failed (thread:%s) status=%d.\n", request.head.type, thread_name, ret);
            
            close (message_socket);
            goto continue_loop;
        }

        //
        // write response
        //

        size = sizeof(response.head) + response.head.length;
        write_num = 0;
        bufptr = (char *)&response;

        while (size - write_num > 0) 
        {

            // select before write

            FD_ZERO(&fds);
            FD_SET(message_socket, &fds);
            nfds = message_socket;
            wait = mstotv(_Wh * 1000);
            selected = select (nfds+1, NULL, &fds, NULL, &wait);
            if (selected == 0) 
            {
                // select timeout
                log_message(MTC_LOG_WARNING, "SC: select timeout (thread:%s).\n", thread_name);
                close(message_socket);
                goto continue_loop;
            }

            if (selected < 0) 
            {
                // select failed
                log_message(MTC_LOG_WARNING, "SC: select failed (thread:%s). (sys %d)\n", thread_name, errno);
                close(message_socket);
                goto continue_loop;
            }

            if ((ret = write(message_socket, bufptr + write_num, size - write_num)) <= 0) 
            {
                // EOF or fail

                log_message(MTC_LOG_WARNING, "SC: write failed in response (thread:%s). (sys %d)\n", thread_name, errno);

                close (message_socket);
                goto continue_loop;
            }
            write_num += ret;
        }

        // close connection graceful;

        close (message_socket);


        // check set_excluded

        script_service_check_after_set_excluded(request.head.type,
                                                response.head.length,
                                                (void *) &response.body);

    continue_loop:

        pthread_spin_lock(&lock);
        term = terminate;
        pthread_spin_unlock(&lock);
        
    } while (!term);

    return NULL;
}

//
//
//  NAME:
//
//      type_to_socket_index();
//
//  DESCRIPTION:
//
//      get socket_index from script_type;
//
//  FORMAL PARAMETERS:
//
//      type - script_type
//          
//  RETURN VALUE:
//
//      socket_index
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATIC  MTC_U32
type_to_socket_index(MTC_U32 type)
{
    SCRIPT_SOCKET_TABLE s_table[] = SCRIPT_SOCKET_TABLE_INITIALIZER;
    MTC_U32 i;
    for (i = 0; i < SCRIPT_TYPE_NUM; i++) 
    {
        if (type == s_table[i].type) 
        {
            return s_table[i].socket_index;
        }
    }
    log_internal(MTC_LOG_ERR, "SC: invalid type (%d).\n", type);
    assert(FALSE);
    return 0;
}

//
//
//  NAME:
//
//      type_to_func();
//
//  DESCRIPTION:
//
//      get service_function from script_type;
//
//  FORMAL PARAMETERS:
//
//      type - script_type
//          
//  RETURN VALUE:
//
//      pointer to the service function
//
//  ENVIRONMENT:
//
//      dom0
//
//
MTC_STATIC  SCRIPT_SERVICE_FUNC
type_to_func(MTC_U32 type)
{
    SCRIPT_FUNC_TABLE f_table[] = SCRIPT_FUNC_TABLE_INITIALIZER;
    MTC_U32 i;
    for (i = 0; i < SCRIPT_TYPE_NUM; i++) 
    {
        if (type == f_table[i].type) 
        {
            return f_table[i].func;
        }
    }
    log_internal(MTC_LOG_ERR, "SC: invalid type (%d).\n", type);
    assert(FALSE);
    return NULL;
}



//
//
//  NAME:
//
//      create_threads();
//
//  DESCRIPTION:
//
//      create 2 threads for script service
//      one is for ha_query_liveset
//      the other one is for the other scripts
//
//  FORMAL PARAMETERS:
//
//      None
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      other - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//

MTC_STATIC  MTC_STATUS
create_threads(void)
{
    static SCRIPT_SERVICE_THREAD_PARAM thread_param[SCRIPT_SOCKET_NUM];
    SCRIPT_SERVICE_THREAD_PARAM *p;
    MTC_U32 sock_index;
    MTC_U32 type_index;
    int pthread_ret;

    for (sock_index = 0; sock_index < SCRIPT_SOCKET_NUM; sock_index++) 
    {
        p = &thread_param[sock_index];
        memset(p, 0, sizeof(SCRIPT_SERVICE_THREAD_PARAM));
        switch (sock_index) 
        {
        case SCRIPT_SOCK_INDEX_FOR_OTHER:
            strcpy(p->thread_name, "sc_thread_other");
            break;
        case SCRIPT_SOCK_INDEX_FOR_QUERY:
            strcpy(p->thread_name, "sc_thread_query");
            break;
        case SCRIPT_SOCK_INDEX_FOR_INTERNAL:
            strcpy(p->thread_name, "sc_thread_internal");
            break;
        default:
            strcpy(p->thread_name, "sc_thread_unknown");
            break;
        }

        p->socket = sc_listening_socket[sock_index];
        p->funcnum = SCRIPT_TYPE_NUM;
        for (type_index = 0; type_index < SCRIPT_TYPE_NUM; type_index ++) 
        {
            if (type_to_socket_index(type_index) == sock_index) 
            {
                p->func[type_index] = type_to_func(type_index);
            }
            else {
                p->func[type_index] = NULL;
            }
        }
        pthread_ret = pthread_create(&(sc_thread[sock_index]), xhad_pthread_attr, script_service_thread, p);
        if (fist_on("sc.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
        
        if (pthread_ret != 0) 
        {
            log_internal(MTC_LOG_ERR, "SC: cannot create thread (thread:%s). (sys %d)\n", p->thread_name, pthread_ret);
            return MTC_ERROR_SC_PTHREAD;
        }
    }
    return MTC_SUCCESS;
}

//
//++
//
//  NAME:
//
//      script_initialize0
//
//  DESCRIPTION:
//
//      script service initialize
//      * create spinlock
//      * create socket
//
//
//  FORMAL PARAMETERS:
//
//
//          
//  RETURN VALUE:
//
//      0 - success
//      not 0 - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

MTC_STATIC  MTC_STATUS
script_initialize0(void)
{
    int pthread_ret;

    pthread_ret = pthread_spin_init(&lock, 0);
    if (fist_on("sc.pthread")) pthread_ret = FIST_PTHREAD_ERRCODE;
    if (pthread_ret != 0) 
    {
        log_internal(MTC_LOG_ERR, "SC: cannot create spin_lock. (sys %d)\n", pthread_ret);
        return MTC_ERROR_SC_PTHREAD;
    }
    return create_sockets();
}

//
//++
//
//  NAME:
//
//      script_initialize0
//
//  DESCRIPTION:
//
//      script service initialize
//      * create spinlock
//      * create socket
//
//
//  FORMAL PARAMETERS:
//
//
//          
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//      other - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

MTC_STATIC  MTC_STATUS
script_terminate(void)
{
    MTC_U32 i;

    pthread_spin_lock(&lock);
    terminate = TRUE;
    for (i = 0 ; i < SCRIPT_SOCKET_NUM; i++) 
    {
        if (sc_listening_socket[i] > 0) 
        {
#if 0
            //  let exit system call handle this to give
            //  an accurate intication of daemon termination
            //  to calldaemon (command)

            close(sc_listening_socket[i]);
#endif
            sc_listening_socket[i] = -1;
        }
    }
    pthread_spin_unlock(&lock);

#if 0
    {
        int pthread_ret;
        // wait for thread termination;

        for (i = 0 ; i < SCRIPT_SOCKET_NUM; i++) 
        {
            if ((pthread_ret = pthread_join(sc_thread[i], NULL)) != 0) 
            {
                pthread_ret = pthread_kill(sc_thread[i], SIGKILL);
            }
        }
    }
#endif
    return MTC_SUCCESS;
}



////
//
//
//  E X T E R N A L   F U N C T I O N
//
//
////

//
//++
//
//  NAME:
//
//      script_initialize
//
//  DESCRIPTION:
//
//      script service initialize/start/terminate
//
//  FORMAL PARAMETERS:
//
//      phase - 0: initialize
//              1: start
//             -1: terminate
//          
//  RETURN VALUE:
//
//      0 - success
//      not 0 - fail
//
//  ENVIRONMENT:
//
//      dom0
//
//--
//

MTC_STATUS
script_initialize(
    MTC_S32 phase)
{
    MTC_STATUS ret;

    switch (phase)
    {
    case 0:
        log_message(MTC_LOG_INFO, "SC: script_initialize(0).\n");
        ret = script_initialize0();
        break;
    case 1:
        log_message(MTC_LOG_INFO, "SC: script_initialize(1).\n");
        ret = create_threads();
        break;
    case -1:
        log_message(MTC_LOG_INFO, "SC: script_initialize(-1).\n");
        ret = script_terminate();
        break;
    }
    return ret;
}



