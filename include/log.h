//
//  MODULE: log.h
//

#ifndef LOG_H
#define LOG_H (1)    // Set flag indicating this file was included

//
//++
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
//--
//


#include <syslog.h>


#include "mtctypes.h"


//
// Use these flags to specify where the message are loged
//

#define MTC_LOG_SYSLOG          (1<<16)
#define MTC_LOG_PRIVATELOG      (1<<17)

//
// mask
//

extern MTC_S32  logmask;

#define MTC_LOG_MASK_DUMPPACKET (1<<18)
#define MTC_LOG_MASK_TRACE      (1<<19)
#define MTC_LOG_MASK_FH_TRACE   (1<<20)
#define MTC_LOG_MASK_LM_TRACE   (1<<21)
#define MTC_LOG_MASK_SCRIPT     (1<<22)
#define MTC_LOG_MASK_SC_WARNING (1<<23)
#define MTC_LOG_MASK_SYSLOG     (1<<24)

#define LOG_MASK_BASE           18
#define LOG_MASK_BITS           (32 - LOG_MASK_BASE)
#define LOG_MASK_NAMES {"DUMPPACKET", "TRACE", "FH_TRACE", "LM_TRACE", "SCRIPT", "SC_WARNING", "SYSLOG", NULL}

#ifdef NDEBUG
#define DEFAULT_LOG_MASK    (0)
#else
//#define DEFAULT_LOG_MASK    (MTC_LOG_MASK_TRACE)
//#define DEFAULT_LOG_MASK    (MTC_LOG_MASK_DUMPPACKET)
#define DEFAULT_LOG_MASK    (MTC_LOG_MASK_FH_TRACE)
//#define DEFAULT_LOG_MASK    (MTC_LOG_MASK_LM_TRACE)
//#define DEFAULT_LOG_MASK    (0)
#endif

//
// privatelog flag
//   TRUE: enable private log
//   FALSE: disable private log
//

extern MTC_BOOLEAN privatelogflag;


//
// Log Level definition
//

#define MTC_LOG_EMERG   (LOG_EMERG   | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_ALERT   (LOG_ALERT   | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_CRIT    (LOG_CRIT    | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_ERR     (LOG_ERR     | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_WARNING (LOG_WARNING                  | MTC_LOG_PRIVATELOG)
#define MTC_LOG_NOTICE  (LOG_NOTICE  | MTC_LOG_SYSLOG | MTC_LOG_PRIVATELOG)
#define MTC_LOG_INFO    (LOG_INFO                     | MTC_LOG_PRIVATELOG)
#ifdef NDEBUG
#define MTC_LOG_DEBUG   (LOG_DEBUG)
#else
#define MTC_LOG_DEBUG   (LOG_DEBUG                    | MTC_LOG_PRIVATELOG)
#endif

#define log_both(pri, ...)      log_message(((pri)  |  MTC_LOG_SYSLOG  | MTC_LOG_PRIVATELOG), __VA_ARGS__)
#define log_internal(pri, ...)  log_message((((pri) & ~MTC_LOG_SYSLOG) | MTC_LOG_PRIVATELOG), __VA_ARGS__)
#define log_external(pri, ...)  log_message((((pri) & ~MTC_LOG_PRIVATELOG) | MTC_LOG_SYSLOG), __VA_ARGS__)

#define log_maskable_debug_message(mask, ...) \
    ((logmask & MTC_LOG_MASK_##mask)? log_message(MTC_LOG_DEBUG, __VA_ARGS__): 0)

#define maskable_dump(mask, data, size) \
    ((logmask & MTC_LOG_MASK_##mask)? log_bin(MTC_LOG_DEBUG, data, size): 0)

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

extern MTC_S32
log_initialize();


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

extern void
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

extern void
log_terminate();


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

extern void
log_message(
    MTC_S32 priority,
    PMTC_S8 fmt,
    ...)
__attribute__((format (printf, 2, 3)));


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

extern void
log_bin(
    MTC_S32 priority,
    PMTC_S8 data,
    MTC_S32 size);


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

extern void
log_logmask(void);

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
//      priority
//          
//
//

void
log_backtrace(MTC_S32 priority);

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
//      "pthread failed. (1702) suffix.\n"
//
//      .(period) and \n are appended automatically.
//
//  FORMAL PARAMETERS:
//
//      status  MTC_STATUS
//      suffix  String appended to the generated message.
//              No string is added if suffix is NULL.
//

extern void
log_status(
    MTC_STATUS status,
    char *suffix);


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

extern void
log_fsync();

#endif	// LOG_H

