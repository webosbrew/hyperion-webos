#pragma once

#include <stdint.h>

#include "unicapture.h"

typedef struct _converter {
    // Temporary conversion memory buffers
    uint8_t* buffers[4];
} converter_t;

void converter_init(converter_t* converter);
int converter_release(converter_t* converter);
int converter_run(converter_t* this, frame_info_t* input, frame_info_t* output, pixel_format_t target_format);
