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

#include "common.h"

pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;

//Halgal
HAL_GAL_SURFACE surfaceInfo;
HAL_GAL_RECT rect;
HAL_GAL_DRAW_FLAGS flags;
HAL_GAL_DRAW_SETTINGS settings;
uint32_t color = 0;

//Other
const VT_CALLER_T caller[24] = "org.webosbrew.piccap.cap";

VT_DRIVER *driver;
VT_CLIENTID_T client[128] = "00";

bool capture_initialized = false;
bool vtcapture_initialized = false;
bool restart = false;
int vtfrmcnt = 0;
int isrunning = 0;
int done = 0;


_LibVtCaptureProperties props;

_LibVtCapturePlaneInfo plane;
int stride, stride2, x, y, w, h, xa, ya, wa, ha;
VT_REGION_T region;
VT_REGION_T activeregion;

_LibVtCaptureBufferInfo buff;
char *addr0, *addr1;
int size0, size1;
char *gesamt;
int comsize;  
char *combined;
int rgbasize;
int rgbsize;   
char *rgbaout;   
char *rgb;
char *rgb2;
char *hal;

//All
int stride, x, y, w, h, xa, ya, wa, ha;

int done;
int ex;
int file;

int rIndex, gIndex, bIndex, aIndex;
unsigned int alpha,iAlpha;
int *nleng;

size_t len; 
char *addr;
int fd;



VT_RESOLUTION_T resolution = {360, 180};

cap_backend_config_t config = {0, 0, 0, 0};
cap_imagedata_callback_t imagedata_cb = NULL;

// Prototypes
int vtcapture_initialize();

int capture_preinit(cap_backend_config_t *backend_config, cap_imagedata_callback_t callback){
    memcpy(&config, backend_config, sizeof(cap_backend_config_t));
    imagedata_cb = callback;

    resolution.w = config.resolution_width;
    resolution.h = config.resolution_height;

    VT_DUMP_T dump = 2;
    VT_LOC_T loc = {0, 0};
    VT_BUF_T buf_cnt = 3;

    props.dump = dump;
    props.loc = loc;
    props.reg = resolution;
    props.buf_cnt = buf_cnt;
    props.frm = config.fps;
    
    //Halgal
    settings.srcblending1 = 2; //default = 2(1-10 possible) - blend? setting
    settings.dstblending2 = 0;
    settings.dstcolor = 0;

    rect.x = 0;
    rect.y = 0;
    rect.w = resolution.w;
    rect.h = resolution.h;

    flags.pflag = 0;

    return 0;
}


int capture_init()
{
    if(_nogui != 1){
        fprintf(stderr, "Init graphical capture..\n");

        if ((done = HAL_GAL_Init()) != 0) {
            fprintf(stderr, "HAL_GAL_Init failed: %x\n", done);
            return -1;
        }
        fprintf(stderr, "HAL_GAL_Init done! Exit: %d\n", done);   

        if ((done = HAL_GAL_CreateSurface(resolution.w, resolution.h, 0, &surfaceInfo)) != 0) {
            fprintf(stderr, "HAL_GAL_CreateSurface failed: %x\n", done);
            return -1;
        }
        fprintf(stderr, "HAL_GAL_CreateSurface done! SurfaceID: %d\n", surfaceInfo.vendorData);

        isrunning = 1;

        if ((done = HAL_GAL_CaptureFrameBuffer(&surfaceInfo)) != 0) {
            fprintf(stderr, "HAL_GAL_CaptureFrameBuffer failed: %x\n", done);
            return -1;
        }
        fprintf(stderr, "HAL_GAL_CaptureFrameBuffer done! %x\n", done);

        fd = open("/dev/gfx",2);
        if (fd < 0){
            fprintf(stderr, "HAL_GAL: gfx open fail result: %d\n", fd);
            return -1;

        }else{
            fprintf(stderr, "HAL_GAL: gfx open ok result: %d\n", fd);
        }

        len = surfaceInfo.property;
        if (len == 0){
            len = surfaceInfo.height * surfaceInfo.pitch;
        }

        fprintf(stderr, "Halgal done!\n");
    }

    if(_novideo != 1){
        fprintf(stderr, "Init video capture..\n");
        driver = vtCapture_create();
        fprintf(stderr, "Driver created!\n");

        done = vtcapture_initialize();
        if (done == -1){
            return -1;
        }else if (done == 17){
            vtcapture_initialized = false;
        }else if (done == 0){
            vtcapture_initialized = true;
        }
    }

    if(_novideo != 1 && _nogui != 1) //Both
    {
        comsize = size0+size1; 
        combined = (char *) malloc(comsize);

        rgbasize = sizeof(combined)*stride*h*4;
        rgbsize = sizeof(combined)*stride*h*3;   
        rgbaout = (char *) malloc(rgbasize); 


        rgb = (char *) malloc(rgbsize);
        rgb2 = (char *) malloc(len);
        gesamt = (char *) malloc(len);
        hal = (char *) malloc(len);

        stride2 = surfaceInfo.pitch/4;

        addr = (char *) mmap(0, len, 3, 1, fd, surfaceInfo.offset);
    }
    else if (_novideo != 1 && _nogui == 1) //Video only
    {
        comsize = size0+size1; 
        combined = (char *) malloc(comsize);

        rgbasize = sizeof(combined)*stride*h*4;
        rgbsize = sizeof(combined)*stride*h*3;   
        rgb = (char *) malloc(rgbsize);
        rgbaout = (char *) malloc(rgbasize);
    }
    else if (_nogui != 1 && _novideo == 1) //GUI only
    {
        stride = surfaceInfo.pitch/4;

        rgbsize = sizeof(combined)*stride*h*3;
        gesamt = (char *) malloc(len);
        rgb = (char *) malloc(len);
        hal = (char *) malloc(len);
        addr = (char *) mmap(0, len, 3, 1, fd, surfaceInfo.offset);
    }

    capture_initialized = true;
    return 0;
}


