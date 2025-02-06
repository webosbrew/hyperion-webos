#include <fcntl.h> // open()
#include <halgal.h>
#include <stdlib.h> // calloc()
#include <sys/mman.h> // munmap()
#include <unistd.h> // close()

#include "log.h"
#include "unicapture.h"

typedef struct _halgal_backend_state {
    int width;
    int height;
    HAL_GAL_SURFACE surface_info;
    size_t num_bytes;
    char* mem_addr;
    int mem_fd;
} halgal_backend_state_t;

int capture_init(cap_backend_config_t* config, void** state_p)
{
    int ret = 0;

    halgal_backend_state_t* this = calloc(1, sizeof(halgal_backend_state_t));
    this->width = config->resolution_width;
    this->height = config->resolution_height;

    INFO("Graphical capture enabled. Begin init..");
    if (getuid() != 0) {
        ERR("Init for graphical capture not possible. Not running as root!");
        ret = -1;
        return ret;
    }

    if ((ret = HAL_GAL_Init()) != 0) {

        ERR("HAL_GAL_Init failed: %x", ret);
        ret = -1;
        goto err_destroy;
    }
    INFO("HAL_GAL_Init done! Exit: %d", ret);

    if ((ret = HAL_GAL_CreateSurface(this->width, this->height, 0, &this->surface_info)) != 0) {

        ERR("HAL_GAL_CreateSurface failed: %x", ret);
        ret = -2;
        goto err_destroy;
    }
    INFO("HAL_GAL_CreateSurface done! SurfaceID: %d", this->surface_info.vendorData);

    if ((ret = HAL_GAL_CaptureFrameBuffer(&this->surface_info)) != 0) {

        ERR("HAL_GAL_CaptureFrameBuffer failed: %x", ret);
        ret = -3;
        goto err_destroy;
    }
    INFO("HAL_GAL_CaptureFrameBuffer done! %x", ret);

    // ToDo: compare open("/dev/mem", O_RDWR|O_SYNC);
    if ((this->mem_fd = open("/dev/gfx", 2)) < 0) {

        ERR("HAL_GAL: gfx open fail result: %d", this->mem_fd);
        ret = -4;
        goto err_destroy;
    }
    INFO("HAL_GAL: gfx open ok result: %d", this->mem_fd);

    this->num_bytes = this->surface_info.property;

    if (this->num_bytes == 0)
        this->num_bytes = this->surface_info.pitch * this->surface_info.height;

    // ToDo: compare mmap(0, this->vfbprop.stride * this->vfbprop.height, PROT_READ, MAP_SHARED, this->mem_fd, this->vfbprop.ptr[vfb][plane]);
    if ((this->mem_addr = (char*)mmap(0, this->num_bytes, 3, 1, this->mem_fd, this->surface_info.offset)) == MAP_FAILED) {

        ERR("HAL_GAL: mmap() failed");
        ret = -5;
        goto err_mmap;
    }
    INFO("HAL_GAL: mmap() success");
    // ToDo: Should be OK to close this->mem_fd right here

    *state_p = this;

    return 0;

err_mmap:
    // TODO: do we need to unmap mmaped memory here as well?
    // -> We will only end up here when mmap() failed.
    close(this->mem_fd);

err_destroy:
    HAL_GAL_DestroySurface(&this->surface_info);
    free(this);

    return ret;
}

int capture_cleanup(void* state)
{
    halgal_backend_state_t* this = (halgal_backend_state_t*)state;

    HAL_GAL_DestroySurface(&this->surface_info);
    munmap(this->mem_addr, this->num_bytes);
    close(this->mem_fd);
    free(this);

    return 0;
}

int capture_start(void* state __attribute__((unused)))
{
    return 0;
}

int capture_terminate(void* state __attribute__((unused)))
{
    return 0;
}

int capture_acquire_frame(void* state, frame_info_t* frame)
{
    halgal_backend_state_t* this = (halgal_backend_state_t*)state;
    int ret = 0;

    if ((ret = HAL_GAL_CaptureFrameBuffer(&this->surface_info)) != 0) {

        ERR("HAL_GAL_CaptureFrameBuffer failed: %x", ret);
        return ret;
    }

    frame->pixel_format = PIXFMT_ARGB;
    frame->width = this->width;
    frame->height = this->height;
    frame->planes[0].buffer = (uint8_t*)this->mem_addr;
    frame->planes[0].stride = this->surface_info.pitch;

    return 0;
}

int capture_release_frame(void* state __attribute__((unused)), frame_info_t* frame __attribute__((unused)))
{
    return 0;
}

int capture_wait(void* state __attribute__((unused)))
{
    return 0;
}
