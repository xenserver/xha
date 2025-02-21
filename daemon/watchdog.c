//  MODULE: watchdog.c

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
//      Xen HA watchdog functions.
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 6, 2008
//
//   


#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>  // for mlock
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#define __XEN_TOOLS__
#include <xen/xen.h>
#include <xen/version.h>
#include <xen/sysctl.h>
#if XEN_SYSCTL_INTERFACE_VERSION < 4
#include <xen/linux/privcmd.h>
#else
#include <xen/sys/privcmd.h>
#endif
#include <xen/sched.h>

#include "mtctypes.h"
#include "mtcerrno.h"
#include "watchdog.h"
#include "log.h"
#include "config.h"
#include "fist.h"

////
//
//
//  D E F I N I T I O N S
//
//
////

#define PRIVCMD_PATH "/dev/xen/privcmd"
#define MAX_WATCHDOG_INSTANCE 8

typedef struct watchdog_instance 
{
    char label[256];
    MTC_U32 id;
    MTC_CLOCK set_time;
    MTC_U32 timeout;
    MTC_STATUS status;
}   WATCHDOG_INSTANCE;


enum {
    WATCHDOG_MODE_NONE,
    WATCHDOG_MODE_HYPERVISOR,
    WATCHDOG_MODE_USER,
};


//
//
//  Static valiables
//
//

static pthread_mutex_t watchdog_mutex = PTHREAD_MUTEX_INITIALIZER;
static WATCHDOG_INSTANCE *instance[MAX_WATCHDOG_INSTANCE] = {NULL};
static MTC_U32 instance_num = 0;
static int     hypercall_fd = -1;


#ifdef WD_IGNORE
static MTC_U32 watchdog_mode = WATCHDOG_MODE_USER;
#else  //WD_IGNORE
static MTC_U32 watchdog_mode = WATCHDOG_MODE_HYPERVISOR;
#endif //WD_IGNORE

static MTC_BOOLEAN initialized = FALSE;


////
//
//
//  I N T E R N A L   F U N C T I O N
//
//
////

#define XC_PAGE_SHIFT           12
#undef PAGE_SHIFT
#undef PAGE_SIZE
#undef PAGE_MASK
#define PAGE_SHIFT              XC_PAGE_SHIFT
#define PAGE_SIZE               (1UL << PAGE_SHIFT)
#define PAGE_MASK               (~(PAGE_SIZE-1))
#define WD_FENCE                (1)


//
//
//  NAME:
//
//      lock_pages
//      unlock_pages
//
//  DESCRIPTION:
//
//      lock/unlock user memory area for hypercall
//
//  FORMAL PARAMETERS:
//
//      addr - addr to lock
//      len - size of area
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
//


MTC_STATIC  int 
do_lock_pages(
    void *addr, 
    size_t len)
{
    int e = 0;
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    e = mlock(laddr, llen);
    return e;
}

MTC_STATIC  void 
do_unlock_pages(
    void *addr, 
    size_t len)
{
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    munlock(laddr, llen);
}

MTC_STATIC  int 
lock_pages(
    void *addr, 
    size_t len)
{
    int e = 0;
#ifdef UNLOCK_PAGES_EVERYTIME
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    e = mlock(laddr, llen);
#endif // UNLOCK_PAGES_PAGES_EVERYTIME
    return e;
}



MTC_STATIC  void 
unlock_pages(
    void *addr, 
    size_t len)
{
#ifdef UNLOCK_PAGES_EVERYTIME
    void *laddr = (void *)((unsigned long)addr & PAGE_MASK);
    size_t llen = (len + PAGE_SIZE - 1) & PAGE_MASK;
    munlock(laddr, llen);
#endif // UNLOCK_PAGES_PAGES_EVERYTIME
}

//
//
//  NAME:
//
//      do_watchdog_close
//
//  DESCRIPTION:
//
//      close file of hypercall
//
//  FORMAL PARAMETERS:
//
//      none
//
//  RETURN VALUE:
//
//      MTC_SUCCESS - success
//
//


