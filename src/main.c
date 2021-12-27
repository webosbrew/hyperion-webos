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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
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

static struct option long_options[] = {
    {"width", required_argument, 0, 'x'},
    {"height", required_argument, 0, 'y'},
    {"address", required_argument, 0, 'a'},
    {"port", required_argument, 0, 'p'},
    {"fps", required_argument, 0, 'f'},
    {"no-video", no_argument, 0, 'V'},
    {"no-gui", no_argument, 0, 'G'},
    {"no-service", no_argument, 0, 'S'},
    {"backend", required_argument, 0, 'b'},
    {"help", no_argument, 0, 'h'},
    {"config", required_argument, 0, 'c'},
    {"save-conf", required_argument, 0, 's'},
    {0, 0, 0, 0},
};

pthread_t capture_thread;

// Main loop for aliving background service
GMainLoop *gmainLoop;
PmLogContext logcontext;

LSHandle  *sh = NULL;
LSMessage *message;
// Declare of each method
bool method_start(LSHandle *sh, LSMessage *message, void *data);
bool method_stop(LSHandle *sh, LSMessage *message, void *data);
bool method_is_root(LSHandle *sh, LSMessage *message, void *data);
bool method_is_running(LSHandle *sh, LSMessage *message, void *data);
bool method_get_settings(LSHandle *sh, LSMessage *message, void *data);
bool method_set_settings(LSHandle *sh, LSMessage *message, void *data);
bool method_reset_settings(LSHandle *sh, LSMessage *message, void *data);
bool method_restart(LSHandle *sh, LSMessage *message, void *data);

//Callbacks
static bool cb_make_root(LSHandle *sh, LSMessage *msg, void *user_data);

LSMethod lunaMethods[] = {
    {"start", method_start},       // luna://org.webosbrew.piccap.service/XXXX
    {"stop", method_stop},
    {"isRoot", method_is_root},
    {"isRunning", method_is_running},
    {"getSettings", method_get_settings},
    {"setSettings", method_set_settings},
    {"resetSettings", method_reset_settings},
    {"restart", method_restart}
};


bool rooted = false;
bool app_quit = false;
bool exitme = false;
bool isrunning = false;
bool initialized = false;

static const char *_backend = "";
static const char *_address = "";
static const char *_configpath= "";
static const char *conffile = "config.json"; //default name using for webOS-Service
static int _port = 19400;
static bool autostart = false;

static cap_backend_config_t config = {0, 0, 360, 180, 0, 0, 0};
static cap_backend_funcs_t backend = {NULL};


static int image_data_cb(int width, int height, uint8_t *rgb_data);
int capture_main();
void *capture_loop(void *data);
int cleanup();

int get_starting_path(char *retstr);
int make_root(LSHandle *handle);
int check_root(LSHandle *handle);
int set_default();
int load_settings();
int save_settings(const char *savestring);
int remove_settings();

int luna_resp(LSHandle *sh, LSMessage *message, char *replyPayload, LSError *lserror);
//JSON helper functions
char* jval_to_string(jvalue_ref parsed, const char *item, const char *def);
bool jval_to_bool(jvalue_ref parsed, const char *item, bool def);
int jval_to_int(jvalue_ref parsed, const char *item, int def);

int get_starting_path(char *retstr){
    int length;
    char fullpath[FILENAME_MAX];
    char *dirpath;
     
     /* /proc/self is a symbolic link to the process-ID subdir
      * of /proc, e.g. /proc/4323 when the pid of the process
      * of this program is 4323.
      *
      * Inside /proc/<pid> there is a symbolic link to the
      * executable that is running as this <pid>.  This symbolic
      * link is called "exe".
      *
      * So if we read the path where the symlink /proc/self/exe
      * points to we have the full path of the executable.
      * https://www.linuxquestions.org/questions/programming-9/get-full-path-of-a-command-in-c-117965/
      */


    length = readlink("/proc/self/exe", fullpath, sizeof(fullpath));
     
    /* Catch some errors: */
    if (length < 0) {
        PmLogError(logcontext, "FNCGPATH", 0, "Error resolving symlink /proc/self/exe.");
        return -1;
    }
    if (length >= FILENAME_MAX) {
        PmLogError(logcontext, "FNCGPATH", 0, "Path too long. Truncated.");
        return -1;
    }

    /* I don't know why, but the string this readlink() function 
    * returns is appended with a '@'.
    */
    fullpath[length] = '\0';       /* Strip '@' off the end. */
    dirpath = dirname(fullpath);
    strcat(dirpath,"/");

    strcpy(retstr,dirpath);
    PmLogInfo(logcontext, "FNCGPATH", 0, "Full path is: %s", retstr);
    return 0;
}

