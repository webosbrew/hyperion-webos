#include <assert.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <vt/vt_openapi.h>

#include "common.h"
#include "gl_debug.h"
#include "log.h"

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;

VT_RESOURCE_ID resource_id;
VT_CONTEXT_ID context_id;
GLuint texture_id = 0;
GLuint offscreen_fb = 0;

GLubyte *pixels_rgba = NULL, *pixels_rgb = NULL;

bool capture_initialized = false;
bool vt_available = false;

cap_backend_config_t config = { 0, 0, 0, 0 };
cap_imagedata_callback_t imagedata_cb = NULL;
VT_RESOLUTION_T resolution = { 192, 108 };

void capture_onevent(VT_EVENT_TYPE_T type, void* data, void* user_data);
void read_picture();

void egl_init()
{
    // 1. Initialize egl
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_display);
    EGLint major, minor;

    eglInitialize(egl_display, &major, &minor);
    assert(eglGetError() == EGL_SUCCESS);
    INFO("[EGL] Display, major = %d, minor = %d", major, minor);

    // 2. Select an appropriate configuration
    EGLint numConfigs;
    EGLConfig eglCfg;

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    eglChooseConfig(egl_display, configAttribs, &eglCfg, 1, &numConfigs);
    assert(eglGetError() == EGL_SUCCESS);

    // 3. Create a surface

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, resolution.w,
        EGL_HEIGHT, resolution.h,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_LARGEST_PBUFFER, EGL_TRUE,
        EGL_NONE
    };
    egl_surface = eglCreatePbufferSurface(egl_display, eglCfg, pbufferAttribs);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_surface);

    // 4. Bind the API
    eglBindAPI(EGL_OPENGL_ES_API);
    assert(eglGetError() == EGL_SUCCESS);

    // 5. Create a context and make it current

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, eglCfg, EGL_NO_CONTEXT, contextAttribs);
    assert(eglGetError() == EGL_SUCCESS);
    assert(egl_context);

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    assert(eglGetError() == EGL_SUCCESS);

    EGLint suf_width, suf_height;
    eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &suf_width);
    eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &suf_height);
    assert(eglGetError() == EGL_SUCCESS);
    INFO("[EGL] Surface size: %dx%d", suf_width, suf_height);

    // Create framebuffer for offscreen rendering
    GL_CHECK(glGenFramebuffers(1, &offscreen_fb));
    GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fb));

    INFO("[EGL] init complete");
}

void egl_cleanup()
{
    glDeleteFramebuffers(1, &offscreen_fb);
    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
    free(pixels_rgb);
    free(pixels_rgba);
}

int capture_preinit(cap_backend_config_t* backend_config, cap_imagedata_callback_t callback)
{
    INFO("Preinit called. Copying config..");
    memcpy(&config, backend_config, sizeof(cap_backend_config_t));
    imagedata_cb = callback;

    resolution.w = config.resolution_width;
    resolution.h = config.resolution_height;

    egl_init();
    return 0;
}

