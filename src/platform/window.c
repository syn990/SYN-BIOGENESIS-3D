/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. It is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License (LICENSE) for more details.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "platform/window.h"

#include <epoxy/gl.h>
#include <stdio.h>
#include <stdlib.h>

/* On Windows there is no terminal, so a driver that
   can't give OpenGL 3.3 must be said in a box or the program just
   vanishes. Elsewhere stderr (a box would hang headless harnesses). */
static void window_fail(const char *what, const char *hint)
{
    const char *why = SDL_GetError();
    fprintf(stderr, "%s failed: %s\n", what, why);
#ifdef _WIN32
    char msg[1024];
    snprintf(msg, sizeof msg, "%s failed:\n%s%s%s", what, why, hint ? "\n\n" : "",
             hint ? hint : "");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SYN-BIOGENESIS-3D could not start", msg, NULL);
#else
    (void)hint;
#endif
}

bool platform_window_create(PlatformWindow *pw, const char *title, int width, int height)
{
    static const char *const GL33_HINT =
        "SYN-BIOGENESIS-3D needs OpenGL 3.3 or newer.\n\n"
        "Install the graphics driver from your card's maker\n"
        "(NVIDIA, AMD or Intel). In a virtual machine,\n"
        "3D acceleration must be on.\n\n"
        "The world also runs without a window: --headless.";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        window_fail("Starting the video system", NULL);
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    pw->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                  height,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                                      SDL_WINDOW_ALLOW_HIGHDPI);
    if (!pw->window) {
        window_fail("Opening the window", GL33_HINT);
        SDL_Quit();
        return false;
    }
    pw->gl_context = SDL_GL_CreateContext(pw->window);
    if (!pw->gl_context) {
        window_fail("Creating an OpenGL 3.3 context", GL33_HINT);
        SDL_DestroyWindow(pw->window);
        SDL_Quit();
        return false;
    }
    /* Asking for 3.3 is not getting it: Windows' fallback renderer hands
       back OpenGL 1.1 rather than failing. Check what was given. */
    int glv = epoxy_gl_version();
    if (glv < 33) {
        SDL_SetError("the graphics driver offered only OpenGL %d.%d (%s)", glv / 10, glv % 10,
                     (const char *)glGetString(GL_RENDERER));
        window_fail("Creating an OpenGL 3.3 context", GL33_HINT);
        SDL_GL_DeleteContext(pw->gl_context);
        SDL_DestroyWindow(pw->window);
        SDL_Quit();
        return false;
    }
    /* adaptive vsync is unreliable under Wayland compositors (labwc):
       plain vsync, or uncapped */
    if (SDL_GL_SetSwapInterval(1) != 0)
        SDL_GL_SetSwapInterval(0);
    fprintf(stderr, "OpenGL %s -- %s\n", (const char *)glGetString(GL_VERSION),
            (const char *)glGetString(GL_RENDERER));
    glEnable(GL_DEPTH_TEST);
    platform_window_refresh_size(pw);
    return true;
}

void platform_window_destroy(PlatformWindow *pw)
{
    SDL_GL_DeleteContext(pw->gl_context);
    SDL_DestroyWindow(pw->window);
    SDL_Quit();
}

bool platform_window_refresh_size(PlatformWindow *pw)
{
    int w, h;
    SDL_GL_GetDrawableSize(pw->window, &w, &h);
    bool changed = w != pw->width || h != pw->height;
    pw->width = w;
    pw->height = h;
    return changed;
}

void platform_window_swap(PlatformWindow *pw)
{
    SDL_GL_SwapWindow(pw->window);
}

bool platform_window_shot(PlatformWindow *pw, const char *path)
{
    int w = pw->width, h = pw->height;
    unsigned char *px = malloc((size_t)w * h * 3);
    if (!px)
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(px);
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) /* GL reads bottom-up */
        fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    free(px);
    return true;
}
