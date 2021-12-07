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


PmLogContext logcontext;


#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define DLSYM_ERROR_CHECK()                                         \
    if ((error = dlerror()) != NULL)  {                             \
        fprintf(stderr, "Error! dlsym failed, msg: %s\n", error);   \
        return -2;                                                  \
    }

static struct option long_options[] = {
    {"width", required_argument, 0, 'x'},
    {"height", required_argument, 0, 'y'},
    {"address", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'p'},
    {"fps", required_argument, 0, 'f'},
    {"no-video", no_argument, 0, 'V'},
    {"no-gui", no_argument, 0, 'G'},
    {"backend", required_argument, 0, 'b'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0},
};

// Main loop for aliving background service
GMainLoop *gmainLoop;

LSHandle  *sh = NULL;
LSMessage *message;
// Declare of each method
bool start(LSHandle *sh, LSMessage *message, void *data);
bool stop(LSHandle *sh, LSMessage *message, void *data);
bool status(LSHandle *sh, LSMessage *message, void *data);

LSMethod sampleMethods[] = {
    {"start", start},
    {"stop", stop},
    {"status", status},   // luna://org.webosbrew.piccap.service/test
};


bool app_quit = false;

static const char *_backend = NULL;
static const char *_address = NULL;
static int _port = 19400;

static cap_backend_config_t config = {15, 0, 192, 108};
static cap_backend_funcs_t backend = {NULL};

static int image_data_cb(int width, int height, uint8_t *rgb_data);
int capture_main(int argc, char *argv[]);

//JSON helper functions
char* jval_to_string(jvalue_ref parsed, const char *item, const char *def);
bool jval_to_bool(jvalue_ref parsed, const char *item, bool def);
int jval_to_int(jvalue_ref parsed, const char *item, int def);


static int import_backend_library(const char *library_filename) {
    char *error;

    void *handle = dlopen(library_filename, RTLD_LAZY);
    if (handle == NULL) {
        fprintf(stderr, "Error! Failed to load backend library: %s, error: %s\n", library_filename, dlerror());
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
        app_quit = true;
        hyperion_destroy();
        break;
    default:
        break;
    }
}

static void print_usage()
{
    printf("Usage: hyperion-webos -a ADDRESS [OPTION]...\n");
    printf("\n");
    printf("Grab screen content continously and send to Hyperion via flatbuffers server.\n");
    printf("\n");
    printf("  -x, --width=WIDTH     Width of video frame (default 192)\n");
    printf("  -y, --height=HEIGHT   Height of video frame (default 108)\n");
    printf("  -a, --address=ADDR    IP address of Hyperion server\n");
    printf("  -p, --port=PORT       Port of Hyperion flatbuffers server (default 19400)\n");
    printf("  -f, --fps=FPS         Framerate for sending video frames (default 15)\n");
    printf("  -b, --backend=BE      Use specific backend (default auto)\n");
    printf("  -V, --no-video        Video will not be captured\n");
    printf("  -G, --no-gui          GUI/UI will not be captured\n");
}

static int parse_options(int argc, char *argv[])
{
    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "x:y:a:p:f:b:h", long_options, &longindex)) != -1)
    {
        switch (opt)
        {
        case 'x':
            config.resolution_width = atoi(optarg);
            break;
        case 'y':
            config.resolution_height = atoi(optarg);
            break;
        case 'a':
            _address = strdup(optarg);
            break;
        case 'p':
            _port = atol(optarg);
            break;
        case 'f':
            config.fps = atoi(optarg);
            break;
        case 'V':
            config.no_video = 1;
            break;
        case 'G':
            config.no_gui = 1;
            break;
        case 'b':
            _backend = strdup(optarg);
            break;
        case 'h':
        default:
            print_usage();
            return 1;
        }
    }
    if (!_address)
    {
        fprintf(stderr, "Error! Address not specified.\n");
        print_usage();
        return 1;
    }
    if (config.fps < 0 || config.fps > 60)
    {
        fprintf(stderr, "Error! FPS should between 0 (unlimited) and 60.\n");
        print_usage();
        return 1;
    }
    if (config.fps == 0)
        config.framedelay_us = 0;
    else
        config.framedelay_us = 1000000 / config.fps;
    return 0;
}

