#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <vtcapture/vtCaptureApi_c.h>
#include <halgal.h>
#include <libyuv.h>

#include "common.h"
#include "log.h"

pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t capture_thread;

//Halgal
HAL_GAL_SURFACE surfaceInfo;
HAL_GAL_RECT rect;
HAL_GAL_DRAW_FLAGS flags;
HAL_GAL_DRAW_SETTINGS settings;
uint32_t color = 0;

//Other
const char *caller = "hyperion-webos_service";

VT_DRIVER *driver;
char client[128] = "00";

bool capture_initialized = false;
bool vtcapture_initialized = false;
bool restart = false;
bool capture_run = false;
int vtfrmcnt = 0;
int isrunning = 0;
int done = 0;


_LibVtCaptureProperties props;

_LibVtCapturePlaneInfo plane;
VT_REGION_T region;
VT_REGION_T activeregion;

_LibVtCaptureBufferInfo buff;
char *addr0, *addr1;
int size0, size1;
char *videoY;
char *videoUV;
char *videoARGB;
char *guiABGR;
char *guiARGB;
char *outARGB;
char *outRGB;

//All
int stride, x, y, w, h, xa, ya, wa, ha;

size_t len; 
char *addr;
int fd;

VT_RESOLUTION_T resolution = {360, 180};

cap_backend_config_t config = {0, 0, 0, 0};
cap_imagedata_callback_t imagedata_cb = NULL;

// Prototypes
int vtcapture_initialize();
int capture_start();
int capture_stop_hal();
int capture_stop_vt();
int capture_cleanup();
uint64_t getticks_us();
void capture_frame();
void send_picture();
void *capture_thread_target(void *data);

int capture_preinit(cap_backend_config_t *backend_config, cap_imagedata_callback_t callback){
    INFO("Preinit called. Copying config..");
    memcpy(&config, backend_config, sizeof(cap_backend_config_t));
    imagedata_cb = callback;

    resolution.w = config.resolution_width;
    resolution.h = config.resolution_height;

    INFO("Copying config done. Initialize vars..");
    VT_DUMP_T dump = 2;
    VT_LOC_T loc = {0, 0};
    VT_BUF_T buf_cnt = 3;

    props.dump = dump;
    props.loc = loc;
    props.reg = resolution;
    props.buf_cnt = buf_cnt;
    if(config.fps == 0){
        props.frm = 120;
    }else{
        props.frm = config.fps;
    }
    
    //Halgal
    settings.srcblending1 = 2; //default = 2(1-10 possible) - blend? setting
    settings.dstblending2 = 0;
    settings.dstcolor = 0;

    rect.x = 0;
    rect.y = 0;
    rect.w = resolution.w;
    rect.h = resolution.h;

    flags.pflag = 0;
    INFO("Init finished.");
    return 0;
}


