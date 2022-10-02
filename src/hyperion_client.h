#pragma once
#include <stdbool.h>

int hyperion_client(const char* origin, const char* hostname, int port, bool unix_socket, int priority);
int hyperion_read();
int hyperion_destroy();
int hyperion_set_image(const unsigned char* image, int width, int height);
int hyperion_set_register(const char* origin, int priority);