int main(int argc, char *argv[])
{
    PmLogGetContext("hyperion-webos_service", &logcontext);
//    PmLogMsg(logcontext,Info, "MAINFNC", 0,  PMLOGKS("APP_STATUS","deleted"));
//    PmLogInfo(logcontext, "MAINFNC", 0,  "Teeeest!");
    LSError lserror;
    LSHandle  *handle = NULL;
    bool bRetVal = FALSE;

    LSErrorInit(&lserror);

    // create a GMainLoop
    gmainLoop = g_main_loop_new(NULL, FALSE);

    bRetVal = LSRegister(SERVICE_NAME, &handle, &lserror);
    if (FALSE== bRetVal) {
        LSErrorFree( &lserror );
        return 0;
    }
    sh = LSMessageGetConnection(message);

    LSRegisterCategory(handle,"/",sampleMethods, NULL, NULL, &lserror);

    LSGmainAttach(handle, gmainLoop, &lserror);

    // run to check continuously for new events from each of the event sources
    g_main_loop_run(gmainLoop);
    // Decreases the reference count on a GMainLoop object by one
    g_main_loop_unref(gmainLoop);

    return 0;
}

int capture_main(int argc, char *argv[]){
    int ret;
    if ((ret = parse_options(argc, argv)) != 0)
    {
        return ret;
    }

    if (getenv("XDG_RUNTIME_DIR") == NULL)
    {
        setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
    }

    if ((ret = detect_backend()) != 0)
    {
        fprintf(stderr, "Error! detect_backend.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_preinit(&config, &image_data_cb)) != 0)
    {
        fprintf(stderr, "Error! capture_preinit.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_init()) != 0)
    {
        fprintf(stderr, "Error! capture_initialize.\n");
        goto cleanup;
    }

    if ((ret = backend.capture_start()) != 0)
    {
        fprintf(stderr, "Error! capture_start.\n");
        goto cleanup;
    }

    if ((ret = hyperion_client("webos", _address, _port, 150)) != 0)
    {
        fprintf(stderr, "Error! hyperion_client.\n");
        goto cleanup;
    }
    signal(SIGINT, handle_signal);
    printf("Start connection loop\n");
    while (!app_quit)
    {
        if (hyperion_read() < 0)
        {
            fprintf(stderr, "Connection terminated.\n");
            app_quit = true;
        }
    }
    ret = 0;
cleanup:
    hyperion_destroy();
    if (backend.capture_terminate) {
        backend.capture_terminate();
    }
    if (backend.capture_cleanup) {
        backend.capture_cleanup();
    }
    return ret;
}

static int image_data_cb(int width, int height, uint8_t *rgb_data)
{
    if (hyperion_set_image(rgb_data, width, height) != 0)
    {
        fprintf(stderr, "Write timeout\n");
        hyperion_destroy();
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
    char *address = "";
    int port = 0;
    int width = 0;
    int height = 0;
    int fps = 0;
    char *backend = "";
    bool novideo = false;
    bool nogui = false;
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }


    PmLogInfo(logcontext, "FNCSTART", 0,  "Getting values from msg input..");
    address = jval_to_string(parsed, "address", "");
    port = jval_to_int(parsed, "port", port);
    width = jval_to_int(parsed, "width", width);
    height = jval_to_int(parsed, "height", height);
    fps = jval_to_int(parsed, "fps", fps);
    backend = jval_to_string(parsed, "backend", backend);
    novideo = jval_to_bool(parsed, "novideo", novideo);
    nogui = jval_to_bool(parsed, "nogui", nogui);

    PmLogInfo(logcontext, "STARTFNC", 0, "Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d", address, port, width, height, fps, backend, novideo, nogui);


    /**
     * JSON create test
     */
    jobj = jobject_create();
    if (jis_null(jobj)) {
        j_release(&jobj);
        return true;
    }
    
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("address"), jstring_create(address));
    jobject_set(jobj, j_cstr_to_buffer("port"), jnumber_create_i32(port));
    jobject_set(jobj, j_cstr_to_buffer("width"), jnumber_create_i32(width));
    jobject_set(jobj, j_cstr_to_buffer("height"), jnumber_create_i32(height));
    jobject_set(jobj, j_cstr_to_buffer("fps"), jnumber_create_i32(fps));
    jobject_set(jobj, j_cstr_to_buffer("backend"), jstring_create(backend));
    jobject_set(jobj, j_cstr_to_buffer("novideo"), jboolean_create(novideo));
    jobject_set(jobj, j_cstr_to_buffer("nogui"), jboolean_create(nogui));

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
    const char *address = NULL;
    const char *port = NULL;
    const char *width = NULL;
    const char *height = NULL;
    const char *fps = NULL;
    const char *backend = NULL;
    const char *novideo = NULL;
    const char *nogui = NULL;
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }

    // Get value from payload.input and JSON Object to string without schema validation check
    value = jobject_get(parsed, j_cstr_to_buffer("address"));
    address = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("port"));
    port = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("width"));
    width = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("height"));
    height = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("fps"));
    fps = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("backend"));
    backend = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("novideo"));
    novideo = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("nogui"));
    nogui = jvalue_tostring_simple(value);



    /**
     * JSON create test
     */
    jobj = jobject_create();
    if (jis_null(jobj)) {
        j_release(&jobj);
        return true;
    }
    
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("address"), jstring_create(address));
    jobject_set(jobj, j_cstr_to_buffer("port"), jstring_create(port));
    jobject_set(jobj, j_cstr_to_buffer("width"), jstring_create(width));
    jobject_set(jobj, j_cstr_to_buffer("height"), jstring_create(height));
    jobject_set(jobj, j_cstr_to_buffer("fps"), jstring_create(fps));
    jobject_set(jobj, j_cstr_to_buffer("backend"), jstring_create(backend));
    jobject_set(jobj, j_cstr_to_buffer("novideo"), jstring_create(novideo));
    jobject_set(jobj, j_cstr_to_buffer("nogui"), jstring_create(nogui));

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
    const char *address = NULL;
    const char *port = NULL;
    const char *width = NULL;
    const char *height = NULL;
    const char *fps = NULL;
    const char *backend = NULL;
    const char *novideo = NULL;
    const char *nogui = NULL;
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }

    // Get value from payload.input and JSON Object to string without schema validation check
    value = jobject_get(parsed, j_cstr_to_buffer("address"));
    address = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("port"));
    port = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("width"));
    width = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("height"));
    height = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("fps"));
    fps = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("backend"));
    backend = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("novideo"));
    novideo = jvalue_tostring_simple(value);
    value = jobject_get(parsed, j_cstr_to_buffer("nogui"));
    nogui = jvalue_tostring_simple(value);



    /**
     * JSON create test
     */
    jobj = jobject_create();
    if (jis_null(jobj)) {
        j_release(&jobj);
        return true;
    }
    
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("address"), jstring_create(address));
    jobject_set(jobj, j_cstr_to_buffer("port"), jstring_create(port));
    jobject_set(jobj, j_cstr_to_buffer("width"), jstring_create(width));
    jobject_set(jobj, j_cstr_to_buffer("height"), jstring_create(height));
    jobject_set(jobj, j_cstr_to_buffer("fps"), jstring_create(fps));
    jobject_set(jobj, j_cstr_to_buffer("backend"), jstring_create(backend));
    jobject_set(jobj, j_cstr_to_buffer("novideo"), jstring_create(novideo));
    jobject_set(jobj, j_cstr_to_buffer("nogui"), jstring_create(nogui));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&parsed);
    return true;
}

char* jval_to_string(jvalue_ref parsed, const char *item, const char *def)
{
    PmLogInfo(logcontext, "FNCTOSTRN", 0,  "Start");
	jvalue_ref jobj = NULL;
	raw_buffer jbuf;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_string(jobj)) {
		return g_strdup(def);
    }

	jbuf = jstring_get(jobj);
	return g_strdup(jbuf.m_str);
}

bool jval_to_bool(jvalue_ref parsed, const char *item, bool def)
{
    PmLogInfo(logcontext, "FNCTOBOOL", 0,  "Start");
	jvalue_ref jobj;
	bool ret;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_boolean(jobj)) {
		return def;
    }

	jboolean_get(jobj, &ret);
	return ret;
}

int jval_to_int(jvalue_ref parsed, const char *item, int def)
{
    PmLogInfo(logcontext, "FNCTOINT", 0,  "Start");
	jvalue_ref jobj = NULL;
	int ret = 0;

	if (!jobject_get_exists(parsed, j_str_to_buffer(item, strlen(item)), &jobj) || !jis_number(jobj)) {
		return def;
    }
	jnumber_get_i32(jobj, &ret);
	return ret;
}