void capture_stop()
{
    fprintf(stderr, "-- Quit called! --\n");

    int done;
    if(isrunning == 1){
        if(_novideo != 1){ 
            free(combined);
        }
        if(_nogui != 1){
            munmap(addr, len);
            done = close(fd);
            if (done != 0){
                fprintf(stderr, "gfx close fail result: %d\n", done);
            }else{
                fprintf(stderr, "gfx close ok result: %d\n", done);
            }
        }
        if(_nogui != 1 && _novideo != 1){
            free(rgb2);
        }
        free(rgbaout);
        free(rgb);
        free(gesamt);
    }
    done = 0;
    if(_novideo != 1){
        done += capture_stop_vt();
    }
    if(_nogui != 1){
        done += capture_stop_hal();
    }
    return;
}

int capture_stop_hal()
{
    int done = 0;
    isrunning = 0;

    if ((done = HAL_GAL_DestroySurface(&surfaceInfo)) != 0) {
        fprintf(stderr, "Quitting: HAL_GAL_DestroySurface failed: %d\n", done);
        return done;
    }
    fprintf(stderr, "Quitting: HAL_GAL_DestroySurface done! %d\n", done);
    return done;
}

int capture_stop_vt()
{
    int done;

    isrunning = 0;
    done = vtCapture_stop(driver, client);
    if (done != 0)
    {
        fprintf(stderr, "vtCapture_stop failed: %x\nQuitting...\n", done);
        capture_terminate();
        return done;
    }
    fprintf(stderr, "vtCapture_stop done!\n");
    capture_terminate();
    return done;
}

void capture_terminate()
{
    int done;
    done = vtCapture_postprocess(driver, client);
        if (done == 0){
            fprintf(stderr, "Quitting: vtCapture_postprocess done!\n");
            done = vtCapture_finalize(driver, client);
            if (done == 0) {
                fprintf(stderr, "Quitting: vtCapture_finalize done!\n");
                vtCapture_release(driver);
                fprintf(stderr, "Quitting: Driver released!\n");
                memset(&client,0,127);
                fprintf(stderr, "Quitting!\n");
                return;
            }
            fprintf(stderr, "Quitting: vtCapture_finalize failed: %x\n", done);
        }
    vtCapture_finalize(driver, client);
    vtCapture_release(driver);
    fprintf(stderr, "Quitting with errors: %x!\n", done);
    return;
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
        fprintf(stderr, "Not initialized!\n");
        return;
    }
    pthread_mutex_lock(&frame_mutex);
    static uint32_t framecount = 0;
    static uint64_t last_ticks = 0, fps_ticks = 0;
    uint64_t ticks = getticks_us();
    last_ticks = ticks;

    if(_novideo != 1 && vtcapture_initialized){
        memcpy(combined, addr0, size0);
        memcpy(combined+size0, addr1, size1);
    }

    if(_nogui != 1){
        if ((indone = HAL_GAL_CaptureFrameBuffer(&surfaceInfo)) != 0) {
            fprintf(stderr, "HAL_GAL_CaptureFrameBuffer failed: %x\n", indone);
            capture_stop();
            return;
        }
        memcpy(hal,addr,len);
    }
    if(_novideo != 1 && _nogui != 1 && vtcapture_initialized) //Both
    {
        NV21_TO_RGBA(combined, rgbaout, stride, h);
        blend(gesamt, hal, rgbaout, len);
        remalpha(rgb, gesamt, len);
    }
    else if(_novideo != 1 && _nogui != 1 && !vtcapture_initialized){ //Both, but vt not ready
        remalpha(rgb2, hal, len);
    }
    else if (_novideo != 1 && _nogui == 1 && vtcapture_initialized) //Video only
    {
        NV21_TO_RGB24(combined, rgb, stride, h);
    }
    else if (_nogui != 1 && _novideo == 1) //GUI only
    {
        remalpha(rgb, hal, len);
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
    if (vtcapture_initialized || (_novideo == 1 && _nogui != 1)){
        if (hyperion_set_image(rgb, stride, resolution.h) != 0)
        {
            fprintf(stderr, "Write timeout\n");
            hyperion_destroy();
            app_quit = true;
        }
    } else {
        if (hyperion_set_image(rgb2, stride2, resolution.h) != 0)
        {
            fprintf(stderr, "Write timeout\n");
            hyperion_destroy();
            app_quit = true;
        }
         if (_novideo != 1 && vtfrmcnt > 200){
            vtfrmcnt = 0;
            fprintf(stderr, "Try to init vtcapture again..\n");
            if (vtcapture_initialize() == 0){
                fprintf(stderr, "Init possible. Cleanup and reset..\n");
                restart = true;
                app_quit = true;
                vtcapture_initialized = false;
            }
        }
        vtfrmcnt++;
    }
}

