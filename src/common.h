#pragma once
#include <stdint.h>

/*
 * Callback for submitting new image data to hyperion
 * To be called from capture backend implementation
 */
typedef int (*cap_imagedata_callback_t)(int width, int height, uint8_t* rgb_data);


/*
 * Common configuration for all backends
 * Gets passed from main app to the dlopen'd capture backend initialization
 */
typedef struct _cap_backend_config {
    int fps;
    int framedelay_us;
    int resolution_width;
    int resolution_height;
    bool no_video;
    bool no_gui;
    bool save_config;
    bool load_config;
    bool no_service;
} cap_backend_config_t;

#if defined(CAPTURE_BACKEND)
    /*
     * Ensuring capture backends implement needed functionality
     */
    extern int capture_preinit(cap_backend_config_t*, cap_imagedata_callback_t);
    extern int capture_init();
    extern int capture_start();
    extern int capture_terminate();
    extern int capture_cleanup();
#endif

/*
 * Function pointers for usage by dlopen in main app
 */
typedef int (*capture_preinit_t)(cap_backend_config_t*, cap_imagedata_callback_t);
typedef int (*capture_init_t)(void);
typedef int (*capture_start_t)(void);
typedef int (*capture_terminate_t)(void);
typedef int (*capture_cleanup_t)(void);

typedef struct _cap_backend_funcs {
    capture_preinit_t capture_preinit;
    capture_init_t capture_init;
    capture_start_t capture_start;
    capture_terminate_t capture_terminate;
    capture_cleanup_t capture_cleanup;
} cap_backend_funcs_t;