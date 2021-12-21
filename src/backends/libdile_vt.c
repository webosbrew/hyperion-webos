#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>

#include <libyuv.h>
#include <dile_vt.h>
#include <gm.h>

#include "common.h"


cap_backend_config_t config = {0, 0, 0, 0};
cap_imagedata_callback_t imagedata_cb = NULL;

pthread_t capture_thread = NULL;
pthread_t vsync_thread = NULL;

pthread_mutex_t vsync_lock;
pthread_cond_t vsync_cond;

bool use_vsync_thread = true;
bool capture_running = true;

DILE_VT_HANDLE vth = NULL;
DILE_OUTPUTDEVICE_STATE output_state;
DILE_VT_FRAMEBUFFER_PROPERTY vfbprop;
DILE_VT_FRAMEBUFFER_CAPABILITY vfbcap;

GM_SURFACE gm_surface;

uint8_t*** vfbs;
int mem_fd = 0;

void* capture_thread_target(void* data);
void* vsync_thread_target(void* data);

uint64_t getticks_us()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

int capture_preinit(cap_backend_config_t *backend_config, cap_imagedata_callback_t callback)
{
    memcpy(&config, backend_config, sizeof(cap_backend_config_t));
    imagedata_cb = callback;

    return 0;
}

int capture_init() {
    if (getenv("NO_VSYNC_THREAD") != NULL) {
        use_vsync_thread = false;
        fprintf(stderr, "[DILE_VT] Disabling vsync thread\n");
    }
    return 0;
}

int capture_terminate() {
    capture_running = false;

    if (capture_thread != NULL) {
        pthread_join(capture_thread, NULL);
    }

    if (use_vsync_thread && vsync_thread != NULL) {
        pthread_join(vsync_thread, NULL);
    }

    GM_DestroySurface(gm_surface.surfaceID);

    DILE_VT_Stop(vth);

    return 0;
}

int capture_cleanup() {
    if (vth != NULL) {
        DILE_VT_Destroy(vth);
    }
    return 0;
}

