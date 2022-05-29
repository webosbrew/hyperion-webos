#pragma once

#include <pbnjson.h>
#include "log.h"

typedef enum _AmbientLightingDaemon {
    DAEMON_INVALID = -1,
    DAEMON_NOT_SET,
    DAEMON_HYPERION_NG,
    DAEMON_HYPERHDR
} AmbientLightingDaemon;

const char *daemon_to_string(AmbientLightingDaemon flavor) {
    switch (flavor) {
        case DAEMON_INVALID:
            return "INVALID";
        case DAEMON_NOT_SET:
            return "NOT SET";
        case DAEMON_HYPERION_NG:
            return "Hyperion.NG";
        case DAEMON_HYPERHDR:
            return "HyperHDR";
        default:
            return "<UNKNOWN>";
    }
}