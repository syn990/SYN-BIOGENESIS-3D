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
#include "ui/uimap.h"

#include <stdio.h>
#include <stdlib.h>
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


/* ---- syn-nav-alt: the interface map (ui/uimap.h) ---- */
#define NAMED 256
#define EVENTS 16
typedef struct {
    UIRect r;
    char role[12];
    char label[64];
    bool disabled, active, hover;
} Named;
static struct {
    int on; /* 0 unknown, 1 on, -1 off */
    const char *path;
    Named n[2][NAMED];
    int nn[2];
    long frame;
    int w, h;
    struct { long frame; char kind[8]; char label[64]; float x, y; bool ours; } ev[EVENTS];
    int nev; /* total ever, the ring keeps the last EVENTS */
    unsigned long last_hash;
    long last_write;
} M;

static bool map_on(void)
{
    if (!M.on) {
        M.path = getenv("SYN3D_UIMAP");
        M.on = M.path && *M.path ? 1 : -1;
    }
    return M.on > 0;
}

static bool same_rect(UIRect a, UIRect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

void uiin_map_size(int w, int h)
{
    M.w = w;
    M.h = h;
}

void uiin_name(UIRect r, const char *role, const char *label, bool disabled, bool active)
{
    if (!map_on() || !label || !label[0])
        return;
    int c = I.cur;
    for (int i = 0; i < M.nn[c]; i++)
        if (same_rect(M.n[c][i].r, r))
            return; /* named once a frame, by the first who draws it */
    if (M.nn[c] >= NAMED)
        return;
    Named *n = &M.n[c][M.nn[c]++];
    n->r = r;
    snprintf(n->role, sizeof n->role, "%s", role);
    snprintf(n->label, sizeof n->label, "%s", label);
    n->disabled = disabled;
    n->active = active;
    n->hover = !I.blocked && skin_hit(r, (int)I.mx, (int)I.my);
}

static const char *named_at(UIRect r)
{
    int c = I.cur;
    for (int i = 0; i < M.nn[c]; i++)
        if (same_rect(M.n[c][i].r, r))
            return M.n[c][i].label;
    return "";
}

static void map_event(const char *kind, const char *label, float x, float y, bool ours)
{
    if (!map_on())
        return;
    int k = M.nev++ % EVENTS;
    M.ev[k].frame = M.frame;
    snprintf(M.ev[k].kind, sizeof M.ev[k].kind, "%s", kind);
    snprintf(M.ev[k].label, sizeof M.ev[k].label, "%s", label ? label : "");
    M.ev[k].x = x;
    M.ev[k].y = y;
    M.ev[k].ours = ours;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\')
            fprintf(f, "\\%c", ch);
        else if (ch < 0x20)
            fprintf(f, "\\u%04x", ch);
        else
            fputc(ch, f);
    }
    fputc('"', f);
}

/* the frame just drawn (index c), written when it differs from the last */
static void map_write(int c)
{
    unsigned long h = 1469598103934665603UL;
    const unsigned char *p = (const unsigned char *)M.n[c];
    for (size_t i = 0; i < sizeof(Named) * (size_t)M.nn[c]; i++)
        h = (h ^ p[i]) * 1099511628211UL;
    h ^= (unsigned long)M.nev * 31 + (unsigned long)(int)I.mx * 7 + (unsigned long)(int)I.my;
    if (h == M.last_hash && M.frame - M.last_write < 60)
        return;
    M.last_hash = h;
    M.last_write = M.frame;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", M.path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "{\"v\":1,\"app\":\"syn-biogenesis-3d\",\"frame\":%ld,\"ticks_ms\":%u,", M.frame,
            (unsigned)SDL_GetTicks());
    fprintf(f, "\"drawable\":[%d,%d],\"scale\":%g,\"pointer\":[%g,%g],\"buttons\":[%d,%d],\"typing\":%s,",
            M.w, M.h, I.scale, I.mx, I.my, I.down[0], I.down[1], I.typing ? "true" : "false");
    fputs("\"widgets\":[", f);
    for (int i = 0; i < M.nn[c]; i++) {
        Named *n = &M.n[c][i];
        fprintf(f, "%s{\"role\":\"%s\",\"label\":", i ? "," : "", n->role);
        json_str(f, n->label);
        fprintf(f, ",\"x\":%g,\"y\":%g,\"w\":%g,\"h\":%g,\"hover\":%s,\"disabled\":%s,\"active\":%s}", n->r.x,
                n->r.y, n->r.w, n->r.h, n->hover ? "true" : "false", n->disabled ? "true" : "false",
                n->active ? "true" : "false");
    }
    fputs("],\"events\":[", f);
    int first = M.nev > EVENTS ? M.nev - EVENTS : 0;
    for (int i = first; i < M.nev; i++) {
        int k = i % EVENTS;
        fprintf(f, "%s{\"seq\":%d,\"frame\":%ld,\"kind\":\"%s\",\"label\":", i > first ? "," : "", i,
                M.ev[k].frame, M.ev[k].kind);
        json_str(f, M.ev[k].label);
        fprintf(f, ",\"x\":%g,\"y\":%g,\"ours\":%s}", M.ev[k].x, M.ev[k].y, M.ev[k].ours ? "true" : "false");
    }
    fputs("]}\n", f);
    fclose(f);
    rename(tmp, M.path);
}

void uiin_begin_frame(float scale)
{
    if (map_on()) {
        map_write(I.cur);
        M.frame++;
        M.nn[I.cur ^ 1] = 0;
    }
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
            map_event(b ? "rdown" : "down", "", I.mx, I.my, I.ours[b]);
            return I.ours[b];
        }
        bool ours = I.ours[b];
        map_event(b ? "rup" : "up", "", I.mx, I.my, ours);
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
    bool got = !I.blocked && I.released[b] && I.ours[b] && skin_hit(r, (int)I.mx, (int)I.my) &&
               skin_hit(r, (int)I.down_x[b], (int)I.down_y[b]);
    if (got && map_on())
        map_event(b ? "rclick" : "click", named_at(r), I.mx, I.my, true);
    return got;
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