int capture_start()
{
    vth = DILE_VT_Create(0);
    if (vth == NULL) {
        return -1;
    }

    DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_LIMITATION limitation;
    if (DILE_VT_GetVideoFrameOutputDeviceLimitation(vth, &limitation) != 0) {
        return -11;
    }

    fprintf(stderr, "[DILE_VT] supportScaleUp: %d; (%dx%d)\n", limitation.supportScaleUp, limitation.scaleUpLimitWidth, limitation.scaleUpLimitHeight);
    fprintf(stderr, "[DILE_VT] supportScaleDown: %d; (%dx%d)\n", limitation.supportScaleDown, limitation.scaleDownLimitWidth, limitation.scaleDownLimitHeight);
    fprintf(stderr, "[DILE_VT] maxResolution: %dx%d\n", limitation.maxResolution.width, limitation.maxResolution.height);
    fprintf(stderr, "[DILE_VT] input deinterlace: %d; display deinterlace: %d\n", limitation.supportInputVideoDeInterlacing, limitation.supportDisplayVideoDeInterlacing);

    if (DILE_VT_SetVideoFrameOutputDeviceDumpLocation(vth, DILE_VT_DISPLAY_OUTPUT) != 0) {
        return -2;
    }

    DILE_VT_RECT region = {0, 0, config.resolution_width, config.resolution_height};

    if (region.width < limitation.scaleDownLimitWidth || region.height < limitation.scaleDownLimitHeight) {
        fprintf(stderr, "[DILE_VT] scaledown limit, overriding to %dx%d\n", limitation.scaleDownLimitWidth, limitation.scaleDownLimitHeight);
        region.width = limitation.scaleDownLimitWidth;
        region.height = limitation.scaleDownLimitHeight;
    }

    if (DILE_VT_SetVideoFrameOutputDeviceOutputRegion(vth, DILE_VT_DISPLAY_OUTPUT, &region) != 0) {
        return -3;
    }

    output_state.enabled = 0;
    output_state.freezed = 0;
    output_state.appliedPQ = 0;

    // Framerate provided by libdile_vt is dependent on content currently played
    // (50/60fps). We don't have any better way of limiting FPS, so let's just
    // average it out - if someone sets their preferred framerate to 30 and
    // content was 50fps, we'll just end up feeding 25 frames per second.
    output_state.framerate = config.fps == 0 ? 1 : 60 / config.fps;
    fprintf(stderr, "[DILE_VT] framerate divider: %d\n", output_state.framerate);

    DILE_VT_WaitVsync(vth, 0, 0);
    uint64_t t1 = getticks_us();
    DILE_VT_WaitVsync(vth, 0, 0);
    uint64_t t2 = getticks_us();

    double fps = 1000000.0 / (t2 - t1);
    fprintf(stderr, "[DILE_VT] frametime: %d; estimated fps before divider: %.5f\n", t2 - t1, fps);

    // Set framerate divider
    if (DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FRAMERATE_DIVIDE, &output_state) != 0) {
        return -4;
    }

    DILE_VT_WaitVsync(vth, 0, 0);
    t1 = getticks_us();
    DILE_VT_WaitVsync(vth, 0, 0);
    t2 = getticks_us();

    fps = 1000000.0 / (t2 - t1);
    fprintf(stderr, "[DILE_VT] frametime: %d; estimated fps after divider: %.5f\n", t2 - t1, fps);

    // Set freeze
    if (DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &output_state) != 0) {
        return -5;
    }

    // Prepare offsets table (needs to be ptr[vfbcap.numVfs][vbcap.numPlanes])
    if (DILE_VT_GetVideoFrameBufferCapability(vth, &vfbcap) != 0) {
        return -9;
    }

    fprintf(stderr, "[DILE_VT] vfbs: %d; planes: %d\n", vfbcap.numVfbs, vfbcap.numPlanes);
    uint32_t** ptr = calloc(sizeof(uint32_t*), vfbcap.numVfbs);
    for (int vfb = 0; vfb < vfbcap.numVfbs; vfb++) {
        ptr[vfb] = calloc(sizeof(uint32_t), vfbcap.numPlanes);
    }

    vfbprop.ptr = ptr;

    if (DILE_VT_GetAllVideoFrameBufferProperty(vth, &vfbcap, &vfbprop) != 0) {
        return -10;
    }

    // TODO: check out if /dev/gfx is something we should rather use
    mem_fd = open("/dev/mem", O_RDWR|O_SYNC);
    if (mem_fd == -1) {
        return -6;
    }

    fprintf(stderr, "[DILE_VT] pixelFormat: %d; width: %d; height: %d; stride: %d...\n", vfbprop.pixelFormat, vfbprop.width, vfbprop.height, vfbprop.stride);
    vfbs = calloc(vfbcap.numVfbs, sizeof(uint8_t**));
    for (int vfb = 0; vfb < vfbcap.numVfbs; vfb++) {
        vfbs[vfb] = calloc(vfbcap.numPlanes, sizeof(uint8_t*));
        for (int plane = 0; plane < vfbcap.numPlanes; plane++) {
            fprintf(stderr, "[DILE_VT] vfb[%d][%d] = 0x%08x\n", vfb, plane, vfbprop.ptr[vfb][plane]);
            vfbs[vfb][plane] = (uint8_t*) mmap(0, vfbprop.stride * vfbprop.height, PROT_READ, MAP_SHARED, mem_fd, vfbprop.ptr[vfb][plane]);
        }
    }

    if (GM_CreateSurface(region.width, region.height, 0, &gm_surface) != 0) {
        return -13;
    }

    if (DILE_VT_Start(vth) != 0) {
       return -12;
    }

    if (pthread_create (&capture_thread, NULL, capture_thread_target, NULL) != 0) {
        return -7;
    }

    if (use_vsync_thread) {
        if (pthread_create (&vsync_thread, NULL, vsync_thread_target, NULL) != 0) {
            return -8;
        }
    }
}

uint64_t framecount = 0;
uint64_t start_time = 0;
uint32_t idx = 0;

