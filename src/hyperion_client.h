#pragma once

int hyperion_client(const char *origin, const char *hostname, int port, int priority);
int hyperion_read();
int hyperion_destroy();
void hyperion_set_image(const unsigned char *image, int width, int height);
void hyperion_set_register(const char *origin, int priority);