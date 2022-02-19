#pragma once

#include <pthread.h>
#include <glib.h>
#include "settings.h"
#include "unicapture.h"

#define SERVICE_NAME "org.webosbrew.piccap.service"

typedef struct {
    bool running;
    bool connected;
    bool power_paused;

    capture_backend_t ui_backend;
    capture_backend_t video_backend;

    unicapture_state_t unicapture;

    settings_t* settings;

    pthread_t connection_thread;
    bool connection_loop_running;

    GMainLoop* loop;
} service_t;

int service_init(service_t* service, settings_t* settings);
int service_register(service_t* service, GMainLoop* loop);

int service_start(service_t* service);
int service_stop(service_t* service);

int service_destroy(service_t* service);
