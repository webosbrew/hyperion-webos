#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengles2.h>
#include <vt/vt_openapi.h>

#include "hyperion_client.h"

SDL_Window *sdl_window;
SDL_GLContext egl;
VT_VIDEO_WINDOW_ID window_id;
VT_RESOURCE_ID resource_id;
VT_CONTEXT_ID context_id;
GLuint texture_id = 0;
GLuint offscreen_fb = 0;

GLubyte *pixels_rgba = NULL, *pixels_rgb = NULL;

bool app_quit = false;
bool capture_initialized = false;
bool vt_available = false;

VT_RESOLUTION_T resolution = {192, 108};

int capture_initialize();
void capture_terminate();
void capture_onevent(VT_EVENT_TYPE_T type, void *data, void *user_data);

int main(int argc, char *argv[])
{
    if (SDL_getenv("XDG_RUNTIME_DIR") == NULL)
    {
        SDL_setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
    }
    // Create an SDL Window
    sdl_window = SDL_CreateWindow("Capture", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920, 1080,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);

    if (sdl_window == NULL)
    {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create an EGL context. VT will use EGL so this is important
    egl = SDL_GL_CreateContext(sdl_window);
    glGenFramebuffers(1, &offscreen_fb);
    SDL_assert(offscreen_fb);

    int ret;
    if ((ret = capture_initialize()) != 0)
    {
        return ret;
    }
    pixels_rgba = (GLubyte *)calloc(resolution.w * resolution.h, 4 * sizeof(GLubyte));
    pixels_rgb = (GLubyte *)calloc(resolution.w * resolution.h, 3 * sizeof(GLubyte));
    hyperion_client("webos", "192.168.4.120", 19400, 150);
    while (!app_quit)
    {
        SDL_Event evt;
        while (SDL_PollEvent(&evt))
        {
            switch (evt.type)
            {
            case SDL_QUIT:
                app_quit = true;
                break;
            case SDL_KEYUP:
                switch (evt.key.keysym.scancode)
                {
                case SDL_WEBOS_SCANCODE_BACK:
                    app_quit = true;
                    break;
                }
            }
        }

        if (hyperion_read() < 0)
        {
            app_quit = true;
        }
    }

    hyperion_destroy();
    capture_terminate();
    glDeleteFramebuffers(1, &offscreen_fb);
    free(pixels_rgb);
    free(pixels_rgba);

    SDL_GL_DeleteContext(egl);
    SDL_DestroyWindow(sdl_window);
    return 0;
}

int capture_initialize()
{
    window_id = VT_CreateVideoWindow(0);
    if (window_id == -1)
    {
        fprintf(stderr, "[Capture Sample] VT_CreateVideoWindow Failed\n");
        return -1;
    }
    fprintf(stderr, "[Capture Sample] window_id=%d\n", window_id);

    if (VT_AcquireVideoWindowResource(window_id, &resource_id) != VT_OK)
    {
        fprintf(stderr, "[Capture Sample] VT_AcquireVideoWindowResource Failed\n");
        return -1;
    }
    fprintf(stderr, "[Capture Sample] resource_id=%d\n", resource_id);

    context_id = VT_CreateContext(resource_id, 2);
    if (context_id == -1)
    {
        fprintf(stderr, "[Capture Sample] VT_CreateContext Failed\n");
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }
    fprintf(stderr, "[Capture Sample] context_id=%d\n", context_id);

    VT_SetTextureResolution(context_id, &resolution);
    // VT_GetTextureResolution(context_id, &resolution);

    if (VT_SetTextureSourceRegion(context_id, VT_SOURCE_REGION_MAX) != VT_OK)
    {
        fprintf(stderr, "[Capture Sample] VT_SetTextureSourceRegion Failed\n");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }

    if (VT_RegisterEventHandler(context_id, &capture_onevent, NULL) != VT_OK)
    {
        fprintf(stderr, "[Capture Sample] VT_RegisterEventHandler Failed\n");
        VT_DeleteContext(context_id);
        VT_ReleaseVideoWindowResource(resource_id);
        return -1;
    }
    capture_initialized = true;
    return 0;
}

void capture_terminate()
{
    capture_initialized = false;

    if (texture_id != 0 && glIsTexture(texture_id))
    {
        VT_DeleteTexture(context_id, texture_id);
    }

    if (VT_UnRegisterEventHandler(context_id) != VT_OK)
    {
        fprintf(stderr, "[Capture Sample] VT_UnRegisterEventHandler error!\n");
    }
    VT_DeleteContext(context_id);
    VT_ReleaseVideoWindowResource(resource_id);
}

void capture_acquire()
{
    static Uint32 fps_ticks = 0, last_send_ticks = 0, framecount = 0;
    VT_OUTPUT_INFO_T output_info;
    if (vt_available)
    {
        if (capture_initialized)
        {
            if (last_send_ticks != 0 && (SDL_GetTicks() - last_send_ticks) < 30)
            {
                // Cap frame to 30fps
                return;
            }
            if (texture_id != 0 && glIsTexture(texture_id))
            {
                VT_DeleteTexture(context_id, texture_id);
            }

            VT_STATUS_T vtStatus = VT_GenerateTexture(resource_id, context_id, &texture_id, &output_info);
            if (vtStatus == VT_OK)
            {
                int width = resolution.w, height = resolution.h;

                glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fb);

                glBindTexture(GL_TEXTURE_2D, texture_id);

                //Bind the texture to your FBO
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0);

                //Test if everything failed
                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE)
                {
                    fprintf(stderr, "failed to make complete framebuffer object %x", status);
                }

                glViewport(0, 0, width, height);
                if (pixels_rgba)
                {
                    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
                    for (int y = 0; y < height; y++)
                    {
                        for (int x = 0; x < width; x++)
                        {
                            int i1 = (y * width) + x, i2 = ((height - y - 1) * width) + x;
                            pixels_rgb[i1 * 3 + 0] = pixels_rgba[i2 * 4 + 0];
                            pixels_rgb[i1 * 3 + 1] = pixels_rgba[i2 * 4 + 1];
                            pixels_rgb[i1 * 3 + 2] = pixels_rgba[i2 * 4 + 2];
                        }
                    }
                    hyperion_set_image(pixels_rgb, width, height);
                    last_send_ticks = SDL_GetTicks();
                }

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            else
            {
                texture_id = 0;
            }
        }
        vt_available = false;
    }

    Uint32 end_ticks = SDL_GetTicks();
    if ((end_ticks - fps_ticks) >= 1000)
    {
        fprintf(stderr, "capture speed %d FPS\n", (int)(framecount * 1000.0 / (end_ticks - fps_ticks)));
        fps_ticks = end_ticks;
        framecount = 0;
    }
    else
    {
        framecount++;
    }
}

void capture_onevent(VT_EVENT_TYPE_T type, void *data, void *user_data)
{
    switch (type)
    {
    case VT_AVAILABLE:
        vt_available = true;
        capture_acquire();
        break;
    case VT_UNAVAILABLE:
        fprintf(stderr, "VT_UNAVAILABLE received\n");
        break;
    case VT_RESOURCE_BUSY:
        fprintf(stderr, "VT_RESOURCE_BUSY received\n");
        break;
    default:
        fprintf(stderr, "UNKNOWN event received\n");
        break;
    }
}