static int import_backend_library(const char *library_filename) {
    char *error;
    char libpath[FILENAME_MAX];

    get_starting_path(libpath);
    PmLogInfo(logcontext, "FNCDLOPEN", 0, "Full exec path: %s", libpath);
    strcat(libpath, library_filename);
    PmLogInfo(logcontext, "FNCDLOPEN", 0, "Full library path: %s", libpath);

    void *handle = dlopen(libpath, RTLD_LAZY);
    if (handle == NULL) {
        PmLogError(logcontext, "FNCDLOPEN", 0, "Error! Failed to load backend library: %s, error: %s", libpath, dlerror());
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

int set_default(){
    PmLogInfo(logcontext, "FNCSETDEF", 0, "Setting default settings to runtime..");
    _address = "";
    _port = 19400;
    config.resolution_width = 360;
    config.resolution_height = 180;
    config.fps = 0;
    _backend = "libdile_vt";
    config.no_video = false;
    config.no_gui = false;
    autostart = false;
    PmLogInfo(logcontext, "FNCSETDEF", 0, "Finished setting default.");
    return 0;
}

int check_root(LSHandle *handle){
    int uid;
    uid = geteuid();
    if(uid != 0){
        PmLogError(logcontext, "FNCISROOT", 0, "Service is not running as root! ID: %d", uid);
        rooted = false;
        PmLogInfo(logcontext, "FNCISROOT", 0, "Trying to evaluate using HBChannel/exec-Service!");
        if(make_root(handle) != 0){
            PmLogError(logcontext, "FNCISROOT", 0, "Error while making root!");
        }
    }else{
        PmLogInfo(logcontext, "FNCISROOT", 0, "Service is running as root!");
        rooted = true;
    }
    return 0;
}

static bool cb_make_root(LSHandle *sh, LSMessage *msg, void *user_data){

    PmLogInfo(logcontext, "FNCMKROOTCB", 0, "Callback received.");
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};

    bool retval;
    char *retstr;

    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    LSError lserror;
    LSErrorInit(&lserror);


    PmLogInfo(logcontext, "FNCMKROOTCB", 0,  "Parsing values from msg input..");
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(msg)), DOMOPT_NOOPT, &schemaInfo);

	if (jis_null(parsed)) {
        PmLogInfo(logcontext, "FNCMKROOTCB", 0,  "Failed parsing values from msg input!");
        j_release(&parsed);
        return false;
    }

    PmLogInfo(logcontext, "FNCMKROOTCB", 0,  "Checking returnvalue..");
	retval = jval_to_bool(parsed, "returnValue", false); 

    if(retval)
    {
        PmLogInfo(logcontext, "FNCMKROOTCB", 0,  "Returnvalue true, checking stdoutString..");
        retstr = jval_to_string(parsed, "stdoutString", "No value!");
        PmLogInfo(logcontext, "FNCMKROOTCB", 0, "HBChannel/exec returned: %s", retstr);
        PmLogInfo(logcontext, "FNCMKROOTCB", 0, "Lets terminate us, to get restarted! :)");
        raise(SIGTERM);
    }else{
        PmLogError(logcontext, "FNCMKROOTCB", 0, "Returnvalue false! Errors occoured.");
    }

    return true;
}

int make_root(LSHandle *handle){
    LSError lserror;
    if(!LSCall(handle, "luna://org.webosbrew.hbchannel.service/exec","{\"command\":\"/media/developer/apps/usr/palm/services/org.webosbrew.hbchannel.service/elevate-service org.webosbrew.piccap.service\"}", cb_make_root, NULL, NULL, &lserror)){
        PmLogError(logcontext, "FNCMKROOT", 0, "Error while executing HBChannel/exec!");
        LSErrorPrint(&lserror, stderr);
        return 1;
    }
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
        break;
    case SIGTERM:
        PmLogError(logcontext, "SIGTERM", 0, "SIGTERM called! Stopping capture if running and exit..");
        exitme = true;
        app_quit = true;
        break;
    default:
        break;
    }
}

