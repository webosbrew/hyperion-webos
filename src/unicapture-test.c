#include "log.h"
#include "unicapture.h"

int main(int argc, char** argv) {
    log_init();
    INFO("Hello world!");

    cap_backend_config_t config = {30, 0, 1920 / 5, 1080 / 5};
    capture_backend_t ui_capture;
    capture_backend_t video_capture;

    unicapture_init_backend(&config, &ui_capture, "libunigm_backend.so");
    unicapture_init_backend(&config, &video_capture, "libunidile_vt_backend.so");
    INFO("Initialized!");

    unicapture_run(&ui_capture, &video_capture);

    ui_capture.cleanup(ui_capture.state);
    video_capture.cleanup(video_capture.state);
    INFO("Cleaned up!");
}
