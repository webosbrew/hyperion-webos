#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include "common.h"
#include "hyperion_client.h"
#include <glib.h>
#include <glib-object.h>
#include <lunaservice.h>
#include <luna-service2/lunaservice.h>
#include <pbnjson.h>
#include <PmLogLib.h>

#define SERVICE_NAME "org.webosbrew.piccap.service"
#define BUF_SIZE 64


#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define DLSYM_ERROR_CHECK()                                         \
    if ((error = dlerror()) != NULL)  {                             \
        fprintf(stderr, "Error! dlsym failed, msg: %s\n", error);   \
        return -2;                                                  \
    }

pthread_t capture_thread;

// Main loop for aliving background service
GMainLoop *gmainLoop;
PmLogContext logcontext;

LSHandle  *sh = NULL;
LSMessage *message;
// Declare of each method
bool start(LSHandle *sh, LSMessage *message, void *data);
bool stop(LSHandle *sh, LSMessage *message, void *data);
bool status(LSHandle *sh, LSMessage *message, void *data);

LSMethod lunaMethods[] = {
    {"start", start},
    {"stop", stop},
    {"status", status},   // luna://org.webosbrew.piccap.service/XXXX
};


bool app_quit = false;
bool isrunning = false;

static const char *_backend = NULL;
static const char *_address = NULL;
static int _port = 19400;

static cap_backend_config_t config = {15, 0, 192, 108};
static cap_backend_funcs_t backend = {NULL};


static int image_data_cb(int width, int height, uint8_t *rgb_data);
int capture_main();
void *capture_loop(void *data);
int cleanup();

int luna_resp(LSHandle *sh, LSMessage *message, char *replyPayload, LSError *lserror);
//JSON helper functions
char* jval_to_string(jvalue_ref parsed, const char *item, const char *def);
bool jval_to_bool(jvalue_ref parsed, const char *item, bool def);
int jval_to_int(jvalue_ref parsed, const char *item, int def);


static int import_backend_library(const char *library_filename) {
    char *error;

    void *handle = dlopen(library_filename, RTLD_LAZY);
    if (handle == NULL) {
        PmLogError(logcontext, "FNCDLOPEN", 0, "Error! Failed to load backend library: %s, error: %s", library_filename, dlerror());
        return -1;
    }

    dlerror();    /* Clear any existing error */

    backend.capture_preinit = dlsym(handle, "capture_preinit"); DLSYM_ERROR_CHECK();
    backend.capture_init = dlsym(handle, "capture_init"); DLSYM_ERROR_CHECK();
    backend.capture_start = dlsym(handle, "capture_start"); DLSYM_ERROR_CHECK();
    backend.capture_terminate = dlsym(handle, "capture_terminate"); DLSYM_ERROR_CHECK();
    backend.capture_cleanup = dlsym(handle, "capture_cleanup"); DLSYM_ERROR_CHECK();

    return 0;
}

static int detect_backend() {
    /*
     * TODO
     * - Detect used rendering backend
     * - Pass appropriate library name to "import_backend_library"
     */

    if (_backend != NULL) {
        char library_name[256];
        snprintf(library_name, sizeof(library_name), "%s_backend.so", _backend);
        return import_backend_library(library_name);
    }

    return import_backend_library("libdile_vt_backend.so");
}

static void handle_signal(int signal)
{
    switch (signal)
    {
    case SIGINT:
        PmLogError(logcontext, "SIGINT", 0, "SIGINT called! Stopping capture if running..");
        app_quit = true;
        cleanup();
        break;
    default:
        break;
    }
}