static void print_usage()
{
    printf("Usage: hyperion-webos -a ADDRESS -S [OPTION]...\n");
    printf("\n");
    printf("Grab screen content continously and send to Hyperion via flatbuffers server.\n");
    printf("Application has to be named to hyperion-webos to avoid bugs!\n");
    printf("\n");
    printf("  -S, --no-service      Run this from CLI and not as webOS-Service\n");
    printf("  -x, --width=WIDTH     Width of video frame (default 192)\n");
    printf("  -y, --height=HEIGHT   Height of video frame (default 108)\n");
    printf("  -a, --address=ADDR    IP address of Hyperion server\n");
    printf("  -p, --port=PORT       Port of Hyperion flatbuffers server (default 19400)\n");
    printf("  -f, --fps=FPS         Framerate for sending video frames (default 0 = unlimited)\n");
    printf("  -b, --backend=BE      Use specific backend (default auto)\n");
    printf("  -V, --no-video        Video will not be captured\n");
    printf("  -G, --no-gui          GUI/UI will not be captured\n");
    printf("  -c, --config=PATH     Absolute path for configfile to load settings. Giving additional runtime arguments will overwrite loaded ones.\n");
    printf("  -s, --save-conf=PATH  Saving configfile to given path.\n");
    
}

static int parse_options(int argc, char *argv[])
{
    PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Starting parsing arguments..");
    PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Setting default settings before parsing..");
    if(set_default() != 0){
        PmLogError(logcontext, "FNCPARSEOPT", 0, "Error while setting default settings!");
    }
    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "x:y:a:p:f:b:h:c:s:SVG", long_options, &longindex)) != -1)
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
        case 'S':
            config.no_service = 1;
            break;
        case 'b':
            _backend = strdup(optarg);
            break;
        case 'c':
            config.load_config = 1;
            _configpath = strdup(optarg);
            break;
        case 's':
            config.save_config = 1;
            _configpath = strdup(optarg);
            break;
        case 'h':
        default:
            print_usage();
            return 1;
        }
    }

    if (config.no_service == 1){
        if (config.load_config == 1){
            PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Loading settings from disk to runtime..");
            if(load_settings() != 0){
                PmLogError(logcontext, "FNCPARSEOPT", 0, "Error while loading settings!");
            }
            PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Finished loading settings.");
        }

        if (config.save_config == 1){
            jvalue_ref tosave = {0};

            PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Creating JSON-String to save..");
            tosave = jobject_create();
            jobject_set(tosave, j_cstr_to_buffer("address"), jstring_create(_address));
            jobject_set(tosave, j_cstr_to_buffer("port"), jnumber_create_i32(_port));
            jobject_set(tosave, j_cstr_to_buffer("width"), jnumber_create_i32(config.resolution_width));
            jobject_set(tosave, j_cstr_to_buffer("height"), jnumber_create_i32(config.resolution_height));
            jobject_set(tosave, j_cstr_to_buffer("fps"), jnumber_create_i32(config.fps));
            jobject_set(tosave, j_cstr_to_buffer("backend"), jstring_create(_backend));
            jobject_set(tosave, j_cstr_to_buffer("novideo"), jboolean_create(config.no_video));
            jobject_set(tosave, j_cstr_to_buffer("nogui"), jboolean_create(config.no_gui));
            jobject_set(tosave, j_cstr_to_buffer("autostart"), jboolean_create(autostart));
            
            PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Saving JSON-String to disk..");

            if(save_settings(jvalue_tostring_simple(tosave)) != 0){
                PmLogError(logcontext, "FNCPARSEOPT", 0, "Error while saveing settings to disk!");
            }
            j_release(&tosave);
            PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Finished saving settings.");
        }


        if (strcmp(_address, "") == 0)
        {
            PmLogError(logcontext, "FNCPARSEOPT", 0, "Error! Address not specified.\n");
            print_usage();
            return 1;
        }
        if (config.fps < 0 || config.fps > 60)
        {
            PmLogError(logcontext, "FNCPARSEOPT", 0, "Error! FPS should between 0 (unlimited) and 60.\n");
            print_usage();
            return 1;
        }
        if (config.fps == 0)
            config.framedelay_us = 0;
        else
            config.framedelay_us = 1000000 / config.fps;
    }
    PmLogInfo(logcontext, "FNCPARSEOPT", 0, "Finished parsing arguments.");
    return 0;
}


