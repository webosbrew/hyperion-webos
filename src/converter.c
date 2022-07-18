#include "converter.h"
#include <libyuv.h>

void converter_init(converter_t* this)
{
    for (int i = 0; i < 4; i++) {
        this->buffers[i] = NULL;
    }
}

int converter_release(converter_t* converter)
{
    for (int i = 0; i < 4; i++) {
        if (converter->buffers[i] != NULL) {
            free(converter->buffers[i]);
        }
    }
    return 0;
}

/**
 * Perform frame conversion - all temporary buffers are stored in converter_t
 * state variable - a single converter instance may be reused multiple times,
 * however output frame shall not be used after converter_release, or when
 * another converter_run call has been issued.
 */
int converter_run(converter_t* this, frame_info_t* input, frame_info_t* output, pixel_format_t target_format)
{
    if (target_format == PIXFMT_ARGB) {
        output->width = input->width;
        output->height = input->height;

        this->buffers[0] = realloc(this->buffers[0], output->width * output->height * 4);

        output->planes[0].buffer = this->buffers[0];
        output->planes[0].stride = output->width * 4;

        if (input->pixel_format == PIXFMT_RGB) {
            RGB24ToARGB(
                input->planes[0].buffer,
                input->planes[0].stride,
                output->planes[0].buffer,
                output->planes[0].stride,
                output->width,
                output->height);
        } else if (input->pixel_format == PIXFMT_ABGR) {
            ABGRToARGB(
                input->planes[0].buffer,
                input->planes[0].stride,
                output->planes[0].buffer,
                output->planes[0].stride,
                output->width,
                output->height);
        } else if (input->pixel_format == PIXFMT_YUV420_SEMI_PLANAR) {
            NV21ToARGB(
                input->planes[0].buffer,
                input->planes[0].stride,
                input->planes[1].buffer,
                input->planes[1].stride,
                output->planes[0].buffer,
                output->planes[0].stride,
                output->width,
                output->height);
        } else if (input->pixel_format == PIXFMT_YUV422_SEMI_PLANAR) {
            this->buffers[1] = realloc(this->buffers[1], input->width / 2 * input->height);
            this->buffers[2] = realloc(this->buffers[2], input->width / 2 * input->height);
            SplitUVPlane(
                input->planes[1].buffer,
                input->planes[1].stride,
                this->buffers[1],
                input->width / 2,
                this->buffers[2],
                input->width / 2,
                input->width / 2,
                input->height);
            I422ToARGB(
                input->planes[0].buffer,
                input->planes[0].stride,
                this->buffers[2],
                input->width / 2,
                this->buffers[1],
                input->width / 2,
                output->planes[0].buffer,
                output->planes[0].stride,
                output->width,
                output->height);
        } else {
            return -2;
        }

        output->pixel_format = PIXFMT_ARGB;
        return 0;
    } else {
        // Only support ARGB for now...
        return -1;
    }
}