int capture_init()
{
    int rgbasize;
    int rgbsize;

    INFO("Initialization of capture devices..");
    if(config.no_gui != 1){
        INFO("Graphical capture enabled. Begin init..");
        if ((done = HAL_GAL_Init()) != 0) {
            ERR("HAL_GAL_Init failed: %x", done);
            return -1;
        }
        INFO("HAL_GAL_Init done! Exit: %d", done);   

        if ((done = HAL_GAL_CreateSurface(resolution.w, resolution.h, 0, &surfaceInfo)) != 0) {
            ERR("HAL_GAL_CreateSurface failed: %x", done);
            return -1;
        }
        INFO("HAL_GAL_CreateSurface done! SurfaceID: %d", surfaceInfo.vendorData);

        isrunning = 1;

        if ((done = HAL_GAL_CaptureFrameBuffer(&surfaceInfo)) != 0) {
            ERR("HAL_GAL_CaptureFrameBuffer failed: %x", done);
            return -1;
        }
        INFO("HAL_GAL_CaptureFrameBuffer done! %x", done);

        fd = open("/dev/gfx",2);
        if (fd < 0){
            ERR("HAL_GAL: gfx open fail result: %d", fd);
            return -1;

        }else{
            INFO("HAL_GAL: gfx open ok result: %d", fd);
        }

        len = surfaceInfo.property;
        if (len == 0){
            len = surfaceInfo.height * surfaceInfo.pitch;
        }

        INFO("Halgal done!");
    }

    if(config.no_video != 1){
        INFO("Init video capture..");
        driver = vtCapture_create();
        INFO("Driver created!");

        done = vtcapture_initialize();
        if (done == -1){
            ERR("vtcapture_initialize failed!");
            return -1;
        }else if (done == 11){
            ERR("vtcapture_initialize failed! No capture permissions!");
            return 11;
        }else if (done == 17){
            vtcapture_initialized = false;
            INFO("vtcapture not ready yet!");
        }else if (done == 0){
            vtcapture_initialized = true;
            INFO("vtcapture initialized!");
        } 
    }

    INFO("Malloc vars..");
    if(config.no_video != 1 && config.no_gui != 1) //Both
    {
        videoY = (char *) malloc(size0);
        videoUV = (char *) malloc(size1);

        rgbasize = sizeof(char) * stride * h * 4;
        rgbsize = sizeof(char) * stride * h * 3;

        videoARGB = (char *) malloc(rgbasize); 
        guiABGR = (char *) malloc(len);
        guiARGB = (char *) malloc(len);
        outARGB = (char *) malloc(len);
        outRGB = (char *) malloc(rgbsize);

        stride = surfaceInfo.pitch/4;

        addr = (char *) mmap(0, len, 3, 1, fd, surfaceInfo.offset);
    }
    else if (config.no_video != 1 && config.no_gui == 1) //Video only
    {
        videoY = (char *) malloc(size0);
        videoUV = (char *) malloc(size1);

        rgbasize = sizeof(char)*stride*h*4;
        rgbsize = sizeof(char)*stride*h*3;

        videoARGB = (char *) malloc(rgbasize); 
        outRGB = (char *) malloc(rgbsize);
    }
    else if (config.no_gui != 1 && config.no_video == 1) //GUI only
    {
        stride = surfaceInfo.pitch/4;
        rgbsize = sizeof(char) * stride * h * 3;

        guiABGR = (char *) malloc(len);
        guiARGB = (char *) malloc(len);
        outARGB = (char *) malloc(len);
        outRGB = (char *) malloc(len);

        addr = (char *) mmap(0, len, 3, 1, fd, surfaceInfo.offset);
    }

    INFO("Malloc vars finished.");
    capture_initialized = true;
    return 0;
}

int vtcapture_initialize(){
    INFO("Starting vtcapture initialization.");
    int innerdone = 0;
    innerdone = vtCapture_init(driver, caller, client);
        if (innerdone == 17) {
            ERR("vtCapture_init not ready yet return: %d", innerdone);
            return 17;
        }else if (innerdone == 11){
            ERR("vtCapture_init failed: %d Permission denied! Quitting...", innerdone);
            return 11;
        }else if (innerdone != 0){
            ERR("vtCapture_init failed: %d Quitting...", innerdone);
            return -1;
        }
        INFO("vtCapture_init done! Caller_ID: %s Client ID: %s", caller, client);

        innerdone = vtCapture_preprocess(driver, client, &props);
        if (innerdone != 0) {
            ERR("vtCapture_preprocess failed: %x Quitting...", innerdone);
            return -1;
        }
        INFO("vtCapture_preprocess done!");

        innerdone = vtCapture_planeInfo(driver, client, &plane);
        if (innerdone == 0 ) {
            stride = plane.stride;

            region = plane.planeregion;
            x = region.a, y = region.b, w = region.c, h = region.d;

            activeregion = plane.activeregion;
            xa = activeregion.a, ya = activeregion.b, wa = activeregion.c, ha = activeregion.d;
        }else{
            ERR("vtCapture_planeInfo failed: %xQuitting...", innerdone);
            return -1;
        }
        INFO("vtCapture_planeInfo done! stride: %d Region: x: %d, y: %d, w: %d, h: %d Active Region: x: %d, y: %d w: %d h: %d", stride, x, y, w, h, xa, ya, wa, ha);

        innerdone = vtCapture_process(driver, client);
        if (innerdone == 0){
            isrunning = 1;
            capture_initialized = true;
        }else{
            isrunning = 0;
            ERR("vtCapture_process failed: %xQuitting...", innerdone);
            return -1;
        }
        INFO("vtCapture_process done!");

        int cnter = 0;
        do{
            sleep(1/1000*100);
            innerdone = vtCapture_currentCaptureBuffInfo(driver, &buff);
            if (innerdone == 0 ) {
                addr0 = buff.start_addr0;
                addr1 = buff.start_addr1;
                size0 = buff.size0;
                size1 = buff.size1;
            }else if (innerdone != 2){
                ERR("vtCapture_currentCaptureBuffInfo failed: %x Quitting...", innerdone);
                capture_terminate();
                return -1;
            }
            cnter++;
        }while(innerdone != 0);
        INFO("vtCapture_currentCaptureBuffInfo done after %d tries! addr0: %p addr1: %p size0: %d size1: %d", cnter, addr0, addr1, size0, size1);

        INFO("vtcapture initialization finished.");
        return 0;
}

