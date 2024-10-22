#pragma once

#include "log.h"
#include <pbnjson.h>

#define RPC_PORT 8090

typedef enum _AmbientLightingDaemon {
    DAEMON_INVALID = -1,
    DAEMON_NOT_SET,
    DAEMON_HYPERION_NG,
    DAEMON_HYPERHDR
} AmbientLightingDaemon;

typedef enum _DynamicRange {
    SDR,
    HDR10,
    DOLBYVISION,
} DynamicRange;

const char* daemon_to_string(AmbientLightingDaemon flavor);
int do_http_post(char* url, const char* post_body, char** response_body, int out_buf_sz);
int send_rpc_message(char* host, ushort rpc_port, jvalue_ref post_body_jval, jvalue_ref* response_body_jval);
int get_daemon_flavor(char* host, ushort rpc_port, AmbientLightingDaemon* flavor);
DynamicRange get_dynamic_range(const char* dynamic_range);
int set_hdr_state(char* host, ushort rpc_port, DynamicRange range);