int main(int argc, char *argv[])
{
    PmLogGetContext("hyperion-webos_service", &logcontext);
    PmLogInfo(logcontext, "FNCMAIN", 0, "Service main starting..");
    signal(SIGINT, handle_signal);

    int ret;
    if ((ret = parse_options(argc, argv)) != 0)
    {
        return ret;
    }

    if(config.no_service == 1){

        if (getenv("XDG_RUNTIME_DIR") == NULL)
        {
            setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
        }

        if ((capture_main()) != 0){
            PmLogError(logcontext, "FNCSTART", 0,  "ERROR: Capture main init failed!");
            return 1;
        }
        return ret;

    }else{

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

        LSRegisterCategory(handle,"/",lunaMethods, NULL, NULL, &lserror);

        LSGmainAttach(handle, gmainLoop, &lserror);

        PmLogInfo(logcontext, "FNCMAIN", 0, "Checking service root status..");
        if(check_root(handle) != 0){
            PmLogError(logcontext, "FNCMAIN", 0, "Error while checking for root status!");
        }

        PmLogInfo(logcontext, "FNCMAIN", 0, "Setting default settings before loading..");
        if(set_default() != 0){
            PmLogError(logcontext, "FNCMAIN", 0, "Error while setting default settings!");
        }

        PmLogInfo(logcontext, "FNCMAIN", 0, "Loading settings from disk to runtime..");
        if(load_settings() != 0){
            PmLogError(logcontext, "FNCMAIN", 0, "Error while loading settings!");
        }

        if(autostart && !rooted){
            PmLogError(logcontext, "FNCMAIN", 0, "Service isn't rooted! Setting autostart to false.");
            autostart = false;
        }

        if(autostart){
            if (isrunning){
                PmLogError(logcontext, "FNCMAIN", 0,  "Capture already running");
                goto skip;
            }

            //Ensure set before starting
            if (strcmp(_address, "") == 0 || strcmp(_backend, "") == 0 || config.fps < 0 || config.fps > 60){
                PmLogError(logcontext, "FNCMAIN", 0, "ERROR: Address and Backend are neccassary parameters and FPS should be between 0 (unlimited) and 60! | Address: %s | Backend: %s | FPS: %d", _address, _backend, config.fps);
                goto skip;
            }

            if (config.fps == 0){
                config.framedelay_us = 0;
            }else{
                config.framedelay_us = 1000000 / config.fps;
            }

            PmLogInfo(logcontext, "FNCMAIN", 0, "Using these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
            PmLogInfo(logcontext, "FNCMAIN", 0,  "Calling capture start main..");

            //luna-send -a org.webosbrew.piccap -f -n 1 luna://com.webos.notification/createToast '{"sourceId":"org.webosbrew.piccap","message": "PicCap startup is enabled! Calling service for startup.."}'
            if(!LSCall(handle, "luna://com.webos.notification/createToast","{\"sourceId\":\"org.webosbrew.piccap\",\"message\": \"PicCap startup is enabled! Calling service for startup..\"}", NULL, NULL, NULL, &lserror)){
                PmLogError(logcontext, "FNCMAIN", 0, "Error while executing toast notification!");
                LSErrorPrint(&lserror, stderr);
            }

            if ((capture_main()) != 0){
                PmLogError(logcontext, "FNCMAIN", 0,  "ERROR: Capture main init failed!");
                if(!LSCall(handle, "luna://com.webos.notification/createToast","{\"sourceId\":\"org.webosbrew.piccap\",\"message\": \"Error while executing PicCap-Startup!\"}", NULL, NULL, NULL, &lserror)){
                    PmLogError(logcontext, "FNCMAIN", 0, "Error while executing toast notification!");
                    LSErrorPrint(&lserror, stderr);
                }
                goto skip;
            }

        }

        skip:
            PmLogInfo(logcontext, "FNCMAIN", 0, "Going into main loop..");
            // run to check continuously for new events from each of the event sources
            g_main_loop_run(gmainLoop);
            // Decreases the reference count on a GMainLoop object by one
            g_main_loop_unref(gmainLoop);

            PmLogInfo(logcontext, "FNCMAIN", 0, "Service main finishing..");

    }
    return 0;
}

int capture_main(){
    initialized = false;
    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Beginning capture main init..");
    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Detecting backend..");
    if ((detect_backend()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! detect_backend.");
        cleanup();
        return -1;
    }

    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Detecting backend..");
    if ((backend.capture_preinit(&config, &image_data_cb)) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_preinit.");
        cleanup();
        return -1;
    }

    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Initiating capture..");
    if ((backend.capture_init()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_init.");
        cleanup();
        return -1;
    }

    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Starting capture..");
    int ret;
    if ((ret = backend.capture_start()) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! capture_start. Code: %d", ret);
        cleanup();
        return -1;
    }

    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Connecting hyperion-client..");
    if ((hyperion_client("webos", _address, _port, 150)) != 0)
    {
        PmLogError(logcontext, "FNCCPTMAIN", 0, "Error! hyperion_client.");
        cleanup();
        return -1;
    }
    PmLogInfo(logcontext, "FNCCPTMAIN", 0, "Capture main init completed. Creating subproccess for looping..");
    initialized = true;
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
    PmLogInfo(logcontext, "FNCCPTLOOP", 0, "Ending connection loop");
    if(exitme){
        PmLogInfo(logcontext, "FNCCPTLOOP", 0, "exitme true -> Exit");
        exit(0);
    }
    cleanup();
    return 0;
}

int cleanup(){
    PmLogInfo(logcontext, "FNCCLEAN", 0, "Starting cleanup..");
    if (isrunning){
        PmLogInfo(logcontext, "FNCCLEAN", 0, "Capture is running! Breaking loop and joining thread..");
        app_quit=true;
        pthread_join(capture_thread, NULL);
        isrunning=false;
    }
    PmLogInfo(logcontext, "FNCCLEAN", 0, "Destroying hyperion-client..");
    hyperion_destroy();
    if(initialized){
        if (backend.capture_terminate) {
            PmLogInfo(logcontext, "FNCCLEAN", 0, "Terminating capture within library..");
            backend.capture_terminate();
            initialized = false;
        }
    }
    if (backend.capture_cleanup) {
        PmLogInfo(logcontext, "FNCCLEAN", 0, "Cleanup capture within library..");
        backend.capture_cleanup();
    }
    PmLogInfo(logcontext, "FNCCLEAN", 0, "Cleanup finished.");
    return 0;
}

static int image_data_cb(int width, int height, uint8_t *rgb_data)
{
    if (hyperion_set_image(rgb_data, width, height) != 0)
    {
        PmLogError(logcontext, "FNCIMGDATA", 0, "Error! Write timeout.");
        isrunning = false;
        app_quit = true;
    }
}

int load_settings(){
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0};
    char *confbuf;
    int sconf, sstr;
    char confpath[FILENAME_MAX];
    int retvalue = 0;

    

    PmLogInfo(logcontext, "FNCLOADCFG", 0,  "Try to read configfile.");
    if(strcmp(_configpath, "") == 0){
        get_starting_path(confpath);
        strcat(confpath, conffile);
    }else{
        strcat(confpath, _configpath);
    }
    FILE *jconf = fopen(confpath,"r");
    if(jconf){
        fseek(jconf, 0, SEEK_END);
        sstr = ftell(jconf);
        rewind(jconf);
        confbuf = malloc(sizeof(char) * (sstr+1));
        sconf = fread(confbuf, sizeof(char), sstr, jconf);
        confbuf[sstr] = '\0';
        retvalue = 0;
        if(sstr != sconf){
            PmLogError(logcontext, "FNCLOADCFG", 0,  "Errors reading configfile at location %s", confpath);
            free(confbuf);
            fclose(jconf);
            return 2;
        }
        fclose(jconf);
    }else{
        PmLogError(logcontext, "FNCLOADCFG", 0,  "Couldn't read configfile at location %s", confpath);
        retvalue = 1;
    }

    if(retvalue == 0){
        PmLogInfo(logcontext, "FNCLOADCFG", 0,  "Read configfile at %s. Contents: %s", confpath, confbuf);
        jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);
        parsed = jdom_parse(j_cstr_to_buffer(confbuf), DOMOPT_NOOPT, &schemaInfo);
        if (jis_null(parsed)) {
            PmLogError(logcontext, "FNCLOADCFG", 0,  "Error parsing config.");
            j_release(&parsed);
            free(confbuf);
            return 2;
        }
        free(confbuf);
    }else{
        PmLogError(logcontext, "FNCLOADCFG", 0,  "config.json at path %s may not found! Will be using default configuration.", confpath);
    }


    _address = jval_to_string(parsed, "address", _address);
    _port = jval_to_int(parsed, "port", _port);
    config.resolution_width = jval_to_int(parsed, "width", config.resolution_width);
    config.resolution_height = jval_to_int(parsed, "height", config.resolution_height);
    config.fps = jval_to_int(parsed, "fps", config.fps);
    _backend = jval_to_string(parsed, "backend", _backend);
    config.no_video = jval_to_bool(parsed, "novideo", config.no_video);
    config.no_gui = jval_to_bool(parsed, "nogui", config.no_gui);
    autostart = jval_to_bool(parsed, "autostart", autostart);

    PmLogInfo(logcontext, "FNCLOADCFG", 0, "Loaded these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
    j_release(&parsed);
    return retvalue;
}

bool method_get_settings(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCGCONF", 0,  "Luna call getSettings recieved.");
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref value = {0};
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };
    int load;

    LSErrorInit(&lserror);

    // Initialize schema
   
    load = load_settings();
    if(load == 0){
        PmLogInfo(logcontext, "FNCGCONF", 0, "Loading settings successfully.");
    }else if(load == 1){
        PmLogInfo(logcontext, "FNCGCONF", 0, "Loading settings not successfully. Using default settings.");
    }else{ 
        PmLogInfo(logcontext, "FNCGCONF", 0, "Error while loading settings. Sending back error.");
        backmsg = "Error loading settings!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    PmLogInfo(logcontext, "FNCGCONF", 0, "Sending these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
 
    //Response
    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    backmsg = "Settings got successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("ip"), jstring_create(_address));
    jobject_set(jobj, j_cstr_to_buffer("port"), jnumber_create_i32(_port));
    jobject_set(jobj, j_cstr_to_buffer("width"), jnumber_create_i32(config.resolution_width));
    jobject_set(jobj, j_cstr_to_buffer("height"), jnumber_create_i32(config.resolution_height));
    jobject_set(jobj, j_cstr_to_buffer("fps"), jnumber_create_i32(config.fps));
    jobject_set(jobj, j_cstr_to_buffer("backend"), jstring_create(_backend));
    jobject_set(jobj, j_cstr_to_buffer("captureVideo"), jboolean_create(!config.no_video));
    jobject_set(jobj, j_cstr_to_buffer("captureUI"), jboolean_create(!config.no_gui));
    jobject_set(jobj, j_cstr_to_buffer("autostart"), jboolean_create(autostart)); 
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    PmLogInfo(logcontext, "FNCGCONF", 0,  "Luna call getSettings finished.");
    j_release(&jobj);
    return true;
}

int save_settings(const char *savestring){
    char confpath[FILENAME_MAX];
    int retvalue = 0;

    PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Try to save configfile.");
    if(strcmp(_configpath, "") == 0){
        get_starting_path(confpath);
        strcat(confpath, conffile);
    }else{
        strcat(confpath, _configpath);
    }
    FILE *jconf = fopen(confpath,"w+");
    if(jconf){
        PmLogInfo(logcontext, "FNCSAVECFG", 0,  "File opened, writing JSON..");
        fwrite(savestring, 1, strlen(savestring), jconf);
        fclose(jconf);
        retvalue = 0;
    }else{
        PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Couldn't open configfile for write at location %s", confpath);
        retvalue = 1;
    }

    PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Autostart: %d", autostart);
    if (autostart){
        PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Autostart enabled. Checking symlink.");
        char *startpath = "/var/lib/webosbrew/init.d/piccapautostart";
        char *startupdir = "/var/lib/webosbrew/init.d";
        char origpath[FILENAME_MAX];
        char *autostartfile = "piccapautostart";

        get_starting_path(origpath);
        strcat(origpath, autostartfile);

        if(access(startpath, F_OK) == 0){
            PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Autostart enabled. Symlink to HBChannel init.d already exists. Nothing to do");
        }else{
            PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Autostart enabled. Trying to create symlink to HBChannel init.d.");
            mkdir(startupdir, 0755);
            if(symlink(origpath, startpath) != 0){
                PmLogError(logcontext, "FNCSAVECFG", 0, "Error while creating symlink!");
                retvalue = 2;
            }else{
                PmLogInfo(logcontext, "FNCSAVECFG", 0,  "Symlink created.");
            }
        }
    }

    return retvalue;
}

bool method_set_settings(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCSCONF", 0,  "Luna call setSettings recieved.");
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};
    jvalue_ref tosave = {0}, jreturnValue = {0}, jobj = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };
    int save;

    LSErrorInit(&lserror);

    // Initialize schema
    jschema_info_init (&schemaInfo, jschema_all(), NULL, NULL);

    // get message from LS2 and parsing to make object
    PmLogInfo(logcontext, "FNCSCONF", 0,  "Parsing values from msg input..");
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        backmsg = "Error while parsing input!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    PmLogInfo(logcontext, "FNCSCONF", 0,  "Putting parsed values to runtime..");
    _address = jval_to_string(parsed, "ip", _address);
    _port = jval_to_int(parsed, "port", _port);
    config.resolution_width = jval_to_int(parsed, "width", config.resolution_width);
    config.resolution_height = jval_to_int(parsed, "height", config.resolution_height);
    config.fps = jval_to_int(parsed, "fps", config.fps);
    _backend = jval_to_string(parsed, "backend", _backend);
    config.no_video = !jval_to_bool(parsed, "captureVideo", !config.no_video);
    config.no_gui = !jval_to_bool(parsed, "captureUI", !config.no_gui);
    autostart = jval_to_bool(parsed, "autostart", autostart);


    PmLogInfo(logcontext, "FNCSCONF", 0,  "Creating JSON from runtime..");
    tosave = jobject_create();
    jobject_set(tosave, j_cstr_to_buffer("address"), jstring_create(_address));
    jobject_set(tosave, j_cstr_to_buffer("port"), jnumber_create_i32(_port));
    jobject_set(tosave, j_cstr_to_buffer("width"), jnumber_create_i32(config.resolution_width));
    jobject_set(tosave, j_cstr_to_buffer("height"), jnumber_create_i32(config.resolution_height));
    jobject_set(tosave, j_cstr_to_buffer("fps"), jnumber_create_i32(config.fps));
    jobject_set(tosave, j_cstr_to_buffer("backend"), jstring_create(_backend));
    jobject_set(tosave, j_cstr_to_buffer("novideo"), jboolean_create(config.no_video));
    jobject_set(tosave, j_cstr_to_buffer("nogui"), jboolean_create(config.no_gui));
    jobject_set(tosave, j_cstr_to_buffer("autostart"), jboolean_create(autostart));

    PmLogInfo(logcontext, "FNCSCONF", 0,  "Saving JSON to disk..");
    save = save_settings(jvalue_tostring_simple(tosave));
    if(save != 0){
        backmsg = "Errors while saving file to disk!";
        luna_resp(sh, message, backmsg, &lserror);
        j_release(&tosave);
        j_release(&parsed);
        return true;
    }

    PmLogInfo(logcontext, "FNCSCONF", 0, "Saved these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
 
    //Response
    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    backmsg = "Settings set successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    PmLogInfo(logcontext, "FNCSCONF", 0,  "Luna call setSettings finished.");
    j_release(&jobj);
    j_release(&tosave);
    j_release(&parsed);
    return true;
}

int remove_settings(){
    char confpath[FILENAME_MAX];
    int retval = 0;

    PmLogInfo(logcontext, "FNCREMCFG", 0,  "Try to delete configfile.");
    get_starting_path(confpath);
    strcat(confpath, conffile);
    if(remove(confpath) != 0){
        PmLogError(logcontext, "FNCREMCFG", 0,  "Error while deleting configfile at path %s", confpath);
        retval = 1;
    }else{
        PmLogInfo(logcontext, "FNCREMCFG", 0,  "Configfile successfully deleted at path %s", confpath);
        retval = 0;
    }


    PmLogInfo(logcontext, "FNCREMCFG", 0,  "Removing autostart symlink, if exists.");
    char *startpath = "/var/lib/webosbrew/init.d/piccapautostart";

    if(access(startpath, F_OK) != 0){
        PmLogInfo(logcontext, "FNCREMCFG", 0,  "Symlink doesnt exists. Nothing to do");
    }else{
        PmLogInfo(logcontext, "FNCREMCFG", 0,  "Autostart enabled. Trying to remove symlink to HBChannel init.d.");
        if(unlink(startpath) != 0){
            PmLogError(logcontext, "FNCREMCFG", 0, "Error while deleting symlink!");
            retval = 2;
        }else{
            PmLogInfo(logcontext, "FNCREMCFG", 0,  "Symlink removed.");
        }
    }
    
    return retval;
}

bool method_reset_settings(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCRCONF", 0,  "Luna call resetSettings recieved.");
    LSError lserror;
    JSchemaInfo schemaInfo;
    jvalue_ref parsed = {0}, value = {0};
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    PmLogInfo(logcontext, "FNCRCONF", 0,  "Removing settings..");
    if(remove_settings() != 0){
        PmLogError(logcontext, "FNCRCONF", 0,  "Errors while removing settings.");
        backmsg = "Errors while removing settings.";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    PmLogInfo(logcontext, "FNCRCONF", 0,  "Setting defaults..");
    if(set_default() != 0){
        PmLogError(logcontext, "FNCRCONF", 0,  "Errors while setting default settings!");
        backmsg = "Errors while setting default settings!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    //TODO: Maybe some other cleanup?

    PmLogInfo(logcontext, "FNCRCONF", 0, "Set to these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
 

    //Response
    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    backmsg = "Settings reset successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    PmLogInfo(logcontext, "FNCRCONF", 0,  "Luna call resetSettings finished.");
    j_release(&parsed);
    j_release(&jobj);
    return true;
}


bool method_start(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCSTART", 0,  "Luna call start recieved.");
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
    PmLogInfo(logcontext, "FNCSTART", 0,  "Parsing values from msg input, ignored for now..");
    parsed = jdom_parse(j_cstr_to_buffer(LSMessageGetPayload(message)), DOMOPT_NOOPT, &schemaInfo);

    if (jis_null(parsed)) {
        j_release(&parsed);
        PmLogError(logcontext, "FNCSTART", 0,  "Error while parsing input");
        backmsg = "Error while parsing input!"; 
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    if (isrunning){
        PmLogError(logcontext, "FNCSTART", 0,  "Capture already running");
        backmsg = "Capture already running!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    if (!rooted){
        PmLogError(logcontext, "FNCSTART", 0,  "Service not rooted");
        backmsg = "Service not running as root!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    //Ensure set before starting
    if (strcmp(_address, "") == 0 || strcmp(_backend, "") == 0 || config.fps < 0 || config.fps > 60){
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

    PmLogInfo(logcontext, "FNCSTART", 0, "Using these values: Address: %s | Port: %d | Width: %d | Height: %d | FPS: %d | Backend: %s | NoVideo: %d | NoGUI: %d | Autostart: %d", _address, _port, config.resolution_width, config.resolution_height, config.fps, _backend, config.no_video, config.no_gui, autostart);
    PmLogInfo(logcontext, "FNCSTART", 0,  "Calling capture start main..");

    if ((capture_main()) != 0){
        PmLogError(logcontext, "FNCSTART", 0,  "ERROR: Capture main init failed!");
        backmsg = "ERROR: Capture main init failed!";
        luna_resp(sh, message, backmsg, &lserror);
        return true;
    }

    jobj = jobject_create();
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
    jobject_set(jobj, j_cstr_to_buffer("autostart"), jboolean_create(autostart));

    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    PmLogInfo(logcontext, "FNCSTART", 0,  "Luna call start finished.");
    j_release(&parsed);
    j_release(&jobj);
    return true;
}

bool method_stop(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCSTOP", 0,  "Luna call stop recieved.");
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

    app_quit=true;

    jobj = jobject_create();

    jreturnValue = jboolean_create(TRUE);
    backmsg = "Capture stopped successfully!";
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    PmLogInfo(logcontext, "FNCSTOP", 0,  "Luna call stop finished.");
    j_release(&parsed);
    j_release(&jobj);
    return true;
}

bool method_restart(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCRESTART", 0,  "Luna call restart recieved.");
    LSError lserror;
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    backmsg = "Killing service..";

    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    PmLogInfo(logcontext, "FNCISRUN", 0,  "Luna call isRunning finished.");
    raise(SIGTERM);
    return true;
}


bool method_is_running(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCISRUN", 0,  "Luna call isRunning recieved.");
    LSError lserror;
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);


    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("isRunning"), jboolean_create(isrunning));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    PmLogInfo(logcontext, "FNCISRUN", 0,  "Luna call isRunning finished.");
    return true;
}

bool method_is_root(LSHandle *sh, LSMessage *message, void *data)
{
    PmLogInfo(logcontext, "FNCISROOT", 0,  "Luna call isRoot recieved.");
    LSError lserror;
    jvalue_ref jobj = {0}, jreturnValue = {0};
    char *backmsg = "No message";
    char buf[BUF_SIZE] = {0, };

    LSErrorInit(&lserror);

    if(rooted){
        backmsg = "Running as root!";
    }else{
        backmsg = "Not running as root!";
    }

    jobj = jobject_create();
    jreturnValue = jboolean_create(TRUE);
    jobject_set(jobj, j_cstr_to_buffer("returnValue"), jreturnValue);
    jobject_set(jobj, j_cstr_to_buffer("rootStatus"), jboolean_create(rooted));
    jobject_set(jobj, j_cstr_to_buffer("backmsg"), jstring_create(backmsg));
    LSMessageReply(sh, message, jvalue_tostring_simple(jobj), &lserror);

    j_release(&jobj);
    PmLogInfo(logcontext, "FNCISROOT", 0,  "Luna call isRoot finished.");
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
        respobj = jobject_create();
        jobject_set(respobj, j_cstr_to_buffer("returnValue"), jboolean_create(TRUE));
        jobject_set(respobj, j_cstr_to_buffer("backmsg"), jstring_create(replyPayload));
        LSMessageReply(sh, message, jvalue_tostring_simple(respobj), lserror);
        j_release(&respobj);
}