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
//      Config-File reader
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

//
//
//  O P E R A T I N G   S Y S T E M   I N C L U D E   F I L E S
//
//

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <libxml2/libxml/xmlmemory.h>
#include <libxml2/libxml/parser.h>


//
//
//  M A R A T H O N   I N C L U D E   F I L E S
//
//

#include "mtctypes.h"
#include "mtcerrno.h"
#include "config.h"
#include "log.h"


//
//
//  L O C A L   D E F I N I T I O N S
//
//


#define VALID_VERSION_STRING "1.0"


//
//
//  F U N C T I O N   D E F I N I T I O N S
//
//


//
//++
//
//  NAME:
//
//      set_default_value
//
//  DESCRIPTION:
//
//      set default value for HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      c - pointer to HA_CONFIG
//
//  RETURN VALUE:
//
//      None
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static void set_default_value(HA_CONFIG *c)
{
    c->common.heartbeat_interval = HEARTBEAT_INTERVAL_DEFAULT;
    c->common.heartbeat_timeout = HEARTBEAT_TIMEOUT_DEFAULT;
    c->common.statefile_interval = STATEFILE_INTERVAL_DEFAULT;
    c->common.statefile_timeout = STATEFILE_TIMEOUT_DEFAULT;
    c->common.heartbeat_watchdog_timeout = HEARTBEAT_WATCHDOG_TIMEOUT_DEFAULT;
    c->common.statefile_watchdog_timeout = STATEFILE_WATCHDOG_TIMEOUT_DEFAULT;
    c->common.boot_join_timeout = BOOT_JOIN_TIMEOUT_DEFAULT;
    c->common.enable_join_timeout = ENABLE_JOIN_TIMEOUT_DEFAULT;
    c->common.xapi_healthcheck_interval = XAPI_HEALTH_CHECK_INTERVAL_DEFAULT;
    c->common.xapi_healthcheck_timeout = XAPI_HEALTH_CHECK_TIMEOUT_DEFAULT;
    c->common.xapi_restart_attempts = XAPI_RESTART_ATTEMPTS_DEFAULT;
    c->common.xapi_restart_timeout = XAPI_RESTART_TIMEOUT_DEFAULT;
    c->common.xapi_licensecheck_timeout = XAPI_LICENSE_CHECK_TIMEOUT;
}

//
//++
//
//  NAME:
//
//      valid_version
//
//  DESCRIPTION:
//
//      check version string match or not
//
//  FORMAL PARAMETERS:
//
//      version - version string of xhad-config
//
//  RETURN VALUE:
//
//      TRUE: valid
//      FALSE: invalid
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static MTC_S32 
valid_version(
    char *version)
{
    if (strlen(version) >= sizeof(((HA_CONFIG *)0)->version)) 
    {
        return FALSE;
    }
    if (strcmp(version, VALID_VERSION_STRING) != 0) 
    {
        return FALSE;
    }

    return TRUE;
}

//
//++
//
//  NAME:
//
//      valid_config
//
//  DESCRIPTION:
//
//      check HA_CONFIG data structure
//      if the all required parameter is set or not.
//
//  FORMAL PARAMETERS:
//
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      TRUE: valid
//      FALSE: invalid
//
//  ENVIRONMENT:
//
//      None
//
//--
//