MTC_STATIC  MTC_STATUS
do_watchdog_close(void)
{
    if (watchdog_mode != WATCHDOG_MODE_HYPERVISOR)
    {
        return MTC_SUCCESS;
    }

    if (hypercall_fd != -1)
    {
        close(hypercall_fd);
        hypercall_fd = -1;
    }
    return MTC_SUCCESS;
}

//
//
//  NAME:
//
//      do_hyercall
//
//  DESCRIPTION:
//
//      call hypervisor
//
//  FORMAL PARAMETERS:
//
//      none
//
//  RETURN VALUE:
//
//  ENVIRONMENT:
//
//      dom0
//


MTC_STATIC  MTC_STATUS
do_hypercall(privcmd_hypercall_t *hypercall, void *arg, size_t argsize, int *ioctl_ret, MTC_STATUS currentstatus)
{
  
    int ret;

    if (hypercall_fd == -1) 
    {
        hypercall_fd = open(PRIVCMD_PATH, O_RDWR);
        if (fist_on("wd.open")) {
            log_internal(MTC_LOG_INFO, "WD: FIST wd.open is on\n" );
            close(hypercall_fd);
            hypercall_fd = -1;
            errno = 999;
        }
    }
    if (hypercall_fd == -1) 
    {
        if (currentstatus == MTC_SUCCESS)
        {
            log_internal(MTC_LOG_ERR, "WD: cannnot open %s (sys %d).\n", PRIVCMD_PATH, errno);
        }
        return MTC_ERROR_WD_OPEN;
    }
    if (lock_pages(arg, argsize) != 0) 
    {
        if (currentstatus == MTC_SUCCESS)
        {
            log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
        }
        return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
    }
    if (lock_pages(hypercall, sizeof(privcmd_hypercall_t)) != 0) 
    {
        if (currentstatus == MTC_SUCCESS)
        {
            log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
        }

        unlock_pages(arg, argsize);
        return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
    }
    if (fist_on("wd.instance.unavailable")) 
    {
        log_internal(MTC_LOG_INFO, "WD: FIST wd.instance.unavaliable is on\n");
        ret = -999;
    }
    else
    {
        ret = ioctl(hypercall_fd, IOCTL_PRIVCMD_HYPERCALL, hypercall);
        if (ret < 0 && currentstatus == MTC_SUCCESS)
        {
            log_internal(MTC_LOG_ERR, "WD: hypercall failed (sys %d).\n", errno);
        }
    }

    unlock_pages(hypercall, sizeof(privcmd_hypercall_t));
    unlock_pages(arg, argsize);

    if (ret < 0)
    {
        return MTC_ERROR_WD_INSTANCE_UNAVAILABLE ;
    }

    *ioctl_ret = ret;
    return MTC_SUCCESS;
}


//
//
//  NAME:
//
//      do_watchdog_hypercall
//
//  DESCRIPTION:
//
//      call hypervisor to create/update/close watchdog
//
//  FORMAL PARAMETERS:
//
//      id - watchdog id (0: create)
//      timeout - watchdog timeout (0: close)
//      currentstatus - current status for edge trriger logging.
//                      MTC_SUCCESS: enable log
//                      other: disable log
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


MTC_STATIC  MTC_STATUS
do_watchdog_hypercall(uint32_t *id, uint32_t timeout, MTC_STATUS currentstatus)
{
    MTC_STATUS ret;
    int hypercall_ret;

    static privcmd_hypercall_t hypercall = {0};
    static sched_watchdog_t arg;
    static MTC_BOOLEAN mlock_done = FALSE;

    if (watchdog_mode != WATCHDOG_MODE_HYPERVISOR)
    {
        return MTC_SUCCESS;
    }

    if (!mlock_done)
    {
        if (do_lock_pages(&arg, sizeof(arg)) != 0) 
        {
            if (currentstatus == MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
            }
            return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
        }
        if (do_lock_pages(&hypercall, sizeof(hypercall)) != 0) 
        {
            if (currentstatus == MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
            }
            do_unlock_pages(&arg, sizeof(arg));
            return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
        }
        mlock_done = TRUE;
    }

    hypercall.op = __HYPERVISOR_sched_op;
    hypercall.arg[0] = SCHEDOP_watchdog;
    hypercall.arg[1] = (uintptr_t) &arg;  // pointer to u64
    arg.id = *id;
    arg.timeout = timeout;
    
    ret = do_hypercall(&hypercall, &arg, sizeof(arg), &hypercall_ret, currentstatus);
    if (ret != MTC_SUCCESS)
    {
        return ret;
    }

    //
    // if id == 0 , hypercall_ret is new id should be > 0
    //

    if (*id == 0) 
    {
        if (hypercall_ret > 0) 
        {
            *id = hypercall_ret;
        }
        else 
        {
            if (currentstatus == MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "WD: watchdog instance not avaliable (%d).\n", hypercall_ret);
            }
            return MTC_ERROR_WD_INSTANCE_UNAVAILABLE;
        }
    }
    return MTC_SUCCESS;
}



