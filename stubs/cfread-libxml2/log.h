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
//      This is HA loggin utility interface header file.
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
//      $Log: log.h $
//      
//  
//--
//


#include <syslog.h>


//
// Use these flags to specify where the message are loged
//

#define MTC_LOG_SYSLOG      (1<<16)
#define MTC_LOG_PRIVATELOG  (1<<17)

//
// Log Level definition
//

#define MTC_LOG_EMERG   (LOG_EMERG | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_ALERT   (LOG_ALERT | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_CRIT    (LOG_CRIT | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_ERR     (LOG_ERR | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_WARNING (LOG_WARNING | MTC_LOG_PRIVATELOG)
#define MTC_LOG_NOTICE  (LOG_NOTICE | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_INFO    (LOG_INFO | MTC_LOG_PRIVATELOG)
#ifdef NDEBUG
#define MTC_LOG_DEBUG   (LOG_DEBUG)
#else
#define MTC_LOG_DEBUG   (LOG_DEBUG | MTC_LOG_PRIVATELOG)
#endif



#define log_message             xhadlog
#define log_internal(pri, ...)  xhadlog(((pri) & ~MTC_LOG_SYSLOG) | \
                                         MTC_LOG_PRIVATELOG, __VA_ARGS__)
#define log_external(pri, ...)  xhadlog(((pri) & ~MTC_LOG_PRIVATELOG) | \
                                         MTC_LOG_SYSLOG, __VA_ARGS__)
#define dump                    xhadlog_bin
#define dump_internal(...)      xhadlog_bin(MTC_LOG_PRIVATELOG, __VA_ARGS__)


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
log_initialize();


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
log_reopen();


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
log_terminate();


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
xhadlog(
    MTC_S32 priority,
    PMTC_S8 fmt,
    ...);


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

void
xhadlog_bin(
    MTC_S32 priority,
    PMTC_S8 data,
    MTC_S32 size);
