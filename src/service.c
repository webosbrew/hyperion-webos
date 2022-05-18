#include "service.h"
#include "hyperion_client.h"
#include "log.h"
#include "pthread.h"
#include "settings.h"
#include "unicapture.h"
#include <errno.h>
#include <luna-service2/lunaservice.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

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

int service_feed_frame(void* data __attribute__((unused)), int width, int height, uint8_t* rgb_data)
{
    int ret;
    if ((ret = hyperion_set_image(rgb_data, width, height)) != 0) {
        WARN("Frame sending failed: %d", ret);
    }

    return 0;
}

int service_init(service_t* service, settings_t* settings)
{
    cap_backend_config_t config;
    config.resolution_width = settings->width;
    config.resolution_height = settings->height;
    config.fps = settings->fps;

    service->settings = settings;

    unicapture_init(&service->unicapture);
    service->unicapture.vsync = settings->vsync;
    service->unicapture.fps = settings->fps;
    service->unicapture.callback = &service_feed_frame;
    service->unicapture.callback_data = (void*)service;
    service->unicapture.dump_frames = settings->dump_frames;

    char* ui_backends[] = { "libgm_backend.so", "libhalgal_backend.so", NULL };
    char* video_backends[] = { "libvtcapture_backend.so", "libdile_vt_backend.so", NULL };
    char backend_name[FILENAME_MAX] = { 0 };

    service->unicapture.ui_capture = NULL;

    if (settings->no_gui) {
        INFO("UI capture disabled");
    } else {
        if (settings->ui_backend == NULL || strcmp(settings->ui_backend, "") == 0 || strcmp(settings->ui_backend, "auto") == 0) {
            INFO("Autodetecting UI backend...");
            if (unicapture_try_backends(&config, &service->ui_backend, ui_backends) == 0) {
                service->unicapture.ui_capture = &service->ui_backend;
            }
        } else {
            snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->ui_backend);
            if (unicapture_init_backend(&config, &service->ui_backend, backend_name) == 0) {
                service->unicapture.ui_capture = &service->ui_backend;
            }
        }
    }

    service->unicapture.video_capture = NULL;

    if (settings->no_video) {
        INFO("Video capture disabled");
    } else {
        if (settings->video_backend == NULL || strcmp(settings->video_backend, "") == 0 || strcmp(settings->video_backend, "auto") == 0) {
            INFO("Autodetecting video backend...");
            if (unicapture_try_backends(&config, &service->video_backend, video_backends) == 0) {
                service->unicapture.video_capture = &service->video_backend;
            }
        } else {
            snprintf(backend_name, sizeof(backend_name), "%s_backend.so", settings->video_backend);
            if (unicapture_init_backend(&config, &service->video_backend, backend_name) == 0) {
                service->unicapture.video_capture = &service->video_backend;
            }
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
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(service_start(service) == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    return false;
}

bool service_method_stop(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(service_stop(service) == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    return false;
}

bool service_method_status(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    jvalue_ref jobj = jobject_create();
    LSError lserror;
    LSErrorInit(&lserror);

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    jobject_set(jobj, j_cstr_to_buffer("elevated"), jboolean_create(getuid() == 0));
    jobject_set(jobj, j_cstr_to_buffer("isRunning"), jboolean_create(service->running));
    jobject_set(jobj, j_cstr_to_buffer("connected"), jboolean_create(service->connected));
    jobject_set(jobj, j_cstr_to_buffer("videoBackend"), service->video_backend.name ? jstring_create(service->video_backend.name) : jnull());
    jobject_set(jobj, j_cstr_to_buffer("videoRunning"), jboolean_create(service->unicapture.video_capture_running));
    jobject_set(jobj, j_cstr_to_buffer("uiBackend"), service->ui_backend.name ? jstring_create(service->ui_backend.name) : jnull());
    jobject_set(jobj, j_cstr_to_buffer("uiRunning"), jboolean_create(service->unicapture.ui_capture_running));
    jobject_set(jobj, j_cstr_to_buffer("framerate"), jnumber_create_f64(service->unicapture.metrics.framerate));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);

    return true;
}

bool service_method_get_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    jvalue_ref jobj = jobject_create();

    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    settings_save_json(service->settings, jobj);

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);

    return true;
}

bool service_method_set_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* service = (service_t*)data;
    LSError lserror;
    LSErrorInit(&lserror);

    JSchemaInfo schema;
    jvalue_ref parsed;

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    if (jis_null(parsed)) {
        j_release(&parsed);
        return false;
    }

    int res = settings_load_json(service->settings, parsed);
    if (res == 0) {
        if (settings_save_file(service->settings, SETTINGS_PERSISTENCE_PATH) != 0) {
            WARN("Settings save failed");
        }

        const char* startup_directory = "/var/lib/webosbrew/init.d";
        const char* startup_symlink = "/var/lib/webosbrew/init.d/piccapautostart";
        const char* startup_script = "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/piccapautostart";

        if (unlink(startup_symlink) != 0 && errno != ENOENT) {
            WARN("Startup symlink removal failed: %s", strerror(errno));
        }

        if (service->settings->autostart) {
            mkdir(startup_directory, 0755);
            if (symlink(startup_script, startup_symlink) != 0) {
                WARN("Startup symlink creation failed: %s", strerror(errno));
            }
        }
    }

    jvalue_ref jobj = jobject_create();
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jboolean_create(res == 0));

    LSMessageReply(sh, msg, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    j_release(&jobj);

    return true;
}

LSMethod methods[] = {
    { "start", service_method_start, LUNA_METHOD_FLAGS_NONE },
    { "stop", service_method_stop, LUNA_METHOD_FLAGS_NONE },
    { "status", service_method_status, LUNA_METHOD_FLAGS_NONE },
    { "getSettings", service_method_get_settings, LUNA_METHOD_FLAGS_NONE },
    { "setSettings", service_method_set_settings, LUNA_METHOD_FLAGS_NONE }
};

static bool power_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    JSchemaInfo schema;
    jvalue_ref parsed;
    service_t* service = (service_t*)data;

    INFO("Power status callback message: %s", LSMessageGetPayload(msg));

    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schema);

    // Parsing failed
    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }

    jvalue_ref state_ref = jobject_get(parsed, j_cstr_to_buffer("state"));
    raw_buffer state_buf = jstring_get(state_ref);
    const char* state_str = state_buf.m_str;
    bool target_state = strcmp(state_str, "Active") == 0 && !jobject_containskey(parsed, j_cstr_to_buffer("processing"));

    if (!service->running && target_state && service->power_paused) {
        INFO("Resuming after power pause...");
        service->power_paused = false;
        service_start(service);
    }

    if (service->running && !target_state && !service->power_paused) {
        INFO("Pausing due to power event...");
        service->power_paused = true;
        service_stop(service);
    }

    jstring_free_buffer(state_buf);
    j_release(&parsed);

    return true;
}

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

    if (!LSCall(handle, "luna://com.webos.service.tvpower/power/getPowerState", "{\"subscribe\":true}", power_callback, (void*)service, NULL, &lserror)) {
        WARN("Power state monitoring call failed: %s", lserror.message);
    }

    LSErrorFree(&lserror);
    return 0;
}
