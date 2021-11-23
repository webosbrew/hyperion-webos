/*
 * Function pointers for usage by dlopen @ main.c
 */

#pragma once

typedef int (*_capture_preinit)(void);
typedef int (*_capture_init)(void);
typedef int (*_capture_start)(void);
typedef int (*_capture_terminate)(void);
typedef int (*_capture_cleanup)(void);
