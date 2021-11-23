#pragma once

/*
 * Common configuration for all backends
 */
typedef struct _cap_backend_config {
    int fps;
    int framedelay_us;
    int resolution_width;
    int resolution_height;
} cap_backend_config_t;

#if defined(CAPTURE_BACKEND)
    extern int capture_preinit(cap_backend_config_t*);
    extern int capture_init();
    extern int capture_start();
    extern int capture_terminate();
    extern int capture_cleanup();
#endif

/*
 * Function pointers for usage by dlopen @ main.c
 */
typedef int (*_capture_preinit_t)(cap_backend_config_t*);
typedef int (*_capture_init_t)(void);
typedef int (*_capture_start_t)(void);
typedef int (*_capture_terminate_t)(void);
typedef int (*_capture_cleanup_t)(void);

typedef struct _cap_backend_funcs {
    _capture_preinit_t _capture_preinit;
    _capture_init_t _capture_init;
    _capture_start_t _capture_start;
    _capture_terminate_t _capture_terminate;
    _capture_cleanup_t _capture_cleanup;
} cap_backend_funcs_t;