int capture_init()
{
    int32_t supported = 0;
    if (VT_IsSystemSupported(&supported) != VT_OK || !supported) {
        ERR("[VT] VT_IsSystemSupported Failed. This TV doesn't support VT.");
        return -1;
    }

    INFO("[VT] VT_CreateVideoWindow");
    VT_VIDEO_WINDOW_ID window_id = VT_CreateVideoWindow(0);
    if (window_id == -1) {
        ERR("[VT] VT_CreateVideoWindow Failed");
        return -1;
    }
    INFO("[VT] window_id=%d", window_id);

    INFO("[VT] VT_AcquireVideoWindowResource");
    if (VT_AcquireVideoWindowResource(window_id, &resource_id) != VT_OK) {
        ERR("[VT] VT_AcquireVideoWindowResource Failed");
        return -1;
    }
    INFO("[VT] resource_id=%d", resource_id);

    INFO("[VT] VT_CreateContext");
    context_id = VT_CreateContext(resource_id, 2);
    if (!context_id || context_id == -1) {
        ERR("[VT] VT_CreateContext Failed");
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }
    INFO("[VT] context_id=%d", context_id);

    INFO("[VT] VT_SetTextureResolution");
    VT_SetTextureResolution(context_id, &resolution);
    // VT_GetTextureResolution(context_id, &resolution);

    INFO("[VT] VT_SetTextureSourceRegion");
    if (VT_SetTextureSourceRegion(context_id, VT_SOURCE_REGION_MAX) != VT_OK) {
        ERR("[VT] VT_SetTextureSourceRegion Failed");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    INFO("[VT] VT_SetTextureSourceLocation");
    if (VT_SetTextureSourceLocation(context_id, VT_SOURCE_LOCATION_DISPLAY) != VT_OK) {
        ERR("[VT] VT_SetTextureSourceLocation Failed");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    INFO("[VT] VT_RegisterEventHandler");
    if (VT_RegisterEventHandler(context_id, &capture_onevent, NULL) != VT_OK) {
        ERR("[VT] VT_RegisterEventHandler Failed");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }
    capture_initialized = true;

    pixels_rgba = (GLubyte*)calloc(resolution.w * resolution.h, 4 * sizeof(GLubyte));
    pixels_rgb = (GLubyte*)calloc(resolution.w * resolution.h, 3 * sizeof(GLubyte));

    return 0;
}

int capture_start()
{
    return 0;
}

int capture_terminate()
{
    if (!capture_initialized)
        return -1;

    capture_initialized = false;

    if (texture_id != 0 && glIsTexture(texture_id)) {
        VT_DeleteTexture(context_id, texture_id);
    }

    INFO("[VT] VT_UnRegisterEventHandler");
    if (VT_UnRegisterEventHandler(context_id) != VT_OK) {
        ERR("[VT] VT_UnRegisterEventHandler error!");
    }
    INFO("[VT] VT_DeleteContext");
    VT_DeleteContext(context_id);
    INFO("[VT] VT_ReleaseVideoWindowResource");
    VT_ReleaseVideoWindowResource(resource_id);
    return 0;
}

int capture_cleanup()
{
    egl_cleanup();
    return 0;
}

uint64_t getticks_us()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

void capture_frame()
{
    if (!capture_initialized)
        return;
    pthread_mutex_lock(&frame_mutex);
    static uint32_t framecount = 0;
    static uint64_t last_ticks = 0, fps_ticks = 0, dur_gentexture = 0, dur_readframe = 0, dur_sendframe = 0;
    uint64_t ticks = getticks_us(), trace_start, trace_end;
    if (ticks - last_ticks < config.framedelay_us) {
        pthread_mutex_unlock(&frame_mutex);
        return;
    }
    last_ticks = ticks;
    VT_OUTPUT_INFO_T output_info;
    if (vt_available) {
        if (texture_id != 0 && glIsTexture(texture_id)) {
            VT_DeleteTexture(context_id, texture_id);
        }

        trace_start = getticks_us();
        VT_STATUS_T vtStatus = VT_GenerateTexture(resource_id, context_id, &texture_id, &output_info);
        trace_end = getticks_us();
        dur_gentexture += trace_end - trace_start;
        trace_start = trace_end;
        if (vtStatus == VT_OK) {
            read_picture();
            trace_end = getticks_us();
            dur_readframe += trace_end - trace_start;
            trace_start = trace_end;
            imagedata_cb(config.resolution_width, config.resolution_height, pixels_rgb);
            trace_end = getticks_us();
            dur_sendframe += trace_end - trace_start;
            trace_start = trace_end;
            framecount++;
        } else {
            ERR("VT_GenerateTexture failed");
            texture_id = 0;
        }
        vt_available = false;
    }
    if (fps_ticks == 0) {
        fps_ticks = ticks;
    } else if (ticks - fps_ticks >= 1000000) {
        INFO("[Stat] Send framerate: %d FPS. gen %d us, read %d us, send %d us",
            framecount, dur_gentexture, dur_readframe, dur_sendframe);
        framecount = 0;
        dur_gentexture = 0;
        dur_readframe = 0;
        dur_sendframe = 0;
        fps_ticks = ticks;
    }
    pthread_mutex_unlock(&frame_mutex);
}

void capture_onevent(VT_EVENT_TYPE_T type, void* data, void* user_data)
{
    switch (type) {
    case VT_AVAILABLE:
        vt_available = true;
        capture_frame();
        break;
    case VT_UNAVAILABLE:
        ERR("VT_UNAVAILABLE received");
        break;
    case VT_RESOURCE_BUSY:
        ERR("VT_RESOURCE_BUSY received");
        break;
    default:
        ERR("UNKNOWN event received");
        break;
    }
}

void read_picture()
{
    int width = resolution.w, height = resolution.h;

    glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fb);

    glBindTexture(GL_TEXTURE_2D, texture_id);

    // Bind the texture to your FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);

    // Test if everything failed
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ERR("failed to make complete framebuffer object %x", status);
    }

    glViewport(0, 0, width, height);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int i1 = (y * width) + x, i2 = ((height - y - 1) * width) + x;
            pixels_rgb[i1 * 3 + 0] = pixels_rgba[i2 * 4 + 0];
            pixels_rgb[i1 * 3 + 1] = pixels_rgba[i2 * 4 + 1];
            pixels_rgb[i1 * 3 + 2] = pixels_rgba[i2 * 4 + 2];
        }
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
