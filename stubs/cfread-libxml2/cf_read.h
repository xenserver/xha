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


#include <netinet/in.h>
#include "mtctypes.h"

//
// Constants
//

#define UUID_LEN                            32
#define MAX_HOST_NUM                        64
#define HEARTBEAT_INTERFACE_LEN             64
#define STATEFILE_PATH_LEN                  256

//
// Default values
//

#define HEARTBEAT_INTERVAL_DEFAULT          3
#define HEARTBEAT_TIMEOUT_DEFAULT           30
#define HEARTBEAT_WATCHDOG_TIMEOUT_DEFAULT  30
#define STATEFILE_INTERVAL_DEFAULT          3
#define STATEFILE_TIMEOUT_DEFAULT           30
#define STATEFILE_WATCHDOG_TIMEOUT_DEFAULT  45
#define BOOT_JOIN_TIMEOUT_DEFAULT           300
#define ENABLE_JOIN_TIMEOUT_DEFAULT         600
#define XAPI_HEALTH_CHECK_INTERVAL_DEFAULT  3       // TBD - should be specified by XS
#define XAPI_HEALTH_CHECK_TIMEOUT_DEFAULT   10      // TBD - should be specified by XS
#define XAPI_RESTART_RETRY_DEFAULT          0       // TBD - should be specified by XS
#define XAPI_RESTART_TIMEOUT_DEFAULT        10      // TBD - should be specified by XS
#define XAPI_LICENSE_CHECK_TIMEOUT          5       // TBD - should be specified by XS

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

typedef struct ha_config_host_info {
    MTC_U8  host_id[UUID_LEN];  // '-' is removed,non NULL terminated.
    in_addr_t ip_address;       // 
} HA_CONFIG_HOST_INFO, *PHA_CONFIG_HOST_INFO;

//
// HA_CONFIG_COMMON
//
//  Define the common section in the config-file.
//

typedef struct ha_config_common
{
    MTC_U8              generation_uuid[UUID_LEN];  // '-' is removed,non NULL terminated.
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
    MTC_U32             xapi_restart_retry;
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
    MTC_U8              heartbeat_interface[HEARTBEAT_INTERFACE_LEN]; // example "eth0" "bond0"
    MTC_U8              statefile_path[STATEFILE_PATH_LEN];
}   HA_CONFIG_LOCAL, *PHA_CONFIG_LOCAL;

//
// HA_CONFIG
//
//  Define the all part in config-file.
//

typedef struct ha_config
{
    MTC_U8              version[8];                  // example "1.0"
    HA_CONFIG_COMMON common;
    HA_CONFIG_LOCAL  local;
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

