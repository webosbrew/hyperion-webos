#include "log.h"
#include <stdint.h>
#include <time.h>
#include <PmLogLib.h>

uint64_t start = 0;
PmLogContext context;
LogLevel current_log_level = Info;

uint64_t getticks_us()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

void log_init() {
    PmLogGetContext("hyperion-webos", &context);
}

void log_set_level(LogLevel level) {
    current_log_level = level;
}

void log_printf(LogLevel level, const char* module, const char* fmt, ...) {
    va_list args;
    va_start (args, fmt);
    const char* level_str;
    const char* color_str;

    if (start == 0)
        start = getticks_us();

    if (level == Debug) {
        level_str = "DBG";
        color_str = "\x1b[34m";
    } else if (level == Info) {
        level_str = "INFO";
        color_str = "\x1b[36m";
    } else if (level == Warning) {
        level_str = "WARN";
        color_str = "\x1b[33m";
    } else {
        level_str = "ERR";
        color_str = "\x1b[31m";
    }

    char formatted[1024];
    vsnprintf(formatted, 1024, fmt, args);
    _PmLogMsgKV(context, level, 0, module, 0, NULL, NULL, formatted);

    if (level <= current_log_level) {
        fprintf(stderr, "%10.3fs %s[%4s %-20s]\x1b[0m %s\n", (getticks_us() - start)/1000000.0, color_str, level_str, module, formatted);
    }

    va_end (args);
}
