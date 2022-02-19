#pragma once

#include <stdbool.h>
#include <pbnjson.h>

#define SETTINGS_PERSISTENCE_PATH "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/config.json"

// Settings stored in config.json file

typedef struct _settings_t {
    char* video_backend;
    char* ui_backend;

    char* address;
    int port;
    int priority;

    int fps;
    int width;
    int height;
    bool vsync;

    bool no_video;
    bool no_gui;

    bool autostart;
} settings_t;

void settings_init(settings_t*);

int settings_load_json(settings_t* settings, jvalue_ref source);
int settings_save_json(settings_t* settings, jvalue_ref target);

int settings_load_file(settings_t* settings, char* source);
int settings_save_file(settings_t* settings, char* target);
