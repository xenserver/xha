//  MODULE: config.h

#ifndef CONFIG_H
#define CONFIG_H (1)    // Set flag indicating this file was included

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
//      Config-File information
//
//  AUTHORS:
//
//      Shinji Matsumoto
//
//  CREATION DATE: 
//
//      Mar 4, 2008
//
//   

#include <netdb.h>
#include <netinet/in.h>
#include <linux/limits.h>

#include "mtctypes.h"
#include "xha.h"

//
// Constants
//

#define HEARTBEAT_INTERFACE_LEN             64
#define STATEFILE_PATH_LEN                  (PATH_MAX + 1)
#define WATCHDOG_MODE_LEN                   16

//
// Default values
//

#define HEARTBEAT_INTERVAL_DEFAULT            4
#define HEARTBEAT_TIMEOUT_DEFAULT            30
#define HEARTBEAT_WATCHDOG_TIMEOUT_DEFAULT   30
#define STATEFILE_INTERVAL_DEFAULT            4
#define STATEFILE_TIMEOUT_DEFAULT            30
#define STATEFILE_WATCHDOG_TIMEOUT_DEFAULT   45
#define BOOT_JOIN_TIMEOUT_DEFAULT            90
#define ENABLE_JOIN_TIMEOUT_DEFAULT          90
#define XAPI_HEALTH_CHECK_INTERVAL_DEFAULT   60  // TBD - should be specified by XS
#define XAPI_HEALTH_CHECK_TIMEOUT_DEFAULT    10  // TBD - should be specified by XS
#define XAPI_RESTART_ATTEMPTS_DEFAULT         1  // TBD - should be specified by XS
#define XAPI_RESTART_TIMEOUT_DEFAULT         30  // TBD - should be specified by XS
#define XAPI_LICENSE_CHECK_TIMEOUT           30

////
//
//
//  S T R U C T U R E   D E F I N I T I O N S
//
//
////

//
// HA_CONFIG_HOST_INFO
//
//  Define the host identifier.
//

typedef union socket_address {
    struct sockaddr sa;
    struct sockaddr_in sa_in;
    struct sockaddr_in6 sa_in6;
} socket_address;

typedef struct ha_config_host_info {
    MTC_S8  host_id[MTC_UUID_SIZE];  // '-' is removed,non NULL terminated.
    socket_address sock_address;
} HA_CONFIG_HOST_INFO, *PHA_CONFIG_HOST_INFO;

//
// HA_CONFIG_COMMON
//
//  Define the common section in the config-file.
//

typedef struct ha_config_common
{
    MTC_S8              generation_uuid[MTC_UUID_SIZE];  // '-' is removed,non NULL terminated.
    MTC_U32             udp_port;                   // host byte order
    MTC_U32             hostnum;
    HA_CONFIG_HOST_INFO    host[MAX_HOST_NUM];
    MTC_U32             heartbeat_interval;
    MTC_U32             heartbeat_timeout;
    MTC_U32             statefile_interval;
    MTC_U32             statefile_timeout;
    MTC_U32             heartbeat_watchdog_timeout;
    MTC_U32             statefile_watchdog_timeout;
    MTC_U32             boot_join_timeout;
    MTC_U32             enable_join_timeout;
    MTC_U32             xapi_healthcheck_interval;
    MTC_U32             xapi_healthcheck_timeout;
    MTC_U32             xapi_restart_attempts;
    MTC_U32             xapi_restart_timeout;
    MTC_U32             xapi_licensecheck_timeout;
}   HA_CONFIG_COMMON, *PHA_CONFIG_COMMON;

//
// HA_CONFIG_LOCAL
//
//  Define the local part in config-file.
//

typedef struct ha_config_local
{
    MTC_U32             localhost_index;  // index for ha_config.common.host
    MTC_S8              heartbeat_interface[HEARTBEAT_INTERFACE_LEN]; // example "xenbr0" "xapi1"
    MTC_S8              heartbeat_physical_interface[HEARTBEAT_INTERFACE_LEN]; // example "eth0" "bond0"
    MTC_S8              statefile_path[STATEFILE_PATH_LEN];
    MTC_S8              watchdog_mode[WATCHDOG_MODE_LEN];
}   HA_CONFIG_LOCAL, *PHA_CONFIG_LOCAL;

//
// HA_CONFIG
//
//  Define the all part in config-file.
//

typedef struct ha_config
{
    MTC_S8              version[8];                  // example "1.0"
    HA_CONFIG_COMMON    common;
    HA_CONFIG_LOCAL     local;
}   HA_CONFIG, *PHA_CONFIG;

////
//
//
//  E X T E R N A L   D A T A   D E C L A R A T I O N S 
//
//
////

//
// Exported by main.c
//

extern HA_CONFIG      ha_config; 

//
// Aliases to access ha_config data
//
#define _my_index       (ha_config.local.localhost_index)
#define _hb_interface   (ha_config.local.heartbeat_interface)
#define _hb_pif         (ha_config.local.heartbeat_physical_interface)
#define _sf_path        (ha_config.local.statefile_path)
#define _wd_mode        (ha_config.local.watchdog_mode)

#define _gen_UUID       (ha_config.common.generation_uuid)
#define _udp_port       (ha_config.common.udp_port)
#define _num_host       (ha_config.common.hostnum)
#define _host_info      (ha_config.common.host)
#define _t1             (ha_config.common.heartbeat_interval)
#define _T1             (ha_config.common.heartbeat_timeout)
#define _t2             (ha_config.common.statefile_interval)
#define _T2             (ha_config.common.statefile_timeout)
#define _Wh             (ha_config.common.heartbeat_watchdog_timeout)
#define _Ws             (ha_config.common.statefile_watchdog_timeout)
#define _Tboot          (ha_config.common.boot_join_timeout)
#define _Tenable        (ha_config.common.enable_join_timeout)
#define _tXapi          (ha_config.common.xapi_healthcheck_interval)
#define _TXapi          (ha_config.common.xapi_healthcheck_timeout)
#define _RestartXapi    (ha_config.common.xapi_restart_attempts)
#define _TRestartXapi   (ha_config.common.xapi_restart_timeout)
#define _Tlicense       (ha_config.common.xapi_licensecheck_timeout)

#define _my_UUID        (_host_info[_my_index].host_id)
#define _is_configured_host(X)   (X < ha_config.common.hostnum)

////
//
//
//  E X T E R N A L   F U N C T I O N   P R O T O T Y P E S
//
//
////

MTC_S32
interpret_config_file(
    MTC_S8 *path, 
    HA_CONFIG *c);

#endif	// CONFIG_H