//
//
//  NAME:
//
//      do_domain_shutdown_self
//
//  DESCRIPTION:
//
//      call hypervisor to shutdown domain 0 immediately
//
//  FORMAL PARAMETERS:
//
//      none
//
//  RETURN VALUE:
//
//  ENVIRONMENT:
//
//      dom0
//


MTC_STATIC  MTC_STATUS

do_domain_shutdown_self(MTC_STATUS currentstatus)
{
  
    MTC_STATUS ret;
    int hypercall_ret;

    static privcmd_hypercall_t hypercall = {0};
    static sched_remote_shutdown_t arg;
    static MTC_BOOLEAN mlock_done = FALSE;

    if (!mlock_done)
    {
        if (do_lock_pages(&arg, sizeof(arg)) != 0) 
        {
            if (currentstatus == MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
            }
            // ignore failure
            // return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
        }
        if (do_lock_pages(&hypercall, sizeof(hypercall)) != 0) 
        {
            if (currentstatus == MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "WD: cannnot lock page (sys %d).\n", errno);
            }
            // ignore failure
            // do_unlock_pages(&arg, sizeof(arg));
            // return MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
        }
        mlock_done = TRUE;
    }

    hypercall.op = __HYPERVISOR_sched_op;
    hypercall.arg[0] = SCHEDOP_remote_shutdown;
    hypercall.arg[1] = (uintptr_t) &arg;  // pointer to u64
    arg.domain_id = 0;
    arg.reason = 1; // reboot
    
    log_fsync(); // flush logfile

    ret = do_hypercall(&hypercall, &arg, sizeof(arg), &hypercall_ret, currentstatus);
    // Never reach here if the hypercall succeeded.
    return ret;
}


//
//
//  NAME:
//
//      check_watchdog_timeout
//
//  DESCRIPTION:
//
//      check watchdog timeout. (expired/expire within t1/not update long time)
//
//  FORMAL PARAMETERS:
//
//      none
//
//  RETURN VALUE:
//
//      none
//
//


MTC_STATIC  void
check_watchdog_timeout(void)
{
    MTC_U32 wdi;
    MTC_CLOCK now;

    now = _getms();

    for (wdi = 0; wdi < instance_num; wdi++) {
        if (now - instance[wdi]->set_time > instance[wdi]->timeout * 1000)
        {
            // watchdog expired!
            log_message(MTC_LOG_DEBUG, "WD: now - settime = %d timeout = %d.\n",
                        (MTC_U32) (now - instance[wdi]->set_time), instance[wdi]->timeout * 1000);
            log_message(MTC_LOG_ERR, "Watchdog should have expired id=%d label=%s.\n",
                        instance[wdi]->id, instance[wdi]->label);

            log_fsync(); // flush logfile
            
            if (watchdog_mode != WATCHDOG_MODE_NONE)
            {
                assert(FALSE);
            }
        }
        else if (now - instance[wdi]->set_time > (instance[wdi]->timeout - _t1) * 1000)
        {
            // watchdog is expiring within t1
            log_message(MTC_LOG_DEBUG, "WD: now - settime = %d timeout = %d.\n",
                        (MTC_U32) (now - instance[wdi]->set_time), instance[wdi]->timeout * 1000);
            log_message(MTC_LOG_NOTICE, "Watchdog is expiring soon id=%d label=%s.\n",
                        instance[wdi]->id, instance[wdi]->label);

            log_fsync(); // flush logfile
        }
        else if (now - instance[wdi]->set_time > (instance[wdi]->timeout) * 500)
        {
            // watchdog is not updated while timeout * 1/2
            log_message(MTC_LOG_DEBUG, "WD: now - settime = %d timeout = %d.\n",
                        (MTC_U32) (now - instance[wdi]->set_time), instance[wdi]->timeout * 1000);
            log_message(MTC_LOG_INFO, "WD: watchdog has not been updated at least for half timeout id=%d label=%s.\n",
                        instance[wdi]->id, instance[wdi]->label);

            log_fsync(); // flush logfile

        }
    }
}


