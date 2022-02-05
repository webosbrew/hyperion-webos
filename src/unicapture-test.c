#include "log.h"
#include "unicapture.h"

int main(int argc, char** argv) {
    log_init();
    INFO("Hello world!");

    cap_backend_config_t config = {30, 0, 240, 135}; // 1920 / 5, 1080 / 5};
    capture_backend_t ui_capture;
    capture_backend_t video_capture;

    char* ui_backends[] = {"libgm_backend.so", NULL};
    char* video_backends[] = {"libdile_vt_backend.so", NULL};

    unicapture_state_t up;

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

    unicapture_run(&up);

    ui_capture.cleanup(ui_capture.state);
    video_capture.cleanup(video_capture.state);
    INFO("Cleaned up!");
}
