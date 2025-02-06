#include "unicapture.h"
#include "converter.h"
#include "log.h"
#include "utils.h"
#include <dlfcn.h>
#include <libyuv.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DLSYM_ERROR_CHECK()                         \
    if ((error = dlerror()) != NULL) {              \
        ERR("Error! dlsym failed, msg: %s", error); \
        return -2;                                  \
    }

int unicapture_init_backend(cap_backend_config_t* config, capture_backend_t* backend, const char* name)
{
    char* error;
    void* handle = dlopen(name, RTLD_LAZY);

    DBG("%s: loading...", name);

    if (handle == NULL) {
        WARN("Unable to load %s: %s", name, dlerror());
        return -1;
    }

    dlerror();

    *(void**)(&backend->init) = dlsym(handle, "capture_init");
    DLSYM_ERROR_CHECK();
    *(void**)(&backend->cleanup) = dlsym(handle, "capture_cleanup");
    DLSYM_ERROR_CHECK();

    *(void**)(&backend->start) = dlsym(handle, "capture_start");
    DLSYM_ERROR_CHECK();
    *(void**)(&backend->terminate) = dlsym(handle, "capture_terminate");
    DLSYM_ERROR_CHECK();

    *(void**)(&backend->acquire_frame) = dlsym(handle, "capture_acquire_frame");
    DLSYM_ERROR_CHECK();
    *(void**)(&backend->release_frame) = dlsym(handle, "capture_release_frame");
    DLSYM_ERROR_CHECK();

    *(void**)(&backend->wait) = dlsym(handle, "capture_wait");
    DLSYM_ERROR_CHECK();

    DBG("%s: loaded, initializing...", name);

    int ret = backend->init(config, &backend->state);

    if (ret == 0) {
        backend->name = strdup(name);
        DBG("%s: success", name);
    } else {
        ERR("%s: init failure, code: %d", name, ret);
    }

    return ret;
}

int unicapture_try_backends(cap_backend_config_t* config, capture_backend_t* backend, const char* const* candidates)
{
    int ret = 0;
    for (int i = 0; candidates[i] != NULL; i++) {
        ret = unicapture_init_backend(config, backend, candidates[i]);
        if (ret == 0) {
            DBG("try_backends: %s succeeded", candidates[i]);
            return 0;
        } else {
            WARN("try_backends: backend: %s failed with code: %d", candidates[i], ret);
        }
    }

    ERR("Try backends failed!");
    return -1;
}

void* unicapture_vsync_handler(void* data)
{
    unicapture_state_t* this = (unicapture_state_t*)data;

    INFO("vsync thread starting...");

    while (this->vsync_thread_running) {
        if (this->vsync && this->video_capture_running && this->video_capture->wait) {
            if ((this->video_capture->wait(this->video_capture->state)) == -99) { // stop video capture (will be started again in unicapture_run())
                INFO("Stopping video capture.");
                this->video_capture->terminate(this->video_capture->state);
                this->video_capture_running = false;
            }
        } else {
            usleep(1000000 / (this->fps == 0 ? 30 : this->fps));
        }
        pthread_mutex_lock(&this->vsync_lock);
        pthread_cond_signal(&this->vsync_cond);
        pthread_mutex_unlock(&this->vsync_lock);
    }

    INFO("vsync thread finished");

    return NULL;
}

void* unicapture_run(void* data)
{
    unicapture_state_t* this = (unicapture_state_t*)data;
    capture_backend_t* ui_capture = this->ui_capture;
    capture_backend_t* video_capture = this->video_capture;

    uint64_t framecounter = 0;
    uint64_t framecounter_start = getticks_us();

    uint64_t last_video_start = 0;
    uint64_t last_ui_start = 0;

    converter_t ui_converter;
    converter_t video_converter;
    converter_t final_converter;

    converter_init(&ui_converter);
    converter_init(&video_converter);
    converter_init(&final_converter);

    pthread_mutex_init(&this->vsync_lock, NULL);
    pthread_cond_init(&this->vsync_cond, NULL);

    frame_info_t blended_frame = { PIXFMT_INVALID };
    frame_info_t final_frame = { PIXFMT_INVALID };

    this->vsync_thread_running = true;
    pthread_create(&this->vsync_thread, NULL, unicapture_vsync_handler, this);

    while (this->running) {
        uint64_t now = getticks_us();

        if ((now - last_ui_start) > 1000000 && ui_capture != NULL && !this->ui_capture_running) {
            last_ui_start = now;
            DBG("Attempting UI capture init...");
            if (ui_capture->start(ui_capture->state) == 0) {
                INFO("UI capture started");
                this->ui_capture_running = true;
            }
        }

        if ((now - last_video_start) > 1000000 && video_capture != NULL && !this->video_capture_running) {
            last_video_start = now;
            DBG("Attempting video capture init...");
            if (video_capture->start(video_capture->state) == 0) {
                INFO("Video capture started");
                this->video_capture_running = true;
            }
        }

        int ret = 0;
        uint64_t frame_start = getticks_us();

        pthread_mutex_lock(&this->vsync_lock);
        pthread_cond_wait(&this->vsync_cond, &this->vsync_lock);
        pthread_mutex_unlock(&this->vsync_lock);
        uint64_t frame_wait = getticks_us();

        frame_info_t ui_frame = { PIXFMT_INVALID };
        frame_info_t ui_frame_converted = { PIXFMT_INVALID };
        frame_info_t video_frame = { PIXFMT_INVALID };
        frame_info_t video_frame_converted = { PIXFMT_INVALID };

        // Capture frames
        if (this->ui_capture_running) {
            if ((ret = ui_capture->acquire_frame(ui_capture->state, &ui_frame)) != 0) {
                ui_frame.pixel_format = PIXFMT_INVALID;
            }
        }

        if (this->video_capture_running) {
            if ((ret = video_capture->acquire_frame(video_capture->state, &video_frame)) != 0) {
                DBG("video_capture acquire_frame failed: %d", ret);
                video_frame.pixel_format = PIXFMT_INVALID;
            }
        }

        uint64_t frame_acquired = getticks_us();
        pixel_format_t target_format = this->target_format;

        // Convert frame to suitable video formats
        if (ui_frame.pixel_format != PIXFMT_INVALID) {
            converter_run(&ui_converter, &ui_frame, &ui_frame_converted,
                (target_format == PIXFMT_RGB || target_format == PIXFMT_ARGB) ? PIXFMT_ARGB : PIXFMT_YUV420_SEMI_PLANAR);
        }

        if (video_frame.pixel_format != PIXFMT_INVALID) {
            converter_run(&video_converter, &video_frame, &video_frame_converted,
                (target_format == PIXFMT_RGB || target_format == PIXFMT_ARGB) ? PIXFMT_ARGB : PIXFMT_YUV420_SEMI_PLANAR);
        }

        uint64_t frame_converted = getticks_us();

        bool got_frame = true;

        // Blend frames and prepare for sending
        if (video_frame_converted.pixel_format != PIXFMT_INVALID && ui_frame_converted.pixel_format != PIXFMT_INVALID) {
            const int width = video_frame_converted.width;
            const int height = video_frame_converted.height;

            if (target_format == PIXFMT_RGB || target_format == PIXFMT_ARGB) {
                blended_frame.planes[0].buffer = realloc(blended_frame.planes[0].buffer, width * height * 4);
                blended_frame.planes[0].stride = width * 4;
                blended_frame.pixel_format = PIXFMT_ARGB;
                blended_frame.width = width;
                blended_frame.height = height;

                ARGBBlend(ui_frame_converted.planes[0].buffer,
                    ui_frame_converted.planes[0].stride,
                    video_frame_converted.planes[0].buffer,
                    video_frame_converted.planes[0].stride,
                    blended_frame.planes[0].buffer,
                    4 * width,
                    width,
                    height);
            }

            if (target_format == PIXFMT_YUV420_SEMI_PLANAR) {
                blended_frame.planes[0].buffer = realloc(blended_frame.planes[0].buffer, width * height);
                blended_frame.planes[0].stride = width;
                blended_frame.planes[1].buffer = realloc(blended_frame.planes[1].buffer, width * height / 2);
                blended_frame.planes[1].stride = width;
                blended_frame.height = height;
                blended_frame.width = width;
                blended_frame.pixel_format = PIXFMT_YUV420_SEMI_PLANAR;

                for (int i = 0; i < width * height; i++) {
                    blended_frame.planes[0].buffer[i] = blend(ui_frame_converted.planes[0].buffer[i],
                        video_frame_converted.planes[0].buffer[i], ui_frame.planes[0].buffer[i * 4 + 3]);
                }

                int alpha_idx = 0;
                for (int i = 0; i < height / 2; i++) {
                    for (int j = 0; j < width; j += 2) {
                        int idx = i * width + j;
                        blended_frame.planes[1].buffer[idx] = blend(ui_frame_converted.planes[1].buffer[idx],
                            video_frame_converted.planes[1].buffer[idx], ui_frame.planes[0].buffer[alpha_idx + 3]);
                        blended_frame.planes[1].buffer[idx + 1] = blend(ui_frame_converted.planes[1].buffer[idx + 1],
                            video_frame_converted.planes[1].buffer[idx + 1], ui_frame.planes[0].buffer[alpha_idx + 3]);
                        alpha_idx += 8;
                    }
                    alpha_idx += ui_frame.planes[0].stride;
                }
            }
            converter_run(&final_converter, &blended_frame, &final_frame, target_format);
        } else if (ui_frame_converted.pixel_format != PIXFMT_INVALID) {
            converter_run(&final_converter, &ui_frame_converted, &final_frame, target_format);
        } else if (video_frame_converted.pixel_format != PIXFMT_INVALID) {
            converter_run(&final_converter, &video_frame_converted, &final_frame, target_format);
        } else {
            got_frame = false;
            WARN("No valid frame to send...");
        }

        uint64_t frame_processed = getticks_us();

        if (this->dump_frames && got_frame && framecounter % 30 == 0) {
            char filename[256];
            snprintf(filename, sizeof(filename), "/tmp/hyperion-webos-dump.%03d.data", (int)(framecounter / 30) % 10);
            FILE* fd = fopen(filename, "wb");
            if (target_format == PIXFMT_RGB)
                fwrite(final_frame.planes[0].buffer, 3 * final_frame.width * final_frame.height, 1, fd);
            if (target_format == PIXFMT_ARGB)
                fwrite(final_frame.planes[0].buffer, 4 * final_frame.width * final_frame.height, 1, fd);
            if (target_format == PIXFMT_YUV420_SEMI_PLANAR) {
                fwrite(final_frame.planes[0].buffer, final_frame.width * final_frame.height, 1, fd);
                fwrite(final_frame.planes[1].buffer, final_frame.width * final_frame.height / 2, 1, fd);
            }
            fclose(fd);
            INFO("Buffer dumped to: %s", filename);
        }

        if (this->callback != NULL && (target_format == PIXFMT_RGB || target_format == PIXFMT_ARGB)) {
            this->callback(this->callback_data, final_frame.width, final_frame.height, final_frame.planes[0].buffer);
        }

        if (this->callback_nv12 != NULL && target_format == PIXFMT_YUV420_SEMI_PLANAR) {
            this->callback_nv12(this->callback_data, final_frame.width, final_frame.height, final_frame.planes[0].buffer,
                final_frame.planes[1].buffer, final_frame.width, final_frame.width);
        }

        uint64_t frame_sent = getticks_us();

        if (ui_frame.pixel_format != PIXFMT_INVALID) {
            ui_capture->release_frame(ui_capture->state, &ui_frame);
        }

        if (video_frame.pixel_format != PIXFMT_INVALID) {
            video_capture->release_frame(video_capture->state, &video_frame);
        }

        uint64_t frame_released = getticks_us();

        if (got_frame) {
            framecounter += 1;

            if (framecounter % 60 == 0) {
                double fps = (60 * 1000000.0) / (getticks_us() - framecounter_start);
                this->metrics.framerate = fps;
                DBG("Framerate: %.6f FPS; timings - wait: %lldus, acquire: %lldus, convert: %lldus, process; %lldus, send: %lldus, release: %lldus",
                    fps, frame_wait - frame_start, frame_acquired - frame_wait, frame_converted - frame_acquired, frame_processed - frame_converted, frame_sent - frame_processed, frame_released - frame_sent);

                DBG("        UI: pixfmt: %d; %dx%d", ui_frame.pixel_format, ui_frame.width, ui_frame.height);
                DBG("     VIDEO: pixfmt: %d; %dx%d", video_frame.pixel_format, video_frame.width, video_frame.height);
                DBG("CONV    UI: pixfmt: %d; %dx%d", ui_frame_converted.pixel_format, ui_frame_converted.width, ui_frame_converted.height);
                DBG("CONV VIDEO: pixfmt: %d; %dx%d", video_frame_converted.pixel_format, video_frame_converted.width, video_frame_converted.height);

                framecounter_start = getticks_us();
            }
        }
    }

    INFO("Shutting down...");

    if (this->vsync_thread_running) {
        DBG("Waiting for vsync thread to finish...");
        this->vsync_thread_running = false;
        pthread_join(this->vsync_thread, NULL);
    }

    if (this->ui_capture_running) {
        DBG("Terminating UI capture...");
        ui_capture->terminate(ui_capture->state);
        this->ui_capture_running = false;
    }

    if (this->video_capture_running) {
        DBG("Terminating Video capture...");
        video_capture->terminate(video_capture->state);
        this->video_capture_running = false;
    }

    for (int i = 0; i < MAX_PLANES; i++) {
        if (blended_frame.planes[i].buffer != NULL) {
            free(blended_frame.planes[i].buffer);
            blended_frame.planes[i].buffer = NULL;
        }
    }

    converter_release(&ui_converter);
    converter_release(&video_converter);
    converter_release(&final_converter);
    DBG("Done!");

    return NULL;
}

void unicapture_init(unicapture_state_t* this)
{
    memset(this, 0, sizeof(unicapture_state_t));
    this->vsync = true;
}

int unicapture_start(unicapture_state_t* this)
{
    if (this->running) {
        return 1;
    }

    this->running = true;
    pthread_create(&this->main_thread, NULL, unicapture_run, this);

    return 0;
}

int unicapture_stop(unicapture_state_t* this)
{
    if (!this->running) {
        return 1;
    }

    this->running = false;
    pthread_join(this->main_thread, NULL);

    return 0;
}
