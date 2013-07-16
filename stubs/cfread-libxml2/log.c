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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "mtctypes.h"
#define SYSLOG_NAMES
#include "log.h"


#define MTC_LOG_FILE_NAME       "/var/log/xhad.log"
#define MTC_XHA_MODULE_NAME     "xhad"

static FILE                 *fpLogfile;
static pthread_spinlock_t   lock;


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

MTC_S32
log_initialize()
{
    MTC_S32 ret;

#if 0
    openlog(MTC_XHA_MODULE_NAME, LOG_PID, LOG_DAEMON);

    fpLogfile = fopen(MTC_LOG_FILE_NAME, "a");
    if (fpLogfile == NULL)
    {
        syslog(LOG_WARNING, "can't open local log file");
    }
#else
    fpLogfile = stderr;
#endif
    ret = pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
    if (ret)
    {
        syslog(LOG_ERR, "can't initialize spin lock");
        return -errno;
    }

    return 0;
}


// log_reopen
//
//  Reopen log file.
//  This function should be called from ha_log_reopen script
//  when log files is rotated.
//
//  Smaple logrotate config file </etc/logrotate.d/xhad>
//    /var/log/xhad.log {
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

void
log_reopen()
{
#if 0
    if (fpLogfile)
    {
        fclose(fpLogfile);
    }

    fpLogfile = fopen(MTC_LOG_FILE_NAME, "a");
    if (fpLogfile == NULL)
    {
        syslog(LOG_WARNING, "can't open local log file");
    }
#endif
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

void
log_terminate()
{
#if 0
    if (fpLogfile)
    {
        fclose(fpLogfile);
    }

    closelog();
#endif

    pthread_spin_destroy(&lock);
}


// xhadlog
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

void
xhadlog(MTC_S32 priority, PMTC_S8 fmt, ...)
{
    time_t  now;
    MTC_S8  *pTString;
    va_list ap;

    pthread_spin_lock(&lock);

    if (priority & MTC_LOG_PRIVATELOG)
    {
        MTC_S32 i = 0;

        while (prioritynames[i].c_val != -1 &&
               prioritynames[i].c_val != LOG_PRI(priority))
        {
	    i++;
        }
      
        now = time(NULL);
        pTString = ctime(&now);
        pTString[strlen(pTString) - 1] = '\0';
        fprintf(fpLogfile,
	        "%s [%s] ", pTString, prioritynames[i].c_name);
    
        va_start(ap, fmt);
        vfprintf(fpLogfile, fmt, ap);
        va_end(ap);

        fflush(fpLogfile);
    }
#if 0    
    if (priority & MTC_LOG_SYSLOG)
    {
        va_start(ap, fmt);
        vsyslog(LOG_PRI(priority), fmt, ap);
        va_end(ap);
    }
#endif

    pthread_spin_unlock(&lock);
}


// xhadlog_bin
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
void
xhadlog_bin8(MTC_S32 priority, PMTC_S8 data, MTC_S32 size)
{
    MTC_S32 line, pos, col;
    MTC_S8  asc_dmp[MAX_COL + 1];

    pthread_spin_lock(&lock);

    if (priority & MTC_LOG_PRIVATELOG)
    {
        for (line = 0, pos = 0; pos < size; line++)
        {
            fprintf(fpLogfile, "\t%04x: ", line);

            for (col = 0; col < MAX_COL && pos < size; col++, pos++)
            {
                fprintf(fpLogfile, "%02x ", data[pos]);
                asc_dmp[col] = (isprint(data[pos]))? data[pos]: '.';
            }
            asc_dmp[col] = '\0';

            for (; col < MAX_COL; col++)
            {
                fprintf(fpLogfile, "   ");
            }

            fprintf(fpLogfile, ": %s\n", asc_dmp);
        }
        fflush(fpLogfile);
    }

    pthread_spin_unlock(&lock);
}
