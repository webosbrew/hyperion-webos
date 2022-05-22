#include <unistd.h>
#include "log.h"
#include "unicapture.h"

int main()
{
    log_init();
    INFO("Hello world!");

    cap_backend_config_t config = { 0 };
    capture_backend_t ui_capture = { 0 };
    capture_backend_t video_capture = { 0 };

    config.fps = 30;
    config.framedelay_us = 0;
    config.resolution_width = 240; // 1920 / 5
    config.resolution_height = 135; // 1080 / 5

    char* ui_backends[] = { "libgm_backend.so", "libhalgal_backend.so", NULL };
    char* video_backends[] = { "libvtcapture_backend.so", "libdile_vt_backend.so", NULL };

    unicapture_state_t up = { 0 };
    unicapture_init(&up);

    if (unicapture_try_backends(&config, &ui_capture, ui_backends) == 0) {
        up.ui_capture = &ui_capture;
    } else {
        up.ui_capture = NULL;
    }

    if (unicapture_try_backends(&config, &video_capture, video_backends) == 0) {
        up.video_capture = &video_capture;
    } else {
        up.video_capture = NULL;
    }

    INFO("Initialized!");

    while (true) {
        INFO("starting up...");
        INFO("start result: %d", unicapture_start(&up));
        sleep(60);

        INFO("stopping...");
        INFO("stop result: %d", unicapture_stop(&up));
        sleep(3);
    }

    ui_capture.cleanup(ui_capture.state);
    video_capture.cleanup(video_capture.state);
    INFO("Cleaned up!");
}
