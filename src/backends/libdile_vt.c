#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include <dile_vt.h>

#include "log.h"
#include "unicapture.h"
#include "quirks.h"
#include "utils.h"

typedef struct _dile_vt_backend_state {
    DILE_VT_HANDLE vth;
    DILE_OUTPUTDEVICE_STATE output_state;
    DILE_VT_FRAMEBUFFER_PROPERTY vfbprop;
    DILE_VT_FRAMEBUFFER_CAPABILITY vfbcap;
    uint8_t*** vfbs;
    int mem_fd;
} dile_vt_backend_state_t;

int capture_init(cap_backend_config_t* config, void** state_p)
{
    int ret = 0;
    DILE_VT_HANDLE vth = NULL;

    INFO("Capture start called.");

    if (HAS_QUIRK(config->quirks, QUIRK_DILE_VT_CREATE_EX)) {
        INFO("[QUIRK_DILE_VT_CREATE_EX]: Attempting DILE_VT_CreateEx...");
        vth = DILE_VT_CreateEx(0, 1);
    } else {
        INFO("Attempting DILE_VT_Create...");
        vth = DILE_VT_Create(0);
    }

    if (vth == NULL) {
        WARN("Failed to get DILE_VT context!");
        return -1;
    }

    dile_vt_backend_state_t* this = calloc(1, sizeof(dile_vt_backend_state_t));
    *state_p = this;
    this->vth = vth;

    DBG("Got DILE_VT context!");

    DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_LIMITATION limitation;
    if (DILE_VT_GetVideoFrameOutputDeviceLimitation(vth, &limitation) != 0) {
        ret = -11;
        goto err_destroy;
    }

    DBG("supportScaleUp: %d; (%dx%d)", limitation.supportScaleUp, limitation.scaleUpLimitWidth, limitation.scaleUpLimitHeight);
    DBG("supportScaleDown: %d; (%dx%d)", limitation.supportScaleDown, limitation.scaleDownLimitWidth, limitation.scaleDownLimitHeight);
    DBG("maxResolution: %dx%d", limitation.maxResolution.width, limitation.maxResolution.height);
    DBG("input deinterlace: %d; display deinterlace: %d", limitation.supportInputVideoDeInterlacing, limitation.supportDisplayVideoDeInterlacing);

    DILE_VT_DUMP_LOCATION_TYPE_T dump_location = DILE_VT_DISPLAY_OUTPUT;

    if (DILE_VT_SetVideoFrameOutputDeviceDumpLocation(vth, dump_location) != 0) {
        WARN("[DILE_VT] DISPLAY dump location failed, attempting SCALER...");
        dump_location = DILE_VT_SCALER_OUTPUT;
        if (DILE_VT_SetVideoFrameOutputDeviceDumpLocation(vth, dump_location) != 0) {
            ERR("[DILE_VT] SCALER dump location failed, attempting UNDOCUMENTED DUMP LOCATION: 2!");
            dump_location = 2;
            if (DILE_VT_SetVideoFrameOutputDeviceDumpLocation(vth, dump_location) != 0) {
                ERR("[DILE_VT] SetVideoFrameOutputDeviceDumpLocation failed!");
                ret = -2;
                goto err_destroy;
            }
        }
    }

    DILE_VT_RECT region = { 0, 0, config->resolution_width, config->resolution_height };

    if (region.width < limitation.scaleDownLimitWidth || region.height < limitation.scaleDownLimitHeight) {
        WARN("scaledown is limited to %dx%d while %dx%d has been chosen - there's a chance this will crash!", limitation.scaleDownLimitWidth, limitation.scaleDownLimitHeight, region.width, region.height);
    }

    if (DILE_VT_SetVideoFrameOutputDeviceOutputRegion(vth, dump_location, &region) != 0) {
        ERR("[DILE_VT] SetVideoFrameOutputDeviceOutputRegion failed!");
        ret = -3;
        goto err_destroy;
    }

    this->output_state.enabled = 0;
    this->output_state.freezed = 0;
    this->output_state.appliedPQ = 0;

    // Framerate provided by libdile_vt is dependent on content currently played
    // (50/60fps). We don't have any better way of limiting FPS, so let's just
    // average it out - if someone sets their preferred framerate to 30 and
    // content was 50fps, we'll just end up feeding 25 frames per second.
    this->output_state.framerate = config->fps == 0 ? 1 : 60 / config->fps;
    INFO("[DILE_VT] framerate divider: %d", this->output_state.framerate);

    DILE_VT_WaitVsync(vth, 0, 0);
    uint64_t t1 = getticks_us();
    DILE_VT_WaitVsync(vth, 0, 0);
    uint64_t t2 = getticks_us();

    double fps = 1000000.0 / (t2 - t1);
    INFO("[DILE_VT] frametime: %d; estimated fps before divider: %.5f", (uint32_t)(t2 - t1), fps);

    // Set framerate divider
    if (DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FRAMERATE_DIVIDE, &this->output_state) != 0) {
        ERR("[DILE_VT] SetVideoFrameOutputDeviceState FRAMERATE_DIVIDE failed!");
        ret = -4;
        goto err_destroy;
    }

    DILE_VT_WaitVsync(vth, 0, 0);
    t1 = getticks_us();
    DILE_VT_WaitVsync(vth, 0, 0);
    t2 = getticks_us();

    fps = 1000000.0 / (t2 - t1);
    INFO("[DILE_VT] frametime: %d; estimated fps after divider: %.5f", (uint32_t)(t2 - t1), fps);

    // Set freeze
    if (DILE_VT_SetVideoFrameOutputDeviceState(vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &this->output_state) != 0) {
        ERR("[DILE_VT] SetVideoFrameOutputDeviceState FREEZE failed!");
        ret = -5;
        goto err_destroy;
    }

    // Prepare offsets table (needs to be ptr[vfbcap.numVfs][vbcap.numPlanes])
    if (DILE_VT_GetVideoFrameBufferCapability(vth, &this->vfbcap) != 0) {
        ERR("[DILE_VT] GetVideoFrameBufferCapability failed!");
        ret = -9;
        goto err_destroy;
    }

    INFO("[DILE_VT] vfbs: %d; planes: %d", this->vfbcap.numVfbs, this->vfbcap.numPlanes);
    uint32_t** ptr = calloc(sizeof(uint32_t*), this->vfbcap.numVfbs);
    for (uint32_t vfb = 0; vfb < this->vfbcap.numVfbs; vfb++) {
        ptr[vfb] = calloc(sizeof(uint32_t), this->vfbcap.numPlanes);
    }

    this->vfbprop.ptr = ptr;

    if (DILE_VT_GetAllVideoFrameBufferProperty(vth, &this->vfbcap, &this->vfbprop) != 0) {
        ERR("[DILE_VT] GetAllVideoFrameBufferProperty failed!");
        ret = -10;
        goto err_destroy;
    }

    // TODO: check out if /dev/gfx is something we should rather use
    this->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (this->mem_fd == -1) {
        ERR("[DILE_VT] Creating handle to /dev/mem failed!");
        ret = -6;
        goto err_destroy;
    }

    INFO("[DILE_VT] pixelFormat: %d; width: %d; height: %d; stride: %d...", this->vfbprop.pixelFormat, this->vfbprop.width, this->vfbprop.height, this->vfbprop.stride);
    this->vfbs = calloc(this->vfbcap.numVfbs, sizeof(uint8_t**));
    for (uint32_t vfb = 0; vfb < this->vfbcap.numVfbs; vfb++) {
        this->vfbs[vfb] = calloc(this->vfbcap.numPlanes, sizeof(uint8_t*));
        for (uint32_t plane = 0; plane < this->vfbcap.numPlanes; plane++) {
            DBG("[DILE_VT] vfb[%d][%d] = 0x%08x", vfb, plane, this->vfbprop.ptr[vfb][plane]);
            this->vfbs[vfb][plane] = (uint8_t*)mmap(0, this->vfbprop.stride * this->vfbprop.height, PROT_READ, MAP_SHARED, this->mem_fd, this->vfbprop.ptr[vfb][plane]);
            if (this->vfbs[vfb][plane] == MAP_FAILED) {
                ERR("[DILE_VT] mmap for vfb[%d][%d] failed!", vfb, plane);
                ret = -7;
                goto err_mmap;
            }
        }
    }

    return 0;