//
//
//  NAME:
//
//      record_watchdog_instance
//
//  DESCRIPTION:
//
//      record watchdog instance id to WATCHDOG_INSTANCE_ID_FILE
//
//
//  FORMAL PARAMETERS:
//
//      none
//
//  RETURN VALUE:
//
//      none
//
//


MTC_STATIC  void
record_watchdog_instatnce(void)
{
    MTC_U32 wdi;
    FILE *fp;
    struct stat statbuf;

    if (instance_num > 0)
    {
        if ((fp = fopen(WATCHDOG_INSTANCE_ID_FILE, "w")) == NULL)
        {
            log_message(MTC_LOG_WARNING, "WD: cannot open %s (sys %d).\n",
                        WATCHDOG_INSTANCE_ID_FILE, errno);
            return;
        }
        for (wdi = 0; wdi < instance_num; wdi++)
        {
            fprintf(fp, "%d\n", instance[wdi]->id);
        }
        fclose(fp);
        log_message(MTC_LOG_DEBUG, "WD: watchdog id file %s is updated.\n",
                    WATCHDOG_INSTANCE_ID_FILE);
    }
    else
    {
        if (stat(WATCHDOG_INSTANCE_ID_FILE, &statbuf) == 0)
        {
            if (unlink(WATCHDOG_INSTANCE_ID_FILE) == 0)
            {
                log_message(MTC_LOG_DEBUG, "WD: watchdog id file %s is deleted.\n",
                            WATCHDOG_INSTANCE_ID_FILE);

            }
            else
            {
                log_message(MTC_LOG_WARNING, "WD: cannot delete watchdog id file %s (sys %d).\n",
                            WATCHDOG_INSTANCE_ID_FILE, errno);

            }
        }
        else
        {
            log_message(MTC_LOG_DEBUG, "WD: watchdog id file %s not found.\n",
                        WATCHDOG_INSTANCE_ID_FILE);
        }
    }
}






////
//
//
//  E X T E R N A L   F U N C T I O N
//
//
////

//
//
//  NAME:
//
//      watchdog_create
//
//  DESCRIPTION:
//
//      Create new watchdog instance.
//      Set very big timer value (0xFFFFFFFF sec/136 years) to the watchdog
//
//  FORMAL PARAMETERS:
//
//      label - name label of the watchdog owner (for log only)
//      watchdog_handle: handle for the watchdog timer instance
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
//

