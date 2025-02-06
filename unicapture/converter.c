#include "converter.h"
#include <libyuv.h>
#include <string.h>

void converter_init(converter_t* this)
{
    for (int i = 0; i < MAX_PLANES; i++) {
        this->buffers[i] = NULL;
    }
}

int converter_release(converter_t* converter)
{
    for (int i = 0; i < MAX_PLANES; i++) {
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
    if (input->pixel_format == target_format) {
        output->width = input->width;
        output->height = input->height;
        output->pixel_format = input->pixel_format;
        for (int i = 0; i < MAX_PLANES; i++) {
            int size = input->height * input->planes[i].stride;
            if (i == 1 && input->pixel_format == PIXFMT_YUV420_SEMI_PLANAR) {
                // technically a stride of width is correct but the assumption of the amount
                // of bytes as stride * height is wrong.
                size /= 2;
            }

            output->planes[i].stride = input->planes[i].stride;
            if (input->planes[i].buffer) {
                this->buffers[i] = realloc(this->buffers[i], size);
                memcpy(this->buffers[i], input->planes[i].buffer, size);
                output->planes[i].buffer = this->buffers[i];
            }
        }
        return 0;
    }

    if (target_format == PIXFMT_ARGB) {
        output->width = input->width;
        output->height = input->height;

        this->buffers[0] = realloc(this->buffers[0], output->width * output->height * 4);

        output->planes[0].buffer = this->buffers[0];
        output->planes[0].stride = output->width * 4;

        if (input->pixel_format == PIXFMT_RGB) {
            RAWToARGB(
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
            NV12ToARGB(
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
        } else if (input->pixel_format == PIXFMT_ARGB) {
            output->planes[0].buffer = input->planes[0].buffer;
            output->planes[0].stride = input->planes[0].stride;
        } else {
            return -2;
        }

        output->pixel_format = PIXFMT_ARGB;
        return 0;
    }
    if (target_format == PIXFMT_YUV420_SEMI_PLANAR && input->pixel_format == PIXFMT_ARGB) {
        this->buffers[0] = realloc(this->buffers[0], input->width * input->height);
        this->buffers[1] = realloc(this->buffers[1], input->width * input->height / 2);

        output->width = input->width;
        output->height = input->height;
        output->planes[0].buffer = this->buffers[0];
        output->planes[0].stride = output->width;
        output->planes[1].buffer = this->buffers[1];
        output->planes[1].stride = output->width;
        output->pixel_format = PIXFMT_YUV420_SEMI_PLANAR;

        ARGBToNV12(input->planes[0].buffer,
            input->planes[0].stride,
            output->planes[0].buffer,
            output->planes[0].stride,
            output->planes[1].buffer,
            output->planes[1].stride,
            output->width,
            output->height);
        return 0;
    }
    if (target_format == PIXFMT_RGB && input->pixel_format == PIXFMT_ARGB) {
        this->buffers[0] = realloc(this->buffers[0], input->width * input->height * 3);

        output->width = input->width;
        output->height = input->height;
        output->pixel_format = PIXFMT_RGB;
        output->planes[0].buffer = this->buffers[0];
        output->planes[0].stride = output->width * 3;

        ARGBToRAW(input->planes[0].buffer,
            input->planes[0].stride,
            output->planes[0].buffer,
            output->planes[0].stride,
            output->width,
            output->height);
        return 0;
    } else {
        // Only support ARGB for now...
        return -1;
    }
}