err_mmap:
    // TODO: do we need to unmap mmaped memory here as well?
    close(this->mem_fd);

err_destroy:
    DILE_VT_Destroy(vth);
    free(this);
    return ret;
}

int capture_cleanup(void* state)
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;
    DILE_VT_Destroy(this->vth);
    free(this);
    return 0;
}

int capture_start(void* state)
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;
    if (DILE_VT_Start(this->vth) != 0) {
        return -1;
    }
    return 0;
}

int capture_terminate(void* state)
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;
    DILE_VT_Stop(this->vth);
    return 0;
}

int capture_acquire_frame(void* state, frame_info_t* frame)
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;

    uint32_t idx = 0;

    this->output_state.freezed = 1;
    DILE_VT_SetVideoFrameOutputDeviceState(this->vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &this->output_state);

    DILE_VT_GetCurrentVideoFrameBufferProperty(this->vth, NULL, &idx);

    frame->width = this->vfbprop.width;
    frame->height = this->vfbprop.height;

    if (this->vfbprop.pixelFormat == DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_RGB) {
        frame->pixel_format = PIXFMT_RGB;
        // Note: vfbprop.width is equal to stride for some reason.
        frame->width = this->vfbprop.stride / 3;
        frame->planes[0].buffer = this->vfbs[idx][0];
        frame->planes[0].stride = this->vfbprop.stride;
    } else if (this->vfbprop.pixelFormat == DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_YUV420_SEMI_PLANAR) {
        frame->pixel_format = PIXFMT_YUV420_SEMI_PLANAR;
        frame->planes[0].buffer = this->vfbs[idx][0];
        frame->planes[0].stride = this->vfbprop.stride;
        frame->planes[1].buffer = this->vfbs[idx][1];
        frame->planes[1].stride = this->vfbprop.stride;
    } else if (this->vfbprop.pixelFormat == DILE_VT_VIDEO_FRAME_BUFFER_PIXEL_FORMAT_YUV422_SEMI_PLANAR) {
        frame->pixel_format = PIXFMT_YUV422_SEMI_PLANAR;
        frame->planes[0].buffer = this->vfbs[idx][0];
        frame->planes[0].stride = this->vfbprop.stride;
        frame->planes[1].buffer = this->vfbs[idx][1];
        frame->planes[1].stride = this->vfbprop.stride;
    } else {
        ERR("[DILE_VT] Unhandled pixelformat: 0x%x!", this->vfbprop.pixelFormat);
        return -1;
    }

    return 0;
}

int capture_release_frame(void* state, frame_info_t* frame __attribute__((unused)))
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;
    this->output_state.freezed = 0;
    DILE_VT_SetVideoFrameOutputDeviceState(this->vth, DILE_VT_VIDEO_FRAME_OUTPUT_DEVICE_STATE_FREEZED, &this->output_state);
    return 0;
}

int capture_wait(void* state)
{
    dile_vt_backend_state_t* this = (dile_vt_backend_state_t*)state;
    DILE_VT_WaitVsync(this->vth, 0, 0);
    return 0;
}
