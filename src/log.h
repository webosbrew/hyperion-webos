#pragma once
#include <stdarg.h>
#include <stdio.h>

typedef enum {
    Error = 3,
    Warning = 4,
    Info = 6,
    Debug = 7,
} LogLevel;

void log_init();
void log_set_level(LogLevel level);
void log_printf(LogLevel level, const char* module, const char* fmt, ...);

#define LOG(level, ...) log_printf(level, __FUNCTION__, __VA_ARGS__)
#define ERR(...) LOG(Error, __VA_ARGS__)
#define WARN(...) LOG(Warning, __VA_ARGS__)
#define INFO(...) LOG(Info, __VA_ARGS__)
#define DBG(...) LOG(Debug, __VA_ARGS__)
