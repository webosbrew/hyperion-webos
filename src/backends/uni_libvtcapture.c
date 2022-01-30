#include <vtcapture/vtCaptureApi_c.h>
#include <stdlib.h> // calloc()
#include <unistd.h> // usleep()

#include "unicapture.h"
#include "log.h"


typedef struct _vtcapture_backend_state {
    int width;
    int height;
    int stride;
    VT_DRIVER *driver; // = NULL;
    VT_CLIENTID_T client[128];
    _LibVtCaptureBufferInfo buff;
    char *curr_buff;
} vtcapture_backend_state_t;


int capture_terminate(void* state);


int capture_init(cap_backend_config_t* config, void** state_p)
{
    int ret = 0;

    INFO("Starting vtcapture initialization.");

    vtcapture_backend_state_t* this = calloc(1, sizeof(vtcapture_backend_state_t));

    if (!(this->driver = vtCapture_create())) {
        
        ERR("Could not create vtcapture driver.");
        ret = -1; goto err_destroy;
    }
    INFO("Driver created!");

    // ToDo: caller and client fixed here. OK?
    const VT_CALLER_T *caller = "hyperion-webos_service";
    sprintf(this->client, "%s", "00");

    if ((ret = vtCapture_init(this->driver, caller, this->client)) == 17) {

        ERR("vtCapture_init not ready yet return: %d", ret);
        ret = -2; goto err_destroy;
    }
    else if (ret == 11) {

        ERR("vtCapture_init failed: %d Permission denied! Quitting...", ret);
        ret = -3; goto err_destroy;
    }
    else if (ret != 0) {

        ERR("vtCapture_init failed: %d Quitting...", ret);
        ret = -4; goto err_destroy;
    }
    INFO("vtCapture_init done! Caller_ID: %s Client ID: %s", caller, this->client);

    _LibVtCaptureProperties props;

    VT_RESOLUTION_T resolution = {config->resolution_width, config->resolution_height};
    VT_DUMP_T dump = 2;
    VT_LOC_T loc = {0, 0};
    VT_BUF_T buf_cnt = 3;

    // Sorry, no unlimited fps for you.
    if (config->fps == 0)
        config->fps = 60;

    INFO("fps = %d", config->fps);

    props.dump = dump;
    props.loc = loc;
    props.reg = resolution;
    props.buf_cnt = buf_cnt;
    props.frm = config->fps;

    if ((vtCapture_preprocess(this->driver, this->client, &props)) == 1) {

        ERR("vtCapture_preprocess not ready yet return: %d", ret);
        ret = -5; goto err_destroy;
    }
    else if (ret != 0) {
        
        ERR("vtCapture_preprocess failed: %x Quitting...", ret);
        ret = -6; goto err_destroy;
    }
    INFO("vtCapture_preprocess done!");

    _LibVtCapturePlaneInfo plane;
    if ((vtCapture_planeInfo(this->driver, this->client, &plane)) != 0 ) {

        ERR("vtCapture_planeInfo failed: %xQuitting...", ret);
        ret = -7; goto err_destroy;
    }

    this->width = plane.activeregion.c;
    this->height = plane.activeregion.d;
    this->stride = plane.stride;

    INFO("vtCapture_planeInfo done! stride: %d Region: x: %d, y: %d, w: %d, h: %d Active Region: x: %d, y: %d w: %d h: %d", 
            plane.stride, plane.planeregion.a, plane.planeregion.b, plane.planeregion.c, plane.planeregion.d, 
            plane.activeregion.a, plane.activeregion.b, plane.activeregion.c, plane.activeregion.d);

    INFO("vtcapture initialization finished.");

    *state_p = this;

    return 0;

err_destroy:
    INFO("DESTROY");
    if (this->driver) {
        vtCapture_postprocess(this->driver, this->client);
        vtCapture_finalize(this->driver, this->client);
        vtCapture_release(this->driver);
    }
    free(this);
    return ret;
}

int capture_cleanup(void* state)
{
    vtcapture_backend_state_t* this = (vtcapture_backend_state_t*) state;
    vtCapture_postprocess(this->driver, this->client);
    vtCapture_finalize(this->driver, this->client);
    vtCapture_release(this->driver);
    free(this);
    return 0;
}

int capture_start(void* state)
{
    int ret;
    vtcapture_backend_state_t* this = (vtcapture_backend_state_t*) state;

    if ((vtCapture_process(this->driver, this->client)) != 0) {

        ERR("vtCapture_process failed: %xQuitting...", ret);
        return -1;
    }
    INFO("vtCapture_process done!");

    int cnter = 0;
    do {

        usleep(100000);

        if ((ret = vtCapture_currentCaptureBuffInfo(this->driver, &this->buff)) == 0 ) {

            break;
        }
        else if (ret != 2) {

            ERR("vtCapture_currentCaptureBuffInfo failed: %x Quitting...", ret);
            capture_terminate(state);
            return -1;
        }
        cnter++;
    } while(ret != 0);

    INFO("vtCapture_currentCaptureBuffInfo done after %d tries! addr0: %p addr1: %p size0: %d size1: %d", 
            cnter, this->buff.start_addr0, this->buff.start_addr1, this->buff.size0, this->buff.size1);

    return 0;
}

int capture_terminate(void* state)
{    
    vtcapture_backend_state_t* this = (vtcapture_backend_state_t*) state;
    vtCapture_stop(this->driver, this->client);
    return 0;
}

int capture_acquire_frame(void* state, frame_info_t* frame)
{
    vtcapture_backend_state_t* this = (vtcapture_backend_state_t*) state;

    if (vtCapture_currentCaptureBuffInfo(this->driver, &this->buff) != 0 ) {

        ERR("vtCapture_currentCaptureBuffInfo() failed.");
        return -1;
    }

    frame->pixel_format = PIXFMT_YUV420_SEMI_PLANAR; // ToDo: I guess?!
    frame->width = this->width;
    frame->height = this->height;
    frame->planes[0].buffer = this->buff.start_addr0;
    frame->planes[0].stride = this->stride;
    frame->planes[1].buffer = this->buff.start_addr0;
    frame->planes[1].stride = this->stride;

    this->curr_buff = this->buff.start_addr0;

    return 0;
}

int capture_release_frame(void* state, frame_info_t* frame)
{
    return 0;
}

int capture_wait(void* state)
{
    vtcapture_backend_state_t* this = (vtcapture_backend_state_t*) state;

    // wait until buffer address changed
    while (true) {
    
        if (vtCapture_currentCaptureBuffInfo(this->driver, &this->buff) != 0 ) {

            ERR("vtCapture_currentCaptureBuffInfo() failed.");
            return -1;
        }

        if (this->curr_buff != this->buff.start_addr0)
            break;

        usleep(10000);
    }

    return 0;
}