MTC_STATUS
watchdog_create(
    char *label,  // for log only
    WATCHDOG_HANDLE *watchdog_handle)
{
    WATCHDOG_INSTANCE *new;
    MTC_STATUS ret = MTC_SUCCESS;
    

    pthread_mutex_lock(&watchdog_mutex);


    //
    // config
    //

    if (initialized == FALSE) 
    {
        if (!strcmp(_wd_mode, "HYPERVISOR"))
        {
            watchdog_mode = WATCHDOG_MODE_HYPERVISOR;
        }
        else if (!strcmp(_wd_mode, "USER"))
        {
            watchdog_mode = WATCHDOG_MODE_USER;
        }
        else if (!strcmp(_wd_mode, "NONE"))
        {
            watchdog_mode = WATCHDOG_MODE_NONE;
        }
        switch (watchdog_mode) 
        {
        case WATCHDOG_MODE_HYPERVISOR:
            log_message(MTC_LOG_INFO, "WD: watchdog mode = HYPERVISOR.\n");
            break;
        case WATCHDOG_MODE_USER:
            log_message(MTC_LOG_INFO, "WD: watchdog mode = USER.\n");
            break;
        case WATCHDOG_MODE_NONE:
            log_message(MTC_LOG_INFO, "WD: watchdog mode = NONE.\n");
            break;
        default:
            log_message(MTC_LOG_WARNING, "WD: unknown watchdog mode %d. Assuming HYPERVISOR mode.\n", watchdog_mode);
            watchdog_mode = WATCHDOG_MODE_HYPERVISOR;
            break;
        }
        
        initialized = TRUE;
    }

    if (label == NULL) 
    {
        label = "(none)";
    }

    new = malloc(sizeof(WATCHDOG_INSTANCE));
    if (new == NULL) 
    {
        log_internal(MTC_LOG_ERR, "WD: cannnot malloc size = %zu.\n", sizeof(WATCHDOG_INSTANCE));
        ret = MTC_ERROR_WD_INSUFFICIENT_RESOURCE;
        goto error_return;
    }
    if (strlen(label) < sizeof(new->label)) 
    {
        strcpy(new->label, label);
    } 
    else 
    {
        strncpy(new->label, label, sizeof(new->label) - 1);
        new->label[sizeof(new->label) - 1] = '\0';
    }
    new->id = 0;
    new->set_time = _getms();
    new->timeout = WATCHDOG_TIMEOUT_MAX;
    new->status = MTC_SUCCESS;

    ret = do_watchdog_hypercall(&(new->id), WATCHDOG_TIMEOUT_MAX, MTC_SUCCESS);
    if (ret == MTC_SUCCESS) 
    {
        log_message(MTC_LOG_INFO, "WD: (%s) success label=%s id=%d.\n", __func__, new->label, new->id);        
        *watchdog_handle = (WATCHDOG_HANDLE *)new;
        instance[instance_num++] = new;
    }
    else 
    {
        log_message(MTC_LOG_WARNING, "WD: (%s) label=%s failed.\n", __func__, label);
        free(new);
        *watchdog_handle = NULL;
    }

 error_return:
    pthread_mutex_unlock(&watchdog_mutex);
    if (ret == MTC_SUCCESS)
    {
        record_watchdog_instatnce();
    }

    return ret;
}

//
//
//  NAME:
//
//      watchdog_close
//
//  DESCRIPTION:
//
//      Stop and cleanup the watchdog timer.
//
//  FORMAL PARAMETERS:
//
//      watchdog_handle: handle for the watchdog timer instance.
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
//

MTC_STATUS
watchdog_close(
    WATCHDOG_HANDLE watchdog_handle)
{

    MTC_STATUS ret = MTC_SUCCESS;
    WATCHDOG_INSTANCE *w = (WATCHDOG_INSTANCE *) watchdog_handle;
    MTC_U32 wdi;
    MTC_U32 found = FALSE;

    pthread_mutex_lock(&watchdog_mutex);
    if (watchdog_handle == NULL) 
    {
        log_message(MTC_LOG_WARNING, "WD: (%s) invalid watchdog_handle.\n", __func__);        
        ret = MTC_ERROR_WD_INVALID_HANDLE;
        goto error_return;
    }
    log_message(MTC_LOG_INFO, "WD: (%s) label=%s stopping watchdog timer.\n", __func__, w->label);        
    ret = do_watchdog_hypercall(&(w->id), 0, MTC_SUCCESS);
    if (ret != MTC_SUCCESS) 
    {
        // ignore error just logging.
        log_message(MTC_LOG_WARNING, "WD: (%s) label=%s failed.\n", __func__, w->label);        
    }
    log_message(MTC_LOG_INFO, "WD: (%s) label=%s watchdog timer has been stopped successfully.\n", __func__, w->label);        
    for (wdi = 0; wdi < instance_num; wdi++) {
        if (instance[wdi] == w)
        {
            instance[wdi] = instance[instance_num - 1];
            instance[instance_num -1 ] = NULL;
            found = TRUE;
            instance_num --;
            break;
        }
    }
    if (!found) 
    {
        // ignore error just logging.
        log_message(MTC_LOG_WARNING, "WD: (%s) label=%s not found.\n", __func__, w->label);        
    }
    free(w);

    if (instance_num == 0)
    {
        do_watchdog_close();
    }

 error_return:
    pthread_mutex_unlock(&watchdog_mutex);
    record_watchdog_instatnce();
    return MTC_SUCCESS; // SUCCESS

}

