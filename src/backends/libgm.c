#include <gm.h>
#include <stdlib.h>

#include "unicapture.h"

typedef struct _gm_backend_state {
    int width;
    int height;
    GM_SURFACE surface_info;
} gm_backend_state_t;

int capture_init(cap_backend_config_t* config, void** state_p)
{
    int ret = 0;

    gm_backend_state_t* state = calloc(1, sizeof(gm_backend_state_t));
    state->width = config->resolution_width;
    state->height = config->resolution_height;

    if ((ret = GM_CreateSurface(state->width, state->height, 0, &state->surface_info)) != 0) {
        free(state);
        return ret;
    }

    *state_p = state;

    return 0;
}

int capture_cleanup(void* state)
{
    gm_backend_state_t* this = (gm_backend_state_t*)state;
    GM_DestroySurface(this->surface_info.surfaceID);
    free(state);
    return 0;
}

int capture_start(void* state) { return 0; }
int capture_terminate(void* state) { return 0; }

int capture_acquire_frame(void* state, frame_info_t* frame)
{
    gm_backend_state_t* this = (gm_backend_state_t*)state;
    int ret = 0;
    int width = this->width;
    int height = this->height;

    if ((ret = GM_CaptureGraphicScreen(this->surface_info.surfaceID, &width, &height)) != 0) {
        return ret;
    }

    frame->pixel_format = PIXFMT_ABGR;
    frame->width = width;
    frame->height = height;
    frame->planes[0].buffer = this->surface_info.framebuffer;
    frame->planes[0].stride = width * 4;

    return 0;
}

int capture_release_frame(void* state, frame_info_t* frame)
{
    return 0;
}

int capture_wait(void* state)
{
    return 0;
}