int main(int argc, char *argv[])
{
    PmLogGetContext("hyperion-webos_service", &logcontext);
//    PmLogMsg(logcontext,Info, "MAINFNC", 0,  PMLOGKS("APP_STATUS","deleted"));
//    PmLogInfo(logcontext, "MAINFNC", 0,  "Teeeest!");
    LSError lserror;
    LSHandle  *handle = NULL;
    bool bRetVal = FALSE;
    signal(SIGINT, handle_signal);
    LSErrorInit(&lserror);

    // create a GMainLoop
    gmainLoop = g_main_loop_new(NULL, FALSE);

    bRetVal = LSRegister(SERVICE_NAME, &handle, &lserror);
    if (FALSE== bRetVal) {
        LSErrorFree( &lserror );
        return 0;
    }
    sh = LSMessageGetConnection(message);

    LSRegisterCategory(handle,"/",lunaMethods, NULL, NULL, &lserror);

    LSGmainAttach(handle, gmainLoop, &lserror);

    // run to check continuously for new events from each of the event sources
    g_main_loop_run(gmainLoop);
    // Decreases the reference count on a GMainLoop object by one
    g_main_loop_unref(gmainLoop);

    return 0;
}

int capture_main(){

    if ((detect_backend()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! detect_backend.");
        cleanup();
        return -1;
    }

    if ((backend.capture_preinit(&config, &image_data_cb)) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_preinit.");
        cleanup();
        return -1;
    }

    if ((backend.capture_init()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_init.");
        cleanup();
        return -1;
    }

    if ((backend.capture_start()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_start.");
        cleanup();
        return -1;
    }

    if ((hyperion_client("webos", _address, _port, 150)) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! hyperion_client.");
        cleanup();
        return -1;
    }
    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Capture main init completed.");

    app_quit = false;
    isrunning = true;
    if (pthread_create(&capture_thread, NULL, capture_loop, NULL) != 0) {
        return -1;
    }
    return 0;
}

void *capture_loop(void *data){
    PmLogInfo(logcontext, "FNCCPTLOOP", 0, "Starting connection loop");
    while (!app_quit)
    {
        if (hyperion_read() < 0)
        {
            PmLogError(logcontext, "FNCCPTLOOP", 0, "Error! Connection timeout.");
            isrunning = false;
            app_quit = true;
        }
    }
    cleanup();
    return 0;
}

int cleanup(){
    if (isrunning){
        app_quit=true;
        pthread_join(capture_thread, NULL);
        isrunning=false;
    }
    hyperion_destroy();
    if (backend.capture_terminate) {
        backend.capture_terminate();
    }
    if (backend.capture_cleanup) {
        backend.capture_cleanup();
    }
    return 0;
}

static int image_data_cb(int width, int height, uint8_t *rgb_data)
{
    if (hyperion_set_image(rgb_data, width, height) != 0)
    {
        PmLogError(logcontext, "FNCIMGDATA", 0, "Error! Write timeout.");
        hyperion_destroy();
        isrunning = false;
        app_quit = true;
    }
}


bool start(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCSTART", 0,  "Starting luna func start");
    PmLogInfo(logcontext, "FNCSTART", 0,  "Just testing");
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    PmLogInfo(logcontext, "FNCSTART", 0,  "Parsing values from msg input..");
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        backmsg = "Error while parsing input!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    if (isrunning){
        backmsg = "Capture already running!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    PmLogInfo(logcontext, "FNCSTART", 0,  "Getting values from msg input, or setting defaults..");
    _address = jval_to_string(parsed, "address", "");
    _port = jval_to_int(parsed, "port", _port);
    config.resolution_width = jval_to_int(parsed, "width", config.resolution_width);
    config.resolution_height = jval_to_int(parsed, "height", config.resolution_height);
    config.fps = jval_to_int(parsed, "fps", config.fps);
    _backend = jval_to_string(parsed, "backend", "");
    config.no_video = jval_to_bool(parsed, "novideo", false);
    config.no_gui = jval_to_bool(parsed, "nogui", false);

    if (_address == "" || _backend == "" || config.fps < 0 || config.fps > 60){
        PmLogError(logcontext, "FNCSTART", 0, "ERROR: Address and Backend are neccassary parameters and FPS should be between 0 (unlimited) and 60! | Address: %s | Backend: %s | FPS: %d", _address, _backend, config.fps);
        backmsg = "ERROR: Address and Backend are neccassary parameters and FPS should be between 0 (unlimited) and 60!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    if (config.fps == 0){
        config.framedelay_us = 0;
    }else{
        config.framedelay_us = 1000000 / config.fps;
    }

    PmLogInfo(logcontext, "FNCSTART", 0, "Using these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui);
    PmLogInfo(logcontext, "FNCSTART", 0,  "Calling capture start main..");

    if ((capture_main()) != 0){
        PmLogError(logcontext, "FNCSTART", 0,  "ERROR: Capture main init failed!");
        backmsg = "ERROR: Capture main init failed!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    //Response
    jreturnValue = jboolean_create(TRUE);
    backmsg = "Capture started successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("address"), jstring_create(_address));
    jobject_set(jobj, j_cstr_to_buffer("port"), jnumber_create_i32(_port));
    jobject_set(jobj, j_cstr_to_buffer("width"), jnumber_create_i32(config.resolution_width));
    jobject_set(jobj, j_cstr_to_buffer("height"), jnumber_create_i32(config.resolution_height));
    jobject_set(jobj, j_cstr_to_buffer("fps"), jnumber_create_i32(config.fps));
    jobject_set(jobj, j_cstr_to_buffer("backend"), jstring_create(_backend));
    jobject_set(jobj, j_cstr_to_buffer("novideo"), jboolean_create(config.no_video));
    jobject_set(jobj, j_cstr_to_buffer("nogui"), jboolean_create(config.no_gui));
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    return true;
}

bool stop(LSHandle *sh, LSMessage *message, void *data)
{
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        backmsg = "Error while parsing input!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    if (!isrunning){
        backmsg = "FAILED! Capture isn't running!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    cleanup();

    jreturnValue = jboolean_create(TRUE);
    backmsg = "Capture stopped successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    return true;
}

bool status(LSHandle *sh, LSMessage *message, void *data)
{
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        backmsg = "Error while parsing input!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("isrunning"), jboolean_create(isrunning));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    return true;
}

char* jval_to_string(jvalue_ref parsed, const char *item, const char *def)
{
	jvalue_ref jobj = NULL;
	raw_buffer jbuf;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_string(jobj)) {
        PmLogInfo(logcontext, "FNCJTOSTRN", 0,  "Didn't got a value for item: %s. Using default value: %s", item, def);
		return g_strdup(def);
    }

	jbuf = jstring_get(jobj);
	return g_strdup(jbuf.m_str);
}

bool jval_to_bool(jvalue_ref parsed, const char *item, bool def)
{
	jvalue_ref jobj;
	bool ret;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_boolean(jobj)) {
        PmLogInfo(logcontext, "FNCJTOBOOL", 0,  "Didn't got a value for item: %s. Using default value: %d", item, def);
		return def;
    }

	jboolean_get(jobj, &ret);
	return ret;
}

int jval_to_int(jvalue_ref parsed, const char *item, int def)
{
	jvalue_ref jobj = NULL;
	int ret = 0;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_number(jobj)) {
        PmLogInfo(logcontext, "FNCJTOINT", 0,  "Didn't got a value for item: %s. Using default value: %d", item, def);
		return def;
    }
	jnumber_get_i32(jobj, &ret);
	return ret;
}

int luna_resp(LSHandle *sh, LSMessage *message, char *replyPayload, LSError *lserror){
        PmLogInfo(logcontext, "FNCLRESP", 0,  "Responding with text: %s", replyPayload);
        jvalue_ref respobj = {0};
        jobject_set(respobj, j_cstr_to_buffer("returnValue"), jboolean_create(TRUE));
        jobject_set(respobj, j_cstr_to_buffer("backmsg"), jstring_create(replyPayload));
        LSMessageReply(sh, message, jvalue_tostring_simple(respobj), lserror);
}