//
//
//  NAME:
//
//      watchdog_set
//
//  DESCRIPTION:
//
//      Set new timout value for the watchdog instance.
//      The timeout value which was set in previous call is overwritten.
//      Caller should call next watchdog_set or watchdog_close within 
//      the timeout. Otherwise, the host is reset by the hypervisor.
//
//  FORMAL PARAMETERS:
//
//      watchdog_handle: handle for the watchdog timer instance.
//      timeout: timeout value(sec).
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
//

MTC_STATUS 
watchdog_set(
    WATCHDOG_HANDLE watchdog_handle,
    MTC_U32 timeout)
{
    MTC_STATUS ret = MTC_SUCCESS;
    WATCHDOG_INSTANCE *w = (WATCHDOG_INSTANCE *) watchdog_handle;

    pthread_mutex_lock(&watchdog_mutex);

    check_watchdog_timeout();

    if (w == NULL) 
    {
        log_message(MTC_LOG_WARNING, "WD: (%s) invalid watchdog_handle.\n", __func__);        
        ret = MTC_ERROR_WD_INVALID_HANDLE;
        goto error_return;
    }

    w->set_time = _getms();
    w->timeout = timeout;

    ret = do_watchdog_hypercall(&(w->id), timeout, w->status);


    if (ret != w->status) 
    {
        log_message(MTC_LOG_INFO, "WD: label=%s status changed to %d.\n", w->label, ret);
        if (ret == MTC_SUCCESS)
        {
            log_message(MTC_LOG_NOTICE, "Watchdog has been recoverd from an error.\n");
        }
        else
        {
            log_status(ret, NULL);
        }
        w->status = ret;
    }


 error_return:
    pthread_mutex_unlock(&watchdog_mutex);
    return ret; 
}

//
//
//  NAME:
//
//      watchdog_selffence
//
//  DESCRIPTION:
//
//      Do self fence.
//
//  FORMAL PARAMETERS:
//
//      None
//
//          
//  RETURN VALUE:
//
//      Never returns
//
//  ENVIRONMENT:
//
//      dom0
//
//

void
watchdog_selffence(void)
{

    MTC_STATUS ret;
    MTC_U32 wdi;
    MTC_U32 id;



    pthread_mutex_lock(&watchdog_mutex);

    if (watchdog_mode != WATCHDOG_MODE_HYPERVISOR) 
    {
        assert(FALSE);

    }
    log_message(MTC_LOG_INFO, "watchdog_selffence.\n");
    
    // Attempt to shutdown domain 0 immediately
    do_domain_shutdown_self(MTC_ERROR_HB_FENCEREQUESTED);
    // We shouldn't get here but if we do then invoke the watchdog:

    if (instance_num == 0)
    {
        // create instance for fence
        id = 0;
        ret = do_watchdog_hypercall(&id, WD_FENCE, MTC_SUCCESS);
        if (ret == MTC_SUCCESS) 
        {
            log_message(MTC_LOG_INFO, "WD: (%s) id=%d succeeded.\n", __func__, id);        
        }
        else
        {
            log_message(MTC_LOG_WARNING, "WD: (%s) id=%d failed.\n", __func__, id);
        }
    }

    for (wdi = 0; wdi < instance_num; wdi++)
    {
        ret = do_watchdog_hypercall(&(instance[wdi]->id), WD_FENCE, MTC_SUCCESS);
        if (ret == MTC_SUCCESS) 
        {
            log_message(MTC_LOG_INFO, "WD: (%s) label=%s id=%d succeeded.\n", __func__, instance[wdi]->label, instance[wdi]->id);        
        }
        else
        {
            log_message(MTC_LOG_WARNING, "WD: (%s) label=%s id=%d failed.\n", __func__, instance[wdi]->label, instance[wdi]->id);        
        }
    }

    // never returns if fencing;

    while (TRUE)
    {
        sleep(3600);
    }
}


