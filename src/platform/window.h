/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Taken from SYN-TREK's src/platform/window.h.
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

#ifndef PLATFORM_WINDOW_H
#define PLATFORM_WINDOW_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    SDL_Window *window;
    SDL_GLContext gl_context;
    int width, height; /* drawable size in pixels */
} PlatformWindow;

/* The window and a core OpenGL 3.3 context. False on failure, with what
   failed and what to do about it already said (stderr; a message box on
   Windows, which has no terminal). */
bool platform_window_create(PlatformWindow *pw, const char *title, int width, int height);
void platform_window_destroy(PlatformWindow *pw);
/* Re-reads the drawable size; true if it changed. */
bool platform_window_refresh_size(PlatformWindow *pw);
void platform_window_swap(PlatformWindow *pw);
/* The finished backbuffer as a binary PPM. Call before swapping. */
bool platform_window_shot(PlatformWindow *pw, const char *path);

#endif
