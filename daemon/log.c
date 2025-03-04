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
//      This file is HA logging utility
//
//  AUTHORS:
//
//      Keiichi Koyama
//
//  CREATION DATE: March 04, 2008
//
//  DESIGN ISSUES:
//
//  REVISION HISTORY: Inserted automatically
//
//      $Log: log.c $
//      
//  
//--
//

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h>


#include "mtctypes.h"
#include "mtcerrno.h"
#define SYSLOG_NAMES
#include "log.h"



#define MTC_LOG_FILE_NAME       "/var/log/xha.log"
#define MTC_XHA_MODULE_NAME     "xha"


MTC_S32 logmask = DEFAULT_LOG_MASK;
MTC_BOOLEAN privatelogflag = TRUE;


static FILE                 *fpLogfile = NULL;
static MTC_BOOLEAN          initialized = FALSE;


static pthread_rwlock_t     lock;


PMTC_S8 monthnames[] = {
    "Jan", "Feb", "Mar",    "Apr", "May", "Jun",
    "Jul", "Aug", "Sep",    "Oct", "Nov", "Dec"
};


//
// Global functions (Lock manager interfaces)
//

// log_initialize
//
//  Initialize logging utility.
//
//
//  paramaters
//
//  return value
//    0: success
//    not 0: fail
//

MTC_S32 log_initialize()
{
    MTC_S32 ret;

    openlog(MTC_XHA_MODULE_NAME, LOG_PID, LOG_DAEMON);

    fpLogfile = fopen(MTC_LOG_FILE_NAME, "a");
    if (fpLogfile == NULL)
    {
        syslog(LOG_WARNING, "can't open local log file. (sys %d)", errno);
    }

    ret = pthread_rwlock_init(&lock, PTHREAD_PROCESS_PRIVATE);
    if (ret)
    {
        syslog(LOG_ERR, "can't initialize rwlock. (sys %d)", ret);
        return MTC_ERROR_LOG_PTHREAD;
    }

    initialized = TRUE;

    return MTC_SUCCESS;
}


// log_reopen
//
//  Reopen log file.
//  This function should be called from ha_log_reopen script
//  when log files is rotated.
//
//  Smaple logrotate config file </etc/logrotate.d/xha>
//    /var/log/xha.log {
//          rotate 5
//          size 100k
//          postrotate
//      	/opt/xensource/ha/ha_log_reopen
//          endscript
//    }
//
//
//  paramaters
//
//  return value
//    0: success
//    not 0: fail
//

void log_reopen()
{
    pthread_rwlock_wrlock(&lock);

    if (fpLogfile)
    {
        fclose(fpLogfile);
    }

    fpLogfile = fopen(MTC_LOG_FILE_NAME, "a");
    if (fpLogfile == NULL)
    {
        syslog(LOG_WARNING, "can't open local log file. (sys %d)", errno);
    }

    pthread_rwlock_unlock(&lock);
}


// log_terminate
//
//  Terminate logging utility.
//
//
//  paramaters
//
//  return value
//

void log_terminate()
{
    initialized = FALSE;

    pthread_rwlock_wrlock(&lock);

    if (fpLogfile)
    {
        fclose(fpLogfile);
        fpLogfile = NULL;
    }

    closelog();

    pthread_rwlock_unlock(&lock);
    pthread_rwlock_destroy(&lock);
}


// log_message
//
//  Log a message
//
//
//  paramaters
//      priority - log priority value
//                 The addtional flags, MTC_LOG_PRIVATELOG and/or
//                 MTC_LOG_SYSLOG is also valid.
//
//                 If MTC_LOG_PRIVATELOG flag is set, the message will log to
//                 HA private log.
//                 If MTC_LOG_SYSLOG flag is set, the message will log to
//                 syslog.
//
//  return value
//

void log_message(MTC_S32 priority, PMTC_S8 fmt, ...)
{
    time_t      now;
    struct tm   *ptm;
    va_list     ap;

    if (!initialized)
    {
        return;
    }

    pthread_rwlock_rdlock(&lock);

    if (priority & MTC_LOG_PRIVATELOG && fpLogfile != NULL && privatelogflag)
    {
        MTC_S32 i = 0;

        while (prioritynames[i].c_val != -1 &&
               prioritynames[i].c_val != LOG_PRI(priority))
        {
            i++;
        }
      
        now = time(NULL);
        ptm = localtime(&now);
        flockfile(fpLogfile);
        fprintf(fpLogfile, "%s %02d %02d:%02d:%02d %s %04d [%s] ",
                monthnames[ptm->tm_mon], ptm->tm_mday,
                ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
                ptm->tm_zone, 1900 + ptm->tm_year, prioritynames[i].c_name);
    
        va_start(ap, fmt);
        vfprintf(fpLogfile, fmt, ap);
        funlockfile(fpLogfile);
        va_end(ap);

        fflush(fpLogfile);

        //
        // Sometime, fsync() takes seconds or more.
        // It may cause unexpected watchdog timeout.
        // So we deceided that we do not use fsync().
        //
        // fsync(fileno(fpLogfile));
        //


    }
    if ((logmask & MTC_LOG_MASK_SYSLOG) || (priority & MTC_LOG_SYSLOG))
    {
        va_start(ap, fmt);
        vsyslog(LOG_PRI(priority), fmt, ap);
        va_end(ap);
    }

    pthread_rwlock_unlock(&lock);
}


// log_bin
//
//  Log binary data
//
//
//  paramaters
//      priority - only MTC_LOG_PRIVATE_LOG flag is valid.  The other flags
//                 are ignored.
//      data - pointer to the data to be dumped
//      size - size of dump data
//
//  return value
//

