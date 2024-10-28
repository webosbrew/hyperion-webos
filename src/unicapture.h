#pragma once
#include "common.h"
#include <pthread.h>
#include <stdint.h>

typedef enum _pixel_format {
    PIXFMT_INVALID = 0,
    PIXFMT_YUV420_PLANAR = 1,
    PIXFMT_YUV420_SEMI_PLANAR = 2,
    PIXFMT_YUV420_INTERLEAVED = 3,
    PIXFMT_YUV422_PLANAR = 4,
    PIXFMT_YUV422_SEMI_PLANAR = 5,
    PIXFMT_YUV422_INTERLEAVED = 6,
    PIXFMT_YUV444_PLANAR = 7,
    PIXFMT_YUV444_SEMI_PLANAR = 8,
    PIXFMT_YUV444_INTERLEAVED = 9,
    PIXFMT_RGB = 10,
    PIXFMT_ABGR = 11,
    PIXFMT_ARGB = 12,
} pixel_format_t;

#define MAX_PLANES 4
typedef struct _frame_info {
    pixel_format_t pixel_format;
    int width;
    int height;

    struct {
        uint8_t* buffer;
        int stride;
    } planes[MAX_PLANES];

    int internal_id;
} frame_info_t;

// Initialize capture backend - if this step succeeds, capture_cleanup **needs
// to be executed**. Otherwise, any allocated resources need to be freed by the
// backend itself.
typedef int (*capture_init_t)(cap_backend_config_t*, void**);

// Start capture - if this step succeeds, capture_terminate **needs to be
// executed**. Otherwise, the backend needs to be reinitialized.
typedef int (*capture_start_t)(void*);
typedef int (*capture_terminate_t)(void*);

// Wait for next video frame/vsync
typedef int (*capture_wait_t)(void*);

// Capture single video frame - if this step succeeds, capture_release_frame
// **needs to be executed**.
typedef int (*capture_acquire_frame_t)(void*, frame_info_t*);

// Called after frame processing has been finished
typedef int (*capture_release_frame_t)(void*, frame_info_t*);

typedef int (*capture_cleanup_t)(void*);

typedef struct _capture_backend {
    char* name;
    void* state;

    capture_init_t init;
    capture_cleanup_t cleanup;

    capture_start_t start;
    capture_terminate_t terminate;

    capture_wait_t wait;

    capture_acquire_frame_t acquire_frame;
    capture_release_frame_t release_frame;
} capture_backend_t;

/*
 * Callback for submitting new image data to hyperion
 * To be called from capture backend implementation
 */
typedef int (*unicapture_imagedata_callback_t)(void* data, int width, int height, uint8_t* rgb_data);
typedef int (*unicapture_imagedata_nv12_callback_t)(void* data, int width, int height, uint8_t* y, uint8_t* uv, int stride_y, int stride_uv);

typedef struct _unicapture_state {
    capture_backend_t* ui_capture;
    capture_backend_t* video_capture;

    bool vsync;
    uint32_t fps;
    pixel_format_t target_format;

    bool running;
    pthread_t main_thread;

    bool ui_capture_running;
    bool video_capture_running;

    bool vsync_thread_running;
    pthread_t vsync_thread;
    pthread_mutex_t vsync_lock;
    pthread_cond_t vsync_cond;

    unicapture_imagedata_callback_t callback;
    unicapture_imagedata_nv12_callback_t callback_nv12;
    void* callback_data;

    bool dump_frames;

    struct {
        double framerate;
    } metrics;
} unicapture_state_t;

#ifdef __cplusplus
extern "C" {
#endif
void unicapture_init(unicapture_state_t* state);
int unicapture_try_backends(cap_backend_config_t* config, capture_backend_t* backend, char** candidates);
int unicapture_init_backend(cap_backend_config_t* config, capture_backend_t* backend, char* name);
int unicapture_start(unicapture_state_t* state);
int unicapture_stop(unicapture_state_t* state);
#ifdef __cplusplus
}
#endif
