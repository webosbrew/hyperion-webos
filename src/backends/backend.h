#pragma once
#include "common.h"

int capture_preinit(cap_backend_config_t*);
int capture_init();
int capture_start();
int capture_terminate();
int capture_cleanup();