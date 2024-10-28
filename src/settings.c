#include "settings.h"
#include <stdio.h>

void settings_init(settings_t* settings)
{
    settings->ui_backend = strdup("auto");
    settings->video_backend = strdup("auto");

    settings->address = strdup("");
    settings->port = 19400;
    settings->priority = 150;
    settings->unix_socket = false;

    settings->fps = 30;
    settings->width = 320;
    settings->height = 180;
    settings->quirks = 0;

    settings->send_nv12 = false;
    settings->no_video = false;
    settings->no_gui = false;
    settings->autostart = false;
    settings->vsync = true;

    settings->no_hdr = false;
    settings->no_powerstate = false;

    settings->dump_frames = false;
}

int settings_load_json(settings_t* settings, jvalue_ref source)
{
    jvalue_ref value;

    if ((value = jobject_get(source, j_cstr_to_buffer("backend"))) && jis_string(value)) {
        free(settings->video_backend);
        raw_buffer str = jstring_get(value);
        settings->video_backend = strdup(str.m_str);
        jstring_free_buffer(str);
    }

    if ((value = jobject_get(source, j_cstr_to_buffer("uibackend"))) && jis_string(value)) {
        free(settings->ui_backend);
        raw_buffer str = jstring_get(value);
        settings->ui_backend = strdup(str.m_str);
        jstring_free_buffer(str);
    }

    if ((value = jobject_get(source, j_cstr_to_buffer("address"))) && jis_string(value)) {
        free(settings->address);
        raw_buffer str = jstring_get(value);
        settings->address = strdup(str.m_str);
        jstring_free_buffer(str);
    }
    if ((value = jobject_get(source, j_cstr_to_buffer("port"))) && jis_number(value))
        jnumber_get_i32(value, &settings->port);
    if ((value = jobject_get(source, j_cstr_to_buffer("priority"))) && jis_number(value))
        jnumber_get_i32(value, &settings->priority);
    if ((value = jobject_get(source, j_cstr_to_buffer("unix-socket"))) && jis_boolean(value))
        jboolean_get(value, &settings->unix_socket);

    if ((value = jobject_get(source, j_cstr_to_buffer("fps"))) && jis_number(value))
        jnumber_get_i32(value, &settings->fps);
    if ((value = jobject_get(source, j_cstr_to_buffer("width"))) && jis_number(value))
        jnumber_get_i32(value, &settings->width);
    if ((value = jobject_get(source, j_cstr_to_buffer("height"))) && jis_number(value))
        jnumber_get_i32(value, &settings->height);
    if ((value = jobject_get(source, j_cstr_to_buffer("nv12"))) && jis_boolean(value))
        jboolean_get(value, &settings->send_nv12);
    if ((value = jobject_get(source, j_cstr_to_buffer("quirks"))) && jis_number(value))
        jnumber_get_i32(value, &settings->quirks);

    if ((value = jobject_get(source, j_cstr_to_buffer("vsync"))) && jis_boolean(value))
        jboolean_get(value, &settings->vsync);
    if ((value = jobject_get(source, j_cstr_to_buffer("novideo"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_video);
    if ((value = jobject_get(source, j_cstr_to_buffer("nogui"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_gui);
    if ((value = jobject_get(source, j_cstr_to_buffer("autostart"))) && jis_boolean(value))
        jboolean_get(value, &settings->autostart);

    if ((value = jobject_get(source, j_cstr_to_buffer("nohdr"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_hdr);
    if ((value = jobject_get(source, j_cstr_to_buffer("nopowerstate"))) && jis_boolean(value))
        jboolean_get(value, &settings->no_powerstate);

    return 0;
}

int settings_save_json(settings_t* settings, jvalue_ref target)
{
    jobject_set(target, j_cstr_to_buffer("backend"), jstring_create(settings->video_backend));
    jobject_set(target, j_cstr_to_buffer("uibackend"), jstring_create(settings->ui_backend));

    jobject_set(target, j_cstr_to_buffer("address"), jstring_create(settings->address));
    jobject_set(target, j_cstr_to_buffer("port"), jnumber_create_i32(settings->port));
    jobject_set(target, j_cstr_to_buffer("priority"), jnumber_create_i32(settings->priority));
    jobject_set(target, j_cstr_to_buffer("unix-socket"), jboolean_create(settings->unix_socket));

    jobject_set(target, j_cstr_to_buffer("fps"), jnumber_create_i32(settings->fps));
    jobject_set(target, j_cstr_to_buffer("width"), jnumber_create_i32(settings->width));
    jobject_set(target, j_cstr_to_buffer("height"), jnumber_create_i32(settings->height));
    jobject_set(target, j_cstr_to_buffer("nv12"), jboolean_create(settings->send_nv12));
    jobject_set(target, j_cstr_to_buffer("quirks"), jnumber_create_i32(settings->quirks));

    jobject_set(target, j_cstr_to_buffer("vsync"), jboolean_create(settings->vsync));
    jobject_set(target, j_cstr_to_buffer("novideo"), jboolean_create(settings->no_video));
    jobject_set(target, j_cstr_to_buffer("nogui"), jboolean_create(settings->no_gui));
    jobject_set(target, j_cstr_to_buffer("autostart"), jboolean_create(settings->autostart));

    jobject_set(target, j_cstr_to_buffer("nohdr"), jboolean_create(settings->no_hdr));
    jobject_set(target, j_cstr_to_buffer("nopowerstate"), jboolean_create(settings->no_powerstate));

    return 0;
}

int settings_load_file(settings_t* settings, char* source)
{
    int ret = 0;

    JSchemaInfo schema;
    jvalue_ref parsed;

    FILE* f = fopen(source, "rb");
    // File read failed
    if (f == NULL) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json = calloc(fsize + 1, 1);
    fread(json, fsize, 1, f);
    fclose(f);

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(json), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        free(json);
        j_release(&parsed);
        return -2;
    }

    ret = settings_load_json(settings, parsed);

    free(json);
    j_release(&parsed);
    return ret;
}

int settings_save_file(settings_t* settings, char* target)
{
    jvalue_ref jobj = jobject_create();
    int ret = settings_save_json(settings, jobj);

    if (ret != 0) {
        j_release(&jobj);
        return -2;
    }

    FILE* fd = fopen(target, "wb");
    if (fd == NULL) {
        j_release(&jobj);
        return -1;
    }

    fputs(jvalue_tostring_simple(jobj), fd);
    fclose(fd);

    j_release(&jobj);
    return 0;
}
