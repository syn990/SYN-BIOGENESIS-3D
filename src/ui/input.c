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

#include "ui/input.h"

#include <stdio.h>
#include <string.h>

#define SOLIDS 64
#define KEYS 32

static struct {
    float scale;
    float mx, my;
    /* where the left/right button went down this press, and whether the
     * press is the interface's */
    float down_x[2], down_y[2];
    bool down[2], ours[2];
    bool released[2];
    bool pressed[2]; /* went down this frame */
    /* what was drawn last frame (decides presses) and this frame */
    UIRect solid[2][SOLIDS];
    int nsolid[2];
    int cur;
    SDL_Keycode keys[KEYS];
    int nkeys;
    SDL_Keymod mods;
    char typed[64];
    bool typing;
    char tip[160];
    float wheel;
    bool blocked;
} I = {.scale = 1};

void uiin_begin_frame(float scale)
{
    I.scale = scale > 0 ? scale : 1;
    I.released[0] = I.released[1] = false;
    I.pressed[0] = I.pressed[1] = false;
    I.nkeys = 0;
    I.typed[0] = 0;
    I.tip[0] = 0;
    I.wheel = 0;
    I.cur ^= 1;
    I.nsolid[I.cur] = 0;
}

static bool on_solid(float x, float y)
{
    int last = I.cur ^ 1;
    for (int i = 0; i < I.nsolid[last]; i++)
        if (skin_hit(I.solid[last][i], (int)x, (int)y))
            return true;
    return false;
}

bool uiin_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_MOUSEMOTION:
        I.mx = e->motion.x * I.scale;
        I.my = e->motion.y * I.scale;
        return (I.down[0] && I.ours[0]) || (I.down[1] && I.ours[1]);
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int b = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1 : -1;
        I.mx = e->button.x * I.scale;
        I.my = e->button.y * I.scale;
        if (b < 0)
            return on_solid(I.mx, I.my);
        if (e->type == SDL_MOUSEBUTTONDOWN) {
            I.down[b] = true;
            I.pressed[b] = true;
            I.down_x[b] = I.mx;
            I.down_y[b] = I.my;
            I.ours[b] = on_solid(I.mx, I.my);
            return I.ours[b];
        }
        bool ours = I.ours[b];
        I.down[b] = false;
        I.released[b] = true;
        return ours;
    }
    case SDL_MOUSEWHEEL:
        if (!on_solid(I.mx, I.my))
            return false;
        I.wheel += e->wheel.preciseY != 0 ? e->wheel.preciseY : (float)e->wheel.y;
        return true;
    case SDL_KEYDOWN:
        I.mods = (SDL_Keymod)e->key.keysym.mod;
        if (I.nkeys < KEYS)
            I.keys[I.nkeys++] = e->key.keysym.sym;
        return I.typing;
    case SDL_KEYUP:
        I.mods = (SDL_Keymod)e->key.keysym.mod;
        return I.typing;
    case SDL_TEXTINPUT:
        if (I.typing && strlen(I.typed) + strlen(e->text.text) < sizeof I.typed)
            strcat(I.typed, e->text.text);
        return I.typing;
    }
    return false;
}

void uiin_block(bool on)
{
    I.blocked = on;
}

bool uiin_over(UIRect r)
{
    return !I.blocked && skin_hit(r, (int)I.mx, (int)I.my);
}

static bool clicked_with(UIRect r, int b)
{
    return !I.blocked && I.released[b] && I.ours[b] && skin_hit(r, (int)I.mx, (int)I.my) &&
           skin_hit(r, (int)I.down_x[b], (int)I.down_y[b]);
}

bool uiin_clicked(UIRect r)
{
    return clicked_with(r, 0);
}

bool uiin_right_clicked(UIRect r)
{
    return clicked_with(r, 1);
}

bool uiin_pressed(UIRect r, int b)
{
    return !I.blocked && b >= 0 && b < 2 && I.pressed[b] && I.ours[b] &&
           skin_hit(r, (int)I.down_x[b], (int)I.down_y[b]);
}

bool uiin_mouse_down(int b)
{
    return b >= 0 && b < 2 && I.down[b];
}

bool uiin_clicked_outside(UIRect r)
{
    return !I.blocked && I.released[0] && !skin_hit(r, (int)I.down_x[0], (int)I.down_y[0]);
}

void uiin_solid(UIRect r)
{
    if (I.nsolid[I.cur] < SOLIDS)
        I.solid[I.cur][I.nsolid[I.cur]++] = r;
}

void uiin_tooltip(const char *s)
{
    snprintf(I.tip, sizeof I.tip, "%s", s ? s : "");
}

void uiin_draw_tooltip(const Fonts *f, int w, int h)
{
    if (!I.tip[0] || I.down[0] || I.down[1])
        return;
    float tw = skin_text_width(f->mono_small, I.tip), th = skin_text_height(f->mono_small);
    UIRect r = {I.mx + 14, I.my + 18, tw + 18, th + 10};
    if (r.x + r.w > w - 4)
        r.x = (float)w - 4 - r.w;
    if (r.y + r.h > h - 4)
        r.y = I.my - 8 - r.h;
    skin_note(r, false);
    skin_text(f->mono_small, I.tip, r.x + 9, r.y + 5, TONE_TEXT, ALIGN_LEFT);
}

void uiin_mouse(float *x, float *y)
{
    *x = I.mx;
    *y = I.my;
}

bool uiin_key(SDL_Keycode k)
{
    for (int i = 0; i < I.nkeys; i++)
        if (I.keys[i] == k)
            return true;
    return false;
}

SDL_Keymod uiin_mods(void)
{
    return I.mods;
}

const char *uiin_typed(void)
{
    return I.typed;
}

void uiin_inject_text(const char *s)
{
    if (I.typing && strlen(I.typed) + strlen(s) < sizeof I.typed)
        strcat(I.typed, s);
}

void uiin_set_typing(bool on)
{
    if (on != I.typing) {
        if (on)
            SDL_StartTextInput();
        else
            SDL_StopTextInput();
    }
    I.typing = on;
}

bool uiin_typing(void)
{
    return I.typing;
}

float uiin_wheel(void)
{
    return I.blocked ? 0 : I.wheel;
}