int capture_start(){
    INFO("Starting capture thread..");
    capture_run = true;
    if (pthread_create(&capture_thread, NULL, capture_thread_target, NULL) != 0) {
        return -1;
    }
    return 0;
}

int capture_cleanup()
{
    INFO("Capture cleanup...");

    int done;
    if(isrunning == 1){
        INFO("Capture was running, freeing vars...");
        if(config.no_video != 1){ 
            INFO("Freeing video vars...");
            free(videoY);
            free(videoUV);
        }
        if(config.no_gui != 1){
            INFO("Freeing gui vars...");
            munmap(addr, len);
            done = close(fd);
            if (done != 0){
                ERR("gfx close fail result: %d", done);
            }else{
                INFO("gfx close ok result: %d", done);
            }
        }

        INFO("Freeing video combination vars...");
        free(videoARGB);
        free(guiABGR);
        free(guiARGB);
        free(outARGB);
        free(outRGB);
    }
    done = 0;
    INFO("Finished capture cleanup..");
    return done;
}

int capture_stop_hal()
{
    int done = 0;
    isrunning = 0;
    INFO("Stopping HAL capture...");
    if ((done = HAL_GAL_DestroySurface(&surfaceInfo)) != 0) {
        ERR("HAL_GAL_DestroySurface failed: %d", done);
        return done;
    }
    INFO("HAL_GAL_DestroySurface done. Result: %d", done);
    return done;
}

int capture_stop_vt()
{
    int done;
    isrunning = 0;
    INFO("Stopping VT capture...");
    done = vtCapture_stop(driver, client);
    if (done != 0)
    {
        ERR("vtCapture_stop failed: %x", done);
        return done;
    }
    INFO("vtCapture_stop done!");
    return done;
}

int capture_terminate()
{
    int done;

    INFO("Called termination of vtcapture");

    capture_run = false;
    pthread_join(capture_thread, NULL);

    if(config.no_video != 1 && vtcapture_initialized){
        INFO("Video capture enabled - Also stopping..");
        done += capture_stop_vt();
    }
    if(config.no_gui != 1){
        INFO("GUI capture enabled - Also stopping..");
        done += capture_stop_hal();
    }

    if(config.no_video != 1){
        if (vtcapture_initialized) {
            done = vtCapture_postprocess(driver, client);
            if (done == 0){
                INFO("vtCapture_postprocess done!");
                done = vtCapture_finalize(driver, client);
                if (done == 0) {
                    INFO("vtCapture_finalize done!");
                    vtCapture_release(driver);
                    INFO("Driver released!");
                    memset(&client,0,127);
                    INFO("Finished capture termination!");
                    return 0;
                }
                ERR("vtCapture_finalize failed: %x", done);
            }
            vtCapture_finalize(driver, client);
        }
        vtCapture_release(driver);
    }
    ERR("Finishing with errors: %x!", done);
    return -1;
}

uint64_t getticks_us()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

