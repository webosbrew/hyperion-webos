#include <stdexcept>
#include <stdlib.h> // calloc()
#include <unistd.h> // usleep()

#include "log.h"
#include "unicapture.h"

extern "C" {
#include <vtcapture/vtCaptureApi_c.h>

typedef struct _vtcapture_backend_state {
    int width;
    int height;
    int stride;
    VT_DRIVER* driver; // = NULL;
    VT_CLIENTID_T client[128];
    _LibVtCaptureBufferInfo buff;
    _LibVtCaptureProperties props;
    char* curr_buff;
    bool terminate;
} vtcapture_backend_state_t;

int capture_terminate(void* state);

int capture_init(cap_backend_config_t* config, void** state_p)
{
    int ret = 0;

    INFO("Starting vtcapture initialization.");

    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)calloc(1, sizeof(vtcapture_backend_state_t));

    try {
        if (!(self->driver = vtCapture_create())) {
            ERR("Could not create vtcapture driver.");
            ret = -1;
            goto err_destroy;
        }
    } catch (const std::runtime_error& err) {
        WARN("vtCapture_create() failed: %s", err.what());
        ret = -2;
        goto err_destroy;
    }

    INFO("Driver created!");

    // Sorry, no unlimited fps for you.
    self->props.frm = config->fps == 0 ? 60 : config->fps;
    self->props.dump = 2;
    self->props.loc.x = 0;
    self->props.loc.y = 0;
    self->props.reg.w = config->resolution_width;
    self->props.reg.h = config->resolution_height;
    self->props.buf_cnt = 3;

    *state_p = self;

    return 0;

err_destroy:
    if (self->driver) {
        vtCapture_release(self->driver);
    }
    free(self);

    return ret;
}

int capture_cleanup(void* state)
{
    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)state;

    vtCapture_postprocess(self->driver, self->client);
    vtCapture_finalize(self->driver, self->client);
    vtCapture_release(self->driver);
    free(self);

    return 0;
}

int capture_start(void* state)
{
    int ret;
    const VT_CALLER_T* caller = "hyperion-webos_service";
    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)state;

    sprintf(self->client, "%s", "00");

    if ((ret = vtCapture_init(self->driver, caller, self->client)) == 17) {

        ERR("vtCapture_init not ready yet return: %d", ret);
        ret = -2;
        goto err_init;
    } else if (ret == 11) {

        ERR("vtCapture_init failed: %d Permission denied! Quitting...", ret);
        ret = -3;
        goto err_init;
    } else if (ret != 0) {

        ERR("vtCapture_init failed: %d Quitting...", ret);
        ret = -4;
        goto err_init;
    }
    INFO("vtCapture_init done! Caller_ID: %s Client ID: %s", caller, self->client);

    if ((vtCapture_preprocess(self->driver, self->client, &self->props)) == 1) {

        ERR("vtCapture_preprocess not ready yet return: %d", ret);
        ret = -5;
        goto err_preprocess;
    } else if (ret != 0) {
        ERR("vtCapture_preprocess failed: %x Quitting...", ret);
        ret = -6;
        goto err_preprocess;
    }
    INFO("vtCapture_preprocess done!");

    _LibVtCapturePlaneInfo plane;
    if ((vtCapture_planeInfo(self->driver, self->client, &plane)) != 0) {

        ERR("vtCapture_planeInfo failed: %xQuitting...", ret);
        ret = -7;
        goto err_planeinfo;
    }

    self->width = plane.planeregion.c;
    self->height = plane.planeregion.d;
    self->stride = plane.stride;

    INFO("vtCapture_planeInfo done! stride: %d Region: x: %d, y: %d, w: %d, h: %d Active Region: x: %d, y: %d w: %d h: %d",
        plane.stride, plane.planeregion.a, plane.planeregion.b, plane.planeregion.c, plane.planeregion.d,
        plane.activeregion.a, plane.activeregion.b, plane.activeregion.c, plane.activeregion.d);

    self->terminate = false;

    INFO("vtcapture initialization finished.");

    if ((vtCapture_process(self->driver, self->client)) != 0) {

        ERR("vtCapture_process failed: %xQuitting...", ret);
        ret = -1;
        goto err_process;
    }
    INFO("vtCapture_process done!");

    return 0;

err_process:
    vtCapture_stop(self->driver, self->client);

err_planeinfo:
    vtCapture_postprocess(self->driver, self->client);

err_preprocess:
    vtCapture_finalize(self->driver, self->client);

err_init:
    return ret;
}

int capture_terminate(void* state)
{
    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)state;

    self->terminate = true;

    vtCapture_stop(self->driver, self->client);
    vtCapture_postprocess(self->driver, self->client);
    vtCapture_finalize(self->driver, self->client);

    return 0;
}

int capture_acquire_frame(void* state, frame_info_t* frame)
{
    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)state;
    _LibVtCaptureBufferInfo buff;

    if (vtCapture_currentCaptureBuffInfo(self->driver, &buff) != 0) {

        ERR("vtCapture_currentCaptureBuffInfo() failed.");
        return -1;
    }

    frame->pixel_format = PIXFMT_YUV420_SEMI_PLANAR; // ToDo: I guess?!
    frame->width = self->width;
    frame->height = self->height;
    frame->planes[0].buffer = buff.start_addr0;
    frame->planes[0].stride = self->stride;
    frame->planes[1].buffer = buff.start_addr1;
    frame->planes[1].stride = self->stride;

    self->curr_buff = self->buff.start_addr0;

    return 0;
}

int capture_release_frame(void* state __attribute__((unused)), frame_info_t* frame __attribute__((unused)))
{
    return 0;
}

int capture_wait(void* state)
{
    vtcapture_backend_state_t* self = (vtcapture_backend_state_t*)state;
    uint32_t attempt_count = 0;

    // wait until buffer address changed
    while (!self->terminate) {
        if (vtCapture_currentCaptureBuffInfo(self->driver, &self->buff) != 0) {

            ERR("vtCapture_currentCaptureBuffInfo() failed.");
            return -1;
        }

        if (self->curr_buff != self->buff.start_addr0)
            break;

        attempt_count += 1;
        if (attempt_count >= 1000000 / 100) {
            // Prevent hanging...
            WARN("captureCurrentBuffInfo() never returned a new plane!");
            return 1;
        }
        usleep(100);
    }

    self->curr_buff = self->buff.start_addr0;

    return 0;
}
}
