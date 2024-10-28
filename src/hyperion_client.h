#pragma once
#include <stdbool.h>
#include <stdint.h>

int hyperion_client(const char* origin, const char* hostname, int port, bool unix_socket, int priority);
int hyperion_read();
int hyperion_destroy();
int hyperion_set_image(const unsigned char* image, int width, int height);
int hyperion_set_nv12_image(const uint8_t* y, const uint8_t* uv, int width, int height, int stride_y, int stride_uv);
int hyperion_set_register(const char* origin, int priority);