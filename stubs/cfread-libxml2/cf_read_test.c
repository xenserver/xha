#include <stdio.h>
#include "cf_read.h"
#include "log.h"

HA_CONFIG ha_config;

print_ha_config(void) 
{
    MTC_U32 i;

    printf("version=%s\n", ha_config.version);
    printf("generation_uuid=%.8s-%.4s-%.4s-%.4s-%.12s\n", 
           ha_config.common.generation_uuid,
           ha_config.common.generation_uuid + 8,
           ha_config.common.generation_uuid + 12,
           ha_config.common.generation_uuid + 16,
           ha_config.common.generation_uuid + 20);
    printf("udp_port=%d\n", ha_config.common.udp_port);
    printf("hostnum=%d\n", ha_config.common.hostnum);
    for (i = 0 ; i < ha_config.common.hostnum; i++) 
    {
        printf("host[%d].HostId=%.8s-%.4s-%.4s-%.4s-%.12s\n", 
               i,
               ha_config.common.host[i].host_id,
               ha_config.common.host[i].host_id + 8,
               ha_config.common.host[i].host_id + 12,
               ha_config.common.host[i].host_id + 16,
               ha_config.common.host[i].host_id + 20);
        printf("host[%d].ipaddress=%s\n",
               i,
               inet_ntoa(ha_config.common.host[i].ip_address));
    }
    printf("heartbeat_interval=%d\n", ha_config.common.heartbeat_interval);
    printf("heartbeat_timeout=%d\n", ha_config.common.heartbeat_timeout);
    printf("statefile_interval=%d\n", ha_config.common.statefile_interval);
    printf("statefile_timeout=%d\n", ha_config.common.statefile_timeout);
    printf("heartbeat_watchdog_timeout=%d\n", ha_config.common.heartbeat_watchdog_timeout);
    printf("statefile_watchdog_timeout=%d\n", ha_config.common.statefile_watchdog_timeout);
    printf("boot_join_timeout=%d\n", ha_config.common.boot_join_timeout);
    printf("enable_join_timeout=%d\n", ha_config.common.enable_join_timeout);
    printf("xapi_healthcheck_interval=%d\n", ha_config.common.xapi_healthcheck_interval);
    printf("xapi_healthcheck_timeout=%d\n", ha_config.common.xapi_healthcheck_timeout);
    printf("xapi_restart_retry=%d\n", ha_config.common.xapi_restart_retry);
    printf("xapi_restart_timeout=%d\n", ha_config.common.xapi_restart_timeout);
    printf("xapi_licensecheck_timeout=%d\n", ha_config.common.xapi_licensecheck_timeout);
    printf("localhost_index=%d\n", ha_config.local.localhost_index);
    printf("heartbeat_interface=%s\n", ha_config.local.heartbeat_interface);
    printf("statefile_path=%s\n", ha_config.local.statefile_path);
}

main(
    int argc,
    char**argv)
{
    MTC_S32 ret;

    log_initialize();

    if (argc != 2)
    {
        fprintf(stderr, "cf_read_test: need XML file name\n");
        exit(1);
    }
        
    char *filename = argv[1];
    ret = interpret_config_file(filename, &ha_config);
    if (ret == 0)
    {
        print_ha_config();
    }

    log_terminate();

    exit(ret? 1: 0);
}
