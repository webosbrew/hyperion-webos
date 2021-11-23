#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>

#include <pthread.h>

#include "common.h"
#include "hyperion_client.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static struct option long_options[] = {
    {"width", optional_argument, 0, 'x'},
    {"height", optional_argument, 0, 'y'},
    {"address", required_argument, 0, 'a'},
    {"port", optional_argument, 0, 'p'},
    {"fps", optional_argument, 0, 'f'},
    {0, 0, 0, 0},
};

bool app_quit = false;

static const char *_address = NULL;
static int _port = 19400;

static cap_backend_config_t config = {15, 0, 192, 108};
static cap_backend_funcs_t backend = {NULL};

static int image_data_cb(int width, int height, uint8_t *rgb_data);

static int detect_backend() {
    /*
     * TODO
     * - Detect used rendering backend
     * - dlopen particular capture backend
     * - dlsym functions into cap_backend_funcs_t struct
     */

    return 0;
}

static void handle_signal(int signal)
{
    switch (signal)
    {
    case SIGINT:
        app_quit = true;
        hyperion_destroy();
        break;
    default:
        break;
    }
}

static void print_usage()
{
    printf("Usage: hyperion-webos -a ADDRESS [OPTION]...\n");
    printf("\n");
    printf("Grab screen content continously and send to Hyperion via flatbuffers server.\n");
    printf("\n");
    printf("  -x, --width           Width of video frame (default 192)\n");
    printf("  -y, --height          Height of video frame (default 108)\n");
    printf("  -a, --address         IP address of Hyperion server\n");
    printf("  -p, --port            Port of Hyperion flatbuffers server (default 19400)\n");
    printf("  -f, --fps             Framerate for sending video frames (default 15)\n");
}

static int parse_options(int argc, char *argv[])
{
    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "x:y:a:p:f:", long_options, &longindex)) != -1)
    {
        switch (opt)
        {
        case 'x':
            config.resolution_width = atoi(optarg);
            break;
        case 'y':
            config.resolution_height = atoi(optarg);
            break;
        case 'a':
            _address = strdup(optarg);
            break;
        case 'p':
            _port = atol(optarg);
            break;
        case 'f':
            config.fps = atoi(optarg);
            break;
        }
    }
    if (!_address)
    {
        fprintf(stderr, "Error! Address not specified.\n");
        print_usage();
        return 1;
    }
    if (config.fps < 0 || config.fps > 60)
    {
        fprintf(stderr, "Error! FPS should between 0 (unlimited) and 60.\n");
        print_usage();
        return 1;
    }
    if (config.fps == 0)
        config.framedelay_us = 0;
    else
        config.framedelay_us = 1000000 / config.fps;
    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    if ((ret = parse_options(argc, argv)) != 0)
    {
        return ret;
    }

    if (getenv("XDG_RUNTIME_DIR") == NULL)
    {
        setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
    }

    if ((ret = detect_backend()) != 0)
    {
        fprintf(stderr, "Error! detect_backend.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_preinit(&config, &image_data_cb)) != 0)
    {
        fprintf(stderr, "Error! capture_preinit.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_init()) != 0)
    {
        fprintf(stderr, "Error! capture_initialize.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_start()) != 0)
    {
        fprintf(stderr, "Error! capture_start.\n");
        goto cleanup;
    }

    hyperion_client("webos", _address, _port, 150);
    signal(SIGINT, handle_signal);
    printf("Start connection loop\n");
    while (!app_quit)
    {
        if (hyperion_read() < 0)
        {
            fprintf(stderr, "Connection terminated.\n");
            app_quit = true;
        }
    }
    ret = 0;
cleanup:
    hyperion_destroy();
    backend.capture_terminate();
    backend.capture_cleanup();
    return ret;
}

static int image_data_cb(int width, int height, uint8_t *rgb_data)
{
    if (hyperion_set_image(rgb_data, width, height) != 0)
    {
        fprintf(stderr, "Write timeout\n");
        hyperion_destroy();
        app_quit = true;
    }
}