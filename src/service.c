#include "service.h"
#include "hyperion_client.h"
#include "log.h"
#include "pthread.h"
#include "unicapture.h"
#include <luna-service2/lunaservice.h>
#include <stdio.h>

void* connection_loop(void* data)
{
    service_t* service = (service_t*)data;
    DBG("Starting connection loop");
    while (service->connection_loop_running) {
        INFO("Connecting hyperion-client..");
        if ((hyperion_client("webos", service->settings->address, service->settings->port, service->settings->priority)) != 0) {
            ERR("Error! hyperion_client.");
        } else {
            INFO("hyperion-client connected!");
            service->connected = true;
            while (service->connection_loop_running) {
                INFO("reading...");
                if (hyperion_read() < 0) {
                    ERR("Error! Connection timeout.");
                    break;
                }
            }
            service->connected = false;
        }

        hyperion_destroy();

        if (service->connection_loop_running) {
            INFO("Connection destroyed, waiting...");
            sleep(1);
        }
    }

    INFO("Ending connection loop");
    DBG("Connection loop exiting");
    return 0;
}

void service_feed_frame(void* data, int width, int height, uint8_t rgb_data)
{
    service_t* service = (service_t*)data;
    int ret;
    if ((ret = hyperion_set_image(rgb_data, width, height)) != 0) {
        WARN("Frame sending failed: %d", ret);
    }
}

int service_init(service_t* service, settings_t* settings)
{
    unicapture_init(&service->unicapture);
    service->unicapture.callback = &service_feed_frame;
    service->unicapture.callback_data = (void*)service;

    service->settings = settings;

    char* ui_backends[] = { "libgm_backend.so", "libhalgal_backend.so", NULL };
    char* video_backends[] = { "libvtcapture_backend.so", "libdile_vt_backend.so", NULL };
    char backend_name[FILENAME_MAX] = { 0 };

    cap_backend_config_t config;
    config.resolution_width = settings->width;
    config.resolution_height = settings->height;
    config.fps = settings->fps;

    if (settings->ui_backend == NULL || strcmp(settings->ui_backend, "") == 0 || strcmp(settings->ui_backend, "auto") == 0) {
        INFO("Autodetecting UI backend...");
        if (unicapture_try_backends(&config, &service->ui_backend, ui_backends) == 0) {
            service->unicapture.ui_capture = &service->ui_backend;
        } else {
            service->unicapture.ui_capture = NULL;
        }
    } else {
        snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->ui_backend);
        if (unicapture_init_backend(&config, &service->ui_backend, backend_name) == 0) {
            service->unicapture.ui_capture = &service->ui_backend;
        }
    }

    if (settings->video_backend == NULL || strcmp(settings->video_backend, "") == 0 || strcmp(settings->video_backend, "auto") == 0) {
        INFO("Autodetecting video backend...");
        if (unicapture_try_backends(&config, &service->video_backend, video_backends) == 0) {
            service->unicapture.video_capture = &service->video_backend;
        } else {
            service->unicapture.video_capture = NULL;
        }
    } else {
        snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->video_backend);
        if (unicapture_init_backend(&config, &service->video_backend, backend_name) == 0) {
            service->unicapture.video_capture = &service->video_backend;
        }
    }

    return 0;
}

int service_destroy(service_t* service)
{
    service_stop(service);
    return 0;
}

int service_start(service_t* service)
{
    if (service->running) {
        return 1;
    }

    service->running = true;
    int res = unicapture_start(&service->unicapture);

    if (res != 0) {
        service->running = false;
        return res;
    }

    service->connection_loop_running = true;
    if (pthread_create(&service->connection_thread, NULL, connection_loop, service) != 0) {
        unicapture_stop(&service->unicapture);
        service->connection_loop_running = false;
        service->running = false;
        return -1;
    }
    return 0;
}

int service_stop(service_t* service)
{
    if (!service->running) {
        return 1;
    }

    service->connection_loop_running = false;
    unicapture_stop(&service->unicapture);
    pthread_join(service->connection_thread, NULL);
    service->running = false;

    return 0;
}

bool service_method_start(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    service_start(service);
    return false;
}

bool service_method_stop(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    service_stop(service);
    return false;
}

bool service_method_restart(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

bool service_method_is_root(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

bool service_method_is_running(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

bool service_method_get_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

bool service_method_set_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

bool service_method_reset_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    return false;
}

LSMethod methods[] = {
    { "start", service_method_start },
    { "stop", service_method_stop },
    { "isRoot", service_method_is_root },
    { "isRunning", service_method_is_running },
    { "getSettings", service_method_get_settings },
    { "setSettings", service_method_set_settings },
    { "resetSettings", service_method_reset_settings },
    { "restart", service_method_restart }
};

int service_register(service_t* service, GMainLoop* loop)
{
    LSHandle* handle = NULL;
    LSError lserror;

    LSErrorInit(&lserror);

    if (!LSRegister(SERVICE_NAME, &handle, &lserror)) {
        ERR("Unable to register on Luna bus: %s", lserror.message);
        LSErrorFree(&lserror);
        return -1;
    }

    LSRegisterCategory(handle, "/", methods, NULL, NULL, &lserror);
    LSCategorySetData(handle, "/", service, &lserror);

    LSGmainAttach(handle, loop, &lserror);

    LSErrorInit(&lserror);
    return 0;
}