int blend(unsigned char *result, unsigned char *fg, unsigned char *bg, int leng){
    for (int i = 0; i < leng; i += 4){
        bIndex = i;
        gIndex = i + 1;
        rIndex = i + 2;
        aIndex = i + 3;

        alpha = fg[aIndex] + 1;
        iAlpha = 256 - fg[aIndex];
        
        result[bIndex] = (unsigned char)((alpha * fg[bIndex] + iAlpha * bg[bIndex]) >> 8);;
        result[gIndex] = (unsigned char)((alpha * fg[gIndex] + iAlpha * bg[gIndex]) >> 8);;
        result[rIndex] = (unsigned char)((alpha * fg[rIndex] + iAlpha * bg[rIndex]) >> 8);;
        result[aIndex] = 0xff;
    }
}

int remalpha(unsigned char *result, unsigned char *rgba, int leng){
    int j = 0;
    int b,g,r;
    for (int i = 0; i < leng; i += 4){
        bIndex = i;
        gIndex = i + 1;
        rIndex = i + 2;
        aIndex = i + 3;

        b = j;
        g = j+1;
        r = j+2;
        
        result[r] = rgba[bIndex];
        result[g] = rgba[gIndex];
        result[b] = rgba[rIndex];

        j+=3;
    }
}

//Credits: https://www.programmersought.com/article/18954751423/
void NV21_TO_RGBA(unsigned char *yuyv, unsigned char *rgba, int width, int height){
        const int nv_start = width * height ;
        int  index = 0, rgb_index = 0;
        uint8_t y, u, v;
        int r, g, b, nv_index = 0, i, j;
        int a = 255;
 
        for(i = 0; i < height; i++){
            for(j = 0; j < width; j ++){

                nv_index = i / 2  * width + j - j % 2;
 
                y = yuyv[rgb_index];
                u = yuyv[nv_start + nv_index ];
                v = yuyv[nv_start + nv_index + 1];
 
                r = y + (140 * (v-128))/100;  //r
                g = y - (34 * (u-128))/100 - (71 * (v-128))/100; //g
                b = y + (177 * (u-128))/100; //b
 
                if(r > 255)   r = 255;
                if(g > 255)   g = 255;
                if(b > 255)   b = 255;
                if(r < 0)     r = 0;
                if(g < 0)     g = 0;
                if(b < 0)     b = 0;
 
                index = rgb_index % width + (height - i - 1) * width;
 
                rgba[i * width * 4 + 4 * j + 0] = b;
                rgba[i * width * 4 + 4 * j + 1] = g;
                rgba[i * width * 4 + 4 * j + 2] = r;   
                rgba[i * width * 4 + 4 * j + 3] = a;               
                rgb_index++;

            }
        }
}

//Credits: https://www.programmersought.com/article/18954751423/
void NV21_TO_RGB24(unsigned char *yuyv, unsigned char *rgb, int width, int height)
{
        const int nv_start = width * height ;
        int  index = 0, rgb_index = 0;
        uint8_t y, u, v;
        int r, g, b, nv_index = 0,i, j;
 
        for(i = 0; i < height; i++){
            for(j = 0; j < width; j ++){

                nv_index = i / 2  * width + j - j % 2;
 
                y = yuyv[rgb_index];
                u = yuyv[nv_start + nv_index ];
                v = yuyv[nv_start + nv_index + 1];
 
                r = y + (140 * (v-128))/100;  //r
                g = y - (34 * (u-128))/100 - (71 * (v-128))/100; //g
                b = y + (177 * (u-128))/100; //b
 
                if(r > 255)   r = 255;
                if(g > 255)   g = 255;
                if(b > 255)   b = 255;
                if(r < 0)     r = 0;
                if(g < 0)     g = 0;
                if(b < 0)     b = 0;
 
                index = rgb_index % width + (height - i - 1) * width;

                rgb[i * width * 3 + 3 * j + 0] = r;
                rgb[i * width * 3 + 3 * j + 1] = g;
                rgb[i * width * 3 + 3 * j + 2] = b;

                rgb_index++;

            }
        }
}