void capture_frame()
{
    int indone = 0;
    if (!capture_initialized){
        ERR("Capture devices not initialized yet!");
        return;
    }
    pthread_mutex_lock(&frame_mutex);
    static uint32_t framecount = 0;
    static uint64_t last_ticks = 0, fps_ticks = 0;
    uint64_t ticks = getticks_us();
    last_ticks = ticks;

    if(config.no_video != 1 && vtcapture_initialized){
        memcpy(videoY, addr0, size0);
        memcpy(videoUV, addr1, size1);
    }

    if(config.no_gui != 1){
        if ((indone = HAL_GAL_CaptureFrameBuffer(&surfaceInfo)) != 0) {
            ERR("HAL_GAL_CaptureFrameBuffer failed: %x", indone);
            capture_terminate();
            return;
        }
        memcpy(guiABGR,addr,len);
    }
    if(config.no_video != 1 && config.no_gui != 1 && vtcapture_initialized) //Both
    {
        // YUV -> ARGB
        // NV21ToARGB(videoY, stride, videoUV, stride, videoARGB, w * 4, w, h);
        NV21ToARGBMatrix(videoY, stride, videoUV, stride, videoARGB, w * 4, &kYuvH709Constants, w, h);
        // ABGR -> ARGB
        ABGRToARGB(guiABGR, w * 4, guiARGB, w * 4, w, h);
        // blend video and gui
        ARGBBlend(guiARGB, w * 4, videoARGB, w * 4, outARGB, w * 4, w, h);
        // remove alpha channel
        ARGBToRGB24(outARGB, w * 4, outRGB, w * 3, w, h);
    }
    else if(config.no_video != 1 && config.no_gui != 1 && !vtcapture_initialized) //Both, but vt not ready
    {
        // ABGR -> ARGB
        ABGRToARGB(guiABGR, w * 4, guiARGB, w * 4, w, h);
        // remove alpha channel
        ARGBToRGB24(guiARGB, w * 4, outRGB, w * 3, w, h);
    }
    else if (config.no_video != 1 && config.no_gui == 1 && vtcapture_initialized) //Video only
    {
        // YUV -> RGB
        // NV21ToRGB24(videoY, stride, videoUV, stride, outRGB, w * 3, w, h);
        NV21ToRGB24Matrix(videoY, stride, videoUV, stride, outRGB, w * 3, &kYuvH709Constants, w, h);
    }
    else if (config.no_gui != 1 && config.no_video == 1) //GUI only
    {
        // ABGR -> ARGB
        ABGRToARGB(guiABGR, surfaceInfo.width * 4, guiARGB, surfaceInfo.width * 4, surfaceInfo.width, surfaceInfo.height);
        // remove alpha channel
        ARGBToRGB24(guiARGB, surfaceInfo.width * 4, outRGB, surfaceInfo.width * 3, surfaceInfo.width, surfaceInfo.height);
    }
    send_picture();

    framecount++;
    if (fps_ticks == 0)
    {
        fps_ticks = ticks;
    }
    else if (ticks - fps_ticks >= 1000000)
    {
//            printf("[Stat] Send framerate: %d\n",
//                framecount);
        framecount = 0;
        fps_ticks = ticks;
    }
    pthread_mutex_unlock(&frame_mutex);
}

void send_picture()
{
//    fprintf(stderr, "[Client] hyperion_set_image\n");
    if (vtcapture_initialized || (config.no_video == 1 && config.no_gui != 1)){
        imagedata_cb(stride, resolution.h, outRGB); //GUI /GUI+VT /VT
    } else {
        imagedata_cb(stride, resolution.h, outRGB); //GUI+VT_notReady

        if (config.no_video != 1 && vtfrmcnt > 200){
            vtfrmcnt = 0;
            INFO("Try to init vtcapture again..");
            if (vtcapture_initialize() == 0){
                vtcapture_initialized = true;
                INFO("Init possible. Terminating current capture..");
                capture_terminate();
                INFO("Init possible. Cleanup current capture..");
                capture_cleanup();
                INFO("Init possible. Init capture..");
                capture_init();
                INFO("Init possible. Starting capture again..");
                capture_start();
                return;
            }
        }
        vtfrmcnt++;
    }
}

void* capture_thread_target(void* data) {
    while (capture_run) {
        capture_frame();
    }
}