void dump_buffer(uint8_t* buf, uint64_t size, uint32_t idx, uint32_t plane) {
    char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/hyperion-webos-dump.%03d.%03d.raw", idx, plane);
    FILE* fd = fopen(filename, "wb");
    fwrite(buf, size, 1, fd);
    fclose(fd);
    fprintf(stderr, "[DILE_VT] Dumped buffer to: %s\n", filename);
}

void capture_frame() {
    static uint8_t* outbuf = NULL;

    if (use_vsync_thread) {
        pthread_mutex_lock(&vsync_lock);
        pthread_cond_wait(&vsync_cond, &vsync_lock);
        pthread_mutex_unlock(&vsync_lock);
    } else {
        DILE_VT_WaitVsync(vth, 0, 0);
    }

    output_state.freezed = 1;
    DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &output_state);

    DILE_VT_GetCurrentVideoFrameBufferProperty(vth, NULL, &idx);

    uint64_t now = getticks_us();
    if (framecount % 30 == 0) {
        fprintf(stderr, "[DILE_VT] framerate: %.6f FPS\n", (30.0 * 1000000) / (now - start_time));
        start_time = now;
    }

    framecount += 1;


    if (vfbprop.pixelFormat == DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_RGB) {
        // Note: vfbprop.width is equal to stride for some reason.
        uint32_t width = vfbprop.stride / 3;
        uint32_t height = vfbprop.height;

        uint32_t gmwidth = width;
        uint32_t gmheight = height;

        static uint8_t* argbvideo = NULL;
        static uint8_t* argbblended = NULL;
        static uint8_t* argbui = NULL;

        if (argbvideo == NULL) {
            argbvideo = malloc(4 * width * height);
        }

        if (argbblended == NULL) {
            argbblended = malloc(4 * width * height);
        }

        if (argbui == NULL) {
            argbui = malloc(4 * width * height);
        }

        if (outbuf == NULL) {
            // Temporary conversion buffer
            outbuf = malloc(3 * width * height);
        }

        uint64_t t1 = getticks_us();
        GM_CaptureGraphicScreen(gm_surface.surfaceID, &gmwidth, &gmheight);
        ABGRToARGB(gm_surface.framebuffer, 4 * width, argbui, 4 * width, width, height);
        RGB24ToARGB(vfbs[idx][0], vfbprop.stride, argbvideo, 4 * width, width, height);
        ARGBBlend(argbui, 4 * width, argbvideo, 4 * width, argbblended, 4 * width, width, height);
        ARGBToRGB24(argbblended, 4 * width, outbuf, 3 * width, width, height);
        imagedata_cb(width, height, outbuf);
        uint64_t t7 = getticks_us();

        if (framecount % 15 == 0) {
            fprintf(stderr, "[DILE_VT] frame feed time: %.3fms\n", (t7 - t1) / 1000.0);
        }
    } else if (vfbprop.pixelFormat == DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_YUV420_SEMI_PLANAR) {
        if (outbuf == NULL) {
            // Temporary conversion buffer
            outbuf = malloc (vfbprop.width * vfbprop.height * 3);
        }

        NV21ToRGB24(vfbs[idx][0], vfbprop.stride, vfbs[idx][1], vfbprop.stride, outbuf, vfbprop.width * 3, vfbprop.width, vfbprop.height);
        imagedata_cb(vfbprop.width, vfbprop.height, outbuf);
    } else {
        fprintf(stderr, "[DILE_VT] Unsupported pixel format: %d\n", vfbprop.pixelFormat);
        for (int plane = 0; plane < vfbcap.numPlanes; plane++) {
            dump_buffer(vfbs[idx][plane], vfbprop.stride * vfbprop.height, idx, plane);
        }
    }

    output_state.freezed = 0;
    DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &output_state);
}

void* capture_thread_target(void* data) {
    while (capture_running) {
        capture_frame();
    }
}

void* vsync_thread_target(void* data) {
    while (capture_running) {
        DILE_VT_WaitVsync(vth, 0, 0);
        pthread_mutex_lock(&vsync_lock);
        pthread_cond_signal(&vsync_cond);
        pthread_mutex_unlock(&vsync_lock);
    }
}
