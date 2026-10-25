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

/* The interface's input, immediate-mode: each frame the shell asks whether
 * the pointer is over a rectangle and whether it was clicked there. What
 * the shell drew last frame is solid: a press that lands on it belongs to
 * the interface, and the window's camera never sees it. Coordinates are
 * drawable pixels, as the interface is drawn. */
#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "ui/skin.h"

/* Before the frame's events; scale = drawable pixels per window point. */
void uiin_begin_frame(float scale);
/* Every event. True if the interface takes it (a press on something it
 * drew, typing into the console): the camera should leave it alone. */
bool uiin_event(const SDL_Event *e);

/* While drawing: */
/* Blocked, nothing drawn is under the pointer or clicked (what lies under
 * an open menu). */
void uiin_block(bool on);
bool uiin_over(UIRect r);
bool uiin_clicked(UIRect r);           /* the left button let go over r, pressed over r */
bool uiin_right_clicked(UIRect r);
bool uiin_clicked_outside(UIRect r);  /* the left button let go, pressed outside r */
/* button b (0 left, 1 right) went down over r this frame; is held now */
bool uiin_pressed(UIRect r, int b);
bool uiin_mouse_down(int b);
void uiin_solid(UIRect r);             /* r is part of the interface this frame */
void uiin_tooltip(const char *s);      /* shown by uiin_draw_tooltip at the end */
void uiin_draw_tooltip(const Fonts *f, int w, int h);
void uiin_mouse(float *x, float *y);
/* The wheel this frame (up positive), when it turned over the interface. */
float uiin_wheel(void);

/* Keys pressed this frame, and typed text, for the console. */
bool uiin_key(SDL_Keycode k);
SDL_Keymod uiin_mods(void);
const char *uiin_typed(void);
/* Text typed by a script (SYN3D_SCRIPT): taken as SDL's would be. */
void uiin_inject_text(const char *s);
/* The console has the keyboard: keys go to it, not the window. */
void uiin_set_typing(bool on);
bool uiin_typing(void);

#endif