static MTC_S32 
valid_config(
    HA_CONFIG *c) 
{
    if (c->common.generation_uuid[0] == '\0') 
    {
        log_internal(MTC_LOG_ERR, "%s: GenerationUUID is not set\n", __func__);
        return FALSE;
    }
    if (c->common.udp_port == 0) 
    {
        log_internal(MTC_LOG_ERR, "%s: UDPport is not set\n", __func__);
        return FALSE;
    }
    if (c->common.hostnum == 0) 
    {
        log_internal(MTC_LOG_ERR, "%s: host num is 0\n", __func__);
        return FALSE;
    }
    if (c->local.heartbeat_interface[0] == '\0') 
    {
        log_internal(MTC_LOG_ERR, "%s: HeartbeatInterface is not set\n", __func__);
        return FALSE;
    }
    if (c->local.statefile_path[0] == '\0') 
    {
        log_internal(MTC_LOG_ERR, "%s: StateFile is not set\n", __func__);
        return FALSE;
    }
    if (c->local.localhost_index >= MAX_HOST_NUM || c->local.localhost_index < 0) 
    {
        log_internal(MTC_LOG_ERR, "%s: localhost.HostID is not set\n", __func__);
        return FALSE;
    }

    return TRUE;
}

//
//++
//
//  NAME:
//
//      uuid_strcpy
//
//  DESCRIPTION:
//
//      copy uuid string removing '-'
//      check size
//      check charactor
//      output does not terminate with '\0'
//
//  FORMAL PARAMETERS:
//
//      packed_uuid - output string
//      uuid - input string
//
//  RETURN VALUE:
//
//      0: success
//      -1: failed (too long or too short)
//
//  ENVIRONMENT:
//
//      None
//
//--
//
//


static int 
uuid_strcpy(
    char *packed_uuid,
    char *uuid)
{
    char *src;
    MTC_U32 count;

    count = 0;
    for (src = uuid; *src; src++) 
    {
        if (*src == '-') continue;
        if (count < MTC_UUID_SIZE) 
        {
            packed_uuid[count] = *src;
            count++;
        }
        else
        {
            // too long uuid
            return MTC_ERROR_CF_INVALID_FORMAT;
        }
    }
    if (count < MTC_UUID_SIZE) 
    {
        // too short uuid
        for (; count < MTC_UUID_SIZE; count++)
        {
            packed_uuid[count] = '\0';
        }
        return MTC_ERROR_CF_INVALID_FORMAT;
    }
    return MTC_SUCCESS;
}

//
//++
//
//  NAME:
//
//      host_info_compar
//
//  DESCRIPTION:
//
//      lexicographical compare of host_id
//      This is callback function for qsort
//
//  FORMAL PARAMETERS:
//
//      h1 - host info 1
//      h2 - host info 2
//
//  RETURN VALUE:
//
//      -1: h1 is less than h2 in lexicographical order.
//       1: h1 is more than h2 in lexicographical order.
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static int 
host_info_compar(
    const void *h1, 
    const void *h2) 
{
    HA_CONFIG_HOST_INFO *host1, *host2;

    host1 = (HA_CONFIG_HOST_INFO *) h1;
    host2 = (HA_CONFIG_HOST_INFO *) h2;

    return strncmp(host1->host_id, host2->host_id, MTC_UUID_SIZE);
}


//
//++
//
//  NAME:
//
//      walk_host_config
//
//  DESCRIPTION:
//
//      walk host subtree and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      doc - pointer for xml doc
//      cur - xml node for host element
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//

