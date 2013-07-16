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


#include "cfread.h"

static void
set_default_value(
    XHAD_CONFIG *c)
{
    c->udp_port = UDP_PORT_DEFAULT;
    c->heartbeat_interval = HEARTBEAT_INTERVAL_DEFAULT;
    c->heartbeat_timeout = HEARTBEAT_TIMEOUT_DEFAULT;
    c->statefile_interval = STATEFILE_INTERVAL_DEFAULT;
    c->statefile_timeout = STATEFILE_TIMEOUT_DEFAULT;
    c->boot_join_timeout = BOOT_JOIN_TIMEOUT_DEFAULT;
    c->enable_join_timeout = ENABLE_JOIN_TIMEOUT_DEFAULT;
    c->xapi_healthcheck_interval = XAPI_HEALTH_CHECK_INTERVAL_DEFAULT;
    c->xapi_healthcheck_timeout = XAPI_HEALTH_CHECK_TIMEOUT_DEFAULT;
    c->xapi_restart_retry = XAPI_RESTART_RETRY_DEFAULT;
    c->xapi_restart_timeout = XAPI_REATART_TIMEOUT_DEFAULT;
    c->xapi_licensecheck_timeout = XAPI_LICENSE_CHECK_TIMEOUT;
}


static void
parsenode(
    xmlTextReaderPtr reader,
    RssState *state)
{
    xmlElementType node_type;
    xmlChar *element_name;
    
    node_type = xmlTextReaderNodeType(reader);
    switch (node_type) {
    XML_READER_TYPE_ELEMENT:
        element_name = xmlTextReadername(reader);
        if (*state == STATE_START) {
            if (xmlStrcmp(element_name
        }
        break;
    XML_READER_TYPE_END_ELEMENT:
        break;
    XML_READER_TYPE_TEXT:
        break;
    default:
        break;
    }

}

MTC_S32
read_config_file(
    MTC_U8 *path,
    XHAD_CONFIG *c)
{
    xmlTextReaderPtr reader;
    int ret;
    RssState state = STATE_START;

    reader = xmlNewTextReaderFileName(path);
    if (reader = NULL) {
        /* log */
        return CFREADER_ERROR_OPEN;
    }
    while ((ret = xmdTextReaderRead(reader)) == 1) {
        perse_node(reader, &state);
    }
    xmlFreeTextReader(reader);
    xmlCleanupParser();
}