#define MAX_COL 16
void log_bin(MTC_S32 priority, PMTC_S8 data, MTC_S32 size)
{
    MTC_S32 line, pos, col;
    MTC_S8  asc_dmp[MAX_COL + 1];

    if (!initialized)
    {
        return;
    }

    pthread_rwlock_rdlock(&lock);

    if (priority & MTC_LOG_PRIVATELOG && fpLogfile != NULL && privatelogflag)
    {
        flockfile(fpLogfile);
        for (line = 0, pos = 0; pos < size; line++)
        {
            fprintf(fpLogfile, "\t%04x: ", line);

            for (col = 0; col < MAX_COL && pos < size; col++, pos++)
            {
                fprintf(fpLogfile, "%02x ", (MTC_U8) data[pos]);
                asc_dmp[col] = (isprint(data[pos]))? data[pos]: '.';
            }
            asc_dmp[col] = '\0';

            for (; col < MAX_COL; col++)
            {
                fprintf(fpLogfile, "   ");
            }

            fprintf(fpLogfile, ": %s\n", asc_dmp);
        }
        funlockfile(fpLogfile);
        fflush(fpLogfile);

        // 
        // We don't use fsync(). See comments in log_message().
        //
        // fsync(fileno(fpLogfile));
        // 
    }

    pthread_rwlock_unlock(&lock);
}

//
//
//  NAME:
//
//      log_logmask
//
//  DESCRIPTION:
//
//      log the logmask value.
//
//  FORMAL PARAMETERS:
//
//      none
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      none
//
//

void
log_logmask(void)
{
    MTC_U32 logmask_index;
    char *logmask_name[LOG_MASK_BITS] = LOG_MASK_NAMES;

    log_message(MTC_LOG_INFO, "LOG: logmask = %x\n", logmask);
    for (logmask_index = 0; logmask_index < LOG_MASK_BITS; logmask_index++)
    {
        MTC_S32 logmask_on;

        logmask_on = (1 << (logmask_index + LOG_MASK_BASE)) & logmask;
        if (logmask_name[logmask_index] == NULL && logmask_on == 0) 
        {
            continue;
        }
        log_message(MTC_LOG_INFO, "LOG:  %s:%2.2d(%s)\n", 
                    (logmask_on == 0)?"OFF":"ON ",
                    logmask_index + LOG_MASK_BASE,
                    (logmask_name[logmask_index] != NULL)?logmask_name[logmask_index]:"UNKNOWN_MASK");
    }
    return;
}


//
//
//  NAME:
//
//      log_backtrace
//
//  DESCRIPTION:
//
//      log the backtrace.
//
//  FORMAL PARAMETERS:
//
//      none
//          
//  RETURN VALUE:
//
//      none
//
//  ENVIRONMENT:
//
//      -rdynamic linker option is required to print funcition name to the log.
//
//

#define BACKTRACE_SIZE 10
#define FILL_RETURN_ADDRESS(x)                          \
    if (__builtin_frame_address(x) == 0)                \
    {                                                   \
        return_address[x] = NULL;                       \
        goto end_fill;                                  \
    }                                                   \
    return_address[x] = __builtin_return_address(x);    \

        

void
log_backtrace(MTC_S32 priority)
{
    int level;
    Dl_info dli;
    void *return_address[BACKTRACE_SIZE] = {NULL};

    level = 0;

    FILL_RETURN_ADDRESS(0);
    FILL_RETURN_ADDRESS(1);
    FILL_RETURN_ADDRESS(2);
    FILL_RETURN_ADDRESS(3);
    FILL_RETURN_ADDRESS(4);
    FILL_RETURN_ADDRESS(5);
    FILL_RETURN_ADDRESS(6);
    FILL_RETURN_ADDRESS(7);
    FILL_RETURN_ADDRESS(8);
    FILL_RETURN_ADDRESS(9);
 end_fill:

    log_message(priority, "backtrace -------------\n");

    for (level = 0;
         level < BACKTRACE_SIZE;
         level ++)
    {
        int err;

        if (return_address[level] == NULL)
        {
            break;
        }
        err = dladdr(return_address[level], &dli);
        if (err == 0 || dli.dli_sname == NULL) 
        {
            log_message(priority, "  %2d: (%p) --\n", level, return_address[level]);
        }
        else 
        {
            log_message(priority, "  %2d: (%p) %s\n", level, return_address[level], dli.dli_sname);
        }
    }
    log_message(priority, "backtrace -------------\n");
    return;
}

//
//
//  NAME:
//
//      log_status
//
//  DESCRIPTION:
//
//      generate a message from the status and log it as
//      a LOG_ERR level message.
//
//      ex.
//      "pthread failed. (1702) suffix"
//
//  FORMAL PARAMETERS:
//
//      status  MTC_STATUS
//      suffix  String appended to the generated message.
//              No string is added if suffix is NULL.
//

void
log_status(
    MTC_STATUS status,
    char *suffix)
{
    char *message;

    message = status_to_message(status);

    if (suffix == NULL || strlen(suffix) == 0)
    {
        log_message(MTC_LOG_ERR, "%s (%d).\n", message, status);
    }
    else
    {
        log_message(MTC_LOG_ERR, "%s (%d) %s.\n", message, status, suffix);
    }
}

//
//
//  NAME:
//
//      log_fsync
//
//  DESCRIPTION:
//
//      Issue fsync() for log file.
//
//  paramaters
//
//      none
//
//  return value
//
//      none
//

void
log_fsync()
{
    if (!initialized)
    {
        return;
    }

    pthread_rwlock_wrlock(&lock);
    if (fpLogfile)
    {
        fsync(fileno(fpLogfile));
    }
    pthread_rwlock_unlock(&lock);
}