static MTC_S32 
walk_host_config(
    xmlDocPtr doc,
    xmlNodePtr cur,
    HA_CONFIG *c) 
{
    xmlNodePtr sub;
    xmlChar *txt;

    //
    // Walk XML
    //

    if (c->common.hostnum > MAX_HOST_NUM) 
    {
        log_internal(MTC_LOG_ERR,
                    "%s: hostnum(%d) exceeds MAX_HOST_NUM(%d)\n",
                    __func__, c->common.hostnum, MAX_HOST_NUM);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {

        //
        // HostID
        //
        //

        if (xmlStrcmp(sub->name, (const xmlChar *) "HostID") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get HostID\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            if (uuid_strcpy(c->common.host[c->common.hostnum].host_id, (char *) txt) < 0) {
                log_internal(MTC_LOG_ERR, "%s: invalid HostID %s\n", __func__, txt);

                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            xmlFree(txt);
        }

        //
        // IPaddress
        //

        else if (xmlStrcmp(sub->name, (const xmlChar *) "IPaddress") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get IPaddress\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            const char *ip_address = (char *) txt;

            struct addrinfo hints;
            struct addrinfo *result;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_flags = AI_NUMERICHOST;
            hints.ai_protocol = 0;
            hints.ai_canonname = NULL;
            hints.ai_addr = NULL;
            hints.ai_next = NULL;

            errno = 0;
            const int ret = getaddrinfo(ip_address, NULL, &hints, &result);
            if (ret)
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get addr info of `%s` (info: %s). (sys %d)\n",
                    __func__, ip_address, gai_strerror(ret), errno);
                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            xmlFree(txt);

            socket_address *ss = &c->common.host[c->common.hostnum].sock_address;
            switch (result->ai_family)
            {
                case AF_INET: {
                    struct sockaddr_in *sa = &ss->sa_in;
                    memcpy(&sa->sin_addr, &((struct sockaddr_in *) result->ai_addr)->sin_addr, sizeof sa->sin_addr);
                    sa->sin_family = AF_INET;
                    break;
                }
                case AF_INET6: {
                    struct sockaddr_in6 *sa6 = &ss->sa_in6;
                    memcpy(&sa6->sin6_addr, &((struct sockaddr_in6 *) result->ai_addr)->sin6_addr, sizeof sa6->sin6_addr);
                    sa6->sin6_family = AF_INET6;
                    break;
                }
                default:
                    log_internal(MTC_LOG_ERR, "%s: Unsupported address type: %d\n", __func__, result->ai_family);
                    freeaddrinfo(result);
                    return MTC_ERROR_CF_INVALID_FORMAT;
            }

            freeaddrinfo(result);
        }
    }
    c->common.hostnum++;
    return MTC_SUCCESS; // SUCCESS
}

//
//++
//
//  NAME:
//
//      walk_parameters_config
//
//  DESCRIPTION:
//
//      walk parameters subtree and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      doc - pointer for xml doc
//      cur - xml node for parameters element
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static MTC_S32 
walk_parameters_config(
    xmlDocPtr doc,
    xmlNodePtr cur,
    HA_CONFIG *c) 
{
    xmlNodePtr sub;
    xmlChar *txt;
    char *endptr;
    MTC_U32 i;

    //
    // Name and value pair
    //

    struct 
    {
        char *name;
        MTC_U32 *value;
    } 
    tbl[] = 
        {{"HeartbeatInterval", &(c->common.heartbeat_interval)},
         {"HeartbeatTimeout", &(c->common.heartbeat_timeout)},
         {"StateFileInterval", &(c->common.statefile_interval)},
         {"StateFileTimeout", &(c->common.statefile_timeout)},
         {"HeartbeatWatchdogTimeout",&(c->common.heartbeat_watchdog_timeout)},
         {"StateFileWatchdogTimeout",&(c->common.statefile_watchdog_timeout)},
         {"BootJoinTimeout",&(c->common.boot_join_timeout)},
         {"EnableJoinTimeout",&(c->common.enable_join_timeout)},
         {"XapiHealthCheckInterval",&(c->common.xapi_healthcheck_interval)},
         {"XapiHealthCheckTimeout",&(c->common.xapi_healthcheck_timeout)},
         {"XapiRestartAttempts",&(c->common.xapi_restart_attempts)},
         {"XapiRestartTimeout",&(c->common.xapi_restart_timeout)},
         {"XapiLicenseCheckTimeout",&(c->common.xapi_licensecheck_timeout)},
         {NULL, NULL}};


    //
    // Walk XML
    //

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {
        for (i = 0; tbl[i].name != NULL; i++) 
        {
            if (xmlStrcmp(sub->name, (const xmlChar *) tbl[i].name) == 0) 
            {
                txt = xmlNodeListGetString(doc, sub->children, 1);
                if (txt == NULL) 
                {
                    log_internal(MTC_LOG_ERR, "%s: failed to get %s\n", __func__, tbl[i].name);
                    return MTC_ERROR_CF_INVALID_FORMAT;
                }
                *tbl[i].value = strtol((char *) txt, &endptr, 10);
                if (*endptr != '\0') 
                {
                    log_internal(MTC_LOG_ERR, "%s: invalid %s %s\n", __func__, tbl[i].name, txt);
                    xmlFree(txt);
                    return MTC_ERROR_CF_INVALID_FORMAT;
                }
                xmlFree(txt);
                break;
            }
        }
    }
    return MTC_SUCCESS; // SUCCESS
}

//
//++
//
//  NAME:
//
//      walk_localhost
//
//  DESCRIPTION:
//
//      walk localhost subtree and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      doc - pointer for xml doc
//      cur - xml node for localhost element
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//

static MTC_S32 
walk_localhost(
    xmlDocPtr doc,
    xmlNodePtr cur,
    HA_CONFIG *c) 
{
    char host_id[MTC_UUID_SIZE];
    xmlNodePtr sub;
    xmlChar *txt;
    MTC_U32 i;

    //
    // Walk XML
    //

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {

        //
        // HostID
        //

        if (xmlStrcmp(sub->name, (const xmlChar *) "HostID") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get HostID\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            if (uuid_strcpy(host_id, (char *) txt) != MTC_SUCCESS)
            {
                log_internal(MTC_LOG_ERR, "%s: invalid HostID %s\n", __func__, txt);
                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            for (i = 0; i < c->common.hostnum; i++) 
            {
                if (!strncmp(host_id, c->common.host[i].host_id, MTC_UUID_SIZE)) 
                {
                    c->local.localhost_index = i;
                    break;
                }
            }
            if (i >= c->common.hostnum) 
            {
                // localhost is not found in host list

                log_internal(MTC_LOG_ERR, "%s: localhost %s is not found in host list\n", __func__, txt);
                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            xmlFree(txt);
        }

        //
        // HeartbeatInterface
        //

        else if (xmlStrcmp(sub->name, (const xmlChar *) "HeartbeatInterface") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get HeartbeatInterface\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            if (strlen((char *) txt) < HEARTBEAT_INTERFACE_LEN) 
            {
                strcpy(c->local.heartbeat_interface, (char *) txt);
            }
            else 
            {
                strncpy(c->local.heartbeat_interface, (char *) txt, HEARTBEAT_INTERFACE_LEN - 1);
                c->local.heartbeat_interface[HEARTBEAT_INTERFACE_LEN - 1] = '\0';
            }
            xmlFree(txt);
        }

        //
        // HeartbeatPhysicalInterface
        //

        else if (xmlStrcmp(sub->name, (const xmlChar *) "HeartbeatPhysicalInterface") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get HeartbeatPhysicalInterface\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            if (strlen((char *) txt) < HEARTBEAT_INTERFACE_LEN) 
            {
                strcpy(c->local.heartbeat_physical_interface, (char *) txt);
            }
            else 
            {
                strncpy(c->local.heartbeat_physical_interface, (char *) txt, HEARTBEAT_INTERFACE_LEN - 1);
                c->local.heartbeat_interface[HEARTBEAT_INTERFACE_LEN - 1] = '\0';
            }
            xmlFree(txt);
        }

        //
        // StateFile
        //

        else if (xmlStrcmp(sub->name, (const xmlChar *) "StateFile") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get StateFile\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            if (strlen((char *) txt) < STATEFILE_PATH_LEN) 
            {
                strcpy(c->local.statefile_path, (char *) txt);
            }
            else 
            {
                strncpy(c->local.statefile_path, (char *) txt, STATEFILE_PATH_LEN -1);
                c->local.statefile_path[STATEFILE_PATH_LEN - 1] = '\0';
            }

            xmlFree(txt);
        }

        //
        // Watchdog Mode
        //

        else if (xmlStrcmp(sub->name, (const xmlChar *) "WatchdogMode") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get StateFile\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }

            if (strlen((char *) txt) < WATCHDOG_MODE_LEN) 
            {
                strcpy(c->local.watchdog_mode, (char *) txt);
            }
            else 
            {
                strncpy(c->local.watchdog_mode, (char *) txt, WATCHDOG_MODE_LEN -1);
                c->local.watchdog_mode[WATCHDOG_MODE_LEN - 1] = '\0';
            }
            xmlFree(txt);
        }
    }
    return MTC_SUCCESS; // SUCCESS
}


//
//++
//
//  NAME:
//
//      walk_common_config
//
//  DESCRIPTION:
//
//      walk common_config subtree and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      doc - pointer for xml doc
//      cur - xml node for common-config element
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static MTC_S32 
walk_common_config(
    xmlDocPtr doc,
    xmlNodePtr cur,
    HA_CONFIG *c) 
{
    xmlNodePtr sub;
    xmlChar *txt;
    MTC_S32 ret;

    //
    // Walk XML
    //

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {
        if (xmlStrcmp(sub->name, (const xmlChar *) "GenerationUUID") == 0) 
        {
            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get GenerationUUID\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            if (uuid_strcpy(c->common.generation_uuid, (char *) txt) != MTC_SUCCESS) 
            {
                log_internal(MTC_LOG_ERR, "%s: invalid GenerationUUID %s\n", __func__, txt);
                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            xmlFree(txt);
        }
        else if (xmlStrcmp(sub->name, (const xmlChar *) "UDPport") == 0) 
        {
            char *endptr;

            txt = xmlNodeListGetString(doc, sub->children, 1);
            if (txt == NULL) 
            {
                log_internal(MTC_LOG_ERR, "%s: failed to get UDPport\n", __func__);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            c->common.udp_port = strtol((char *) txt, &endptr, 10);
            if (*endptr != '\0' || c->common.udp_port == 0) 
            {
                log_internal(MTC_LOG_ERR, "%s: invalid UDPport %s\n", __func__, txt);
                xmlFree(txt);
                return MTC_ERROR_CF_INVALID_FORMAT;
            }
            xmlFree(txt);
        }
        else if (xmlStrcmp(sub->name, (const xmlChar *) "host") == 0) 
        {
            if ((ret = walk_host_config(doc, sub, c)) != MTC_SUCCESS) 
            {
                return ret;
            }
        }
        else if (xmlStrcmp(sub->name, (const xmlChar *) "parameters") == 0) 
        {
            if ((ret = walk_parameters_config(doc, sub, c)) != MTC_SUCCESS) 
            {
                return ret;
            }
        }
    }

    qsort(c->common.host, (size_t) c->common.hostnum, 
          sizeof(HA_CONFIG_HOST_INFO), host_info_compar);

    return MTC_SUCCESS; // SUCCESS
}

//
//++
//
//  NAME:
//
//      walk_local_config
//
//  DESCRIPTION:
//
//      walk local_config subtree and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      doc - pointer for xml doc
//      cur - xml node for local-config element
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//


static MTC_S32
walk_local_config(
    xmlDocPtr doc,
    xmlNodePtr cur,
    HA_CONFIG *c) 
{
    xmlNodePtr sub;
    MTC_S32 ret;

    //
    // Walk XML
    //

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {
        if (xmlStrcmp(sub->name, (const xmlChar *) "localhost") == 0) 
        {
            if ((ret = walk_localhost(doc, sub, c)) != MTC_SUCCESS) 
            {
                return ret;
            }
        }
    }
    return MTC_SUCCESS; // SUCCESS
}


//
//++
//
//  NAME:
//
//      interpret_config_file
//
//  DESCRIPTION:
//
//      interpret config-file and set HA_CONFIG
//
//  FORMAL PARAMETERS:
//
//      path - filepath of config-file
//      c - HA_CONFIG
//
//  RETURN VALUE:
//
//      0: success
//      other: failed
//
//  ENVIRONMENT:
//
//      None
//
//--
//

MTC_S32 
interpret_config_file(
    MTC_S8 *path,
    HA_CONFIG *c)
{
    xmlChar *txt;
    xmlDocPtr doc;
    xmlNodePtr cur, sub;
    MTC_S32 ret;
    struct stat buf;

    //
    // parameter check
    //

    if (path == NULL) 
    {
        log_internal(MTC_LOG_ERR, "%s: config-path is NULL\n", __func__);
        return MTC_ERROR_CF_OPEN;
    }

    if (c == NULL) 
    {
        log_internal(MTC_LOG_ERR, "%s: HA_CONFIG is NULL\n", __func__);
        return MTC_ERROR_CF_INVALID_PARAMETER;
    }

    //
    // file check
    //

    if (stat(path, &buf) != 0) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) not found\n", path);
        return MTC_ERROR_CF_OPEN;
    }

    //
    // fill default value
    //

    set_default_value(c);
    
    //
    // poison host_index
    //

    c->local.localhost_index = (MTC_U32) -1;

    if ((doc = xmlParseFile(path)) == NULL) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) contains invalid XML text.\n", path);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }

    //
    // get root element and check version
    //

    if ((cur = xmlDocGetRootElement(doc)) == NULL) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) contains invalid data.\n", path);
        log_internal(MTC_LOG_ERR, "%s: faild to xmlDocGetRootElement %s\n", __func__, path);
        xmlFreeDoc(doc);
        return MTC_ERROR_CF_INVALID_FORMAT;
        
    }
    if (xmlStrcmp( cur->name, (const xmlChar *) "xhad-config")) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) contains invalid data.\n", path);
        log_internal(MTC_LOG_ERR, "%s: name of root element is invalid\n", __func__);
        xmlFreeDoc(doc);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }

    if ((txt = xmlGetProp(cur, (const xmlChar *) "version")) == NULL) 
    {
        // attribute "version" not found
        log_message(MTC_LOG_ERR, "CF: config file (%s) contains invalid data.\n", path);
        log_internal(MTC_LOG_ERR, "%s: failed to get version\n", __func__);

        xmlFreeDoc(doc);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }
    if (!valid_version((char *) txt)) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) version mismatch.\n", path);
        log_internal(MTC_LOG_ERR, "%s: version %s is invalid\n", __func__, txt);

        xmlFree(txt);
        xmlFreeDoc(doc);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }
    strcpy(c->version, (char *) txt);
    xmlFree(txt);
    
    //
    // Walk XML
    //

    for (sub = cur->children; sub != NULL; sub = sub->next) 
    {
        if (xmlStrcmp(sub->name, (const xmlChar *) "common-config") == 0) 
        {
            if ((ret = walk_common_config(doc, sub, c)) != MTC_SUCCESS) 
            {
                log_message(MTC_LOG_ERR, "CF: config-file (%s) contains invalid data.\n", path);
                xmlFreeDoc(doc);
                return ret;
            }
        }
        else if (xmlStrcmp(sub->name, (const xmlChar *) "local-config") == 0) 
        {
            if ((ret = walk_local_config(doc, sub, c)) != MTC_SUCCESS) 
            {
                log_message(MTC_LOG_ERR, "CF: config file (%s) contains invalid data.\n", path);
                xmlFreeDoc(doc);
                return ret;
            }
        }
    }

    //
    // check output
    //

    if (!valid_config(c)) 
    {
        log_message(MTC_LOG_ERR, "CF: config-file (%s) contains invalid data.\n", path);
        xmlFreeDoc(doc);
        return MTC_ERROR_CF_INVALID_FORMAT;
    }
    xmlFreeDoc(doc);
    return MTC_SUCCESS; // SUCCESS
}

