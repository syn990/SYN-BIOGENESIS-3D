/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's minimap (src/syn/view.c), the SYN edition of
 * its C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* The minimap, SYN-BIOGENESIS's: a window of its own at the bottom right,
 * every organism a dot in the look's colour of what most of it is, shown
 * while part of the world is out of view. Here it has two panes, the box
 * from above and from the side (its depth, the surface at the top), each
 * with where the camera looks and the way it looks from; a click or a drag
 * on either moves the camera there. M shows it or hides it; the choice is
 * kept with the theme. */

#include "app/minimap.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/app_internal.h"
#include "app/looks.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

#define MAP_SIDE 200.0f
#define MAP_TITLE 22.0f

static struct {
    bool on, loaded;
    int drag; /* 0: none, 1: from above, 2: from the side */
} M = {.on = true};

static void load(void)
{
    if (M.loaded)
        return;
    M.loaded = true;
    const char *v = shell_setting("minimap");
    if (v)
        M.on = v[0] == '1';
}

bool minimap_on(void)
{
    load();
    return M.on;
}

void minimap_toggle(void)
{
    load();
    M.on = !M.on;
    shell_setting_set("minimap", M.on ? "1" : "0");
}

/* every corner of the box within the picture */
static bool box_in_view(const App *a)
{
    const float *m = a->view.view_proj;
    for (int i = 0; i < 8; i++) {
        double p[3] = {i & 1 ? a->w.width : 0, i & 2 ? a->w.height : 0, i & 4 ? a->w.depth : 0};
        double c[4];
        for (int r = 0; r < 4; r++)
            c[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
        if (c[3] <= 0 || fabs(c[0] / c[3]) > 1 || fabs(c[1] / c[3]) > 1)
            return false;
    }
    return true;
}

/* what most of a living organism is, by its segments */
static int main_colour(const Organism *o)
{
    if (!o->alive)
        return COL_BROWN;
    int count[COL_COUNT] = {0}, best = 0;
    for (int s = 0; s < o->segments; s++)
        if (o->seg_color[s] < COL_COUNT)
            count[o->seg_color[s]]++;
    for (int k = 0; k < COL_COUNT; k++)
        if (count[k] > count[best])
            best = k;
    return best;
}

static void mark(float x, float y, Rgb c)
{
    skin_fill(ui_rect(x - 5, y - 0.5f, 10, 1.5f), c, 1);
    skin_fill(ui_rect(x - 0.5f, y - 5, 1.5f, 10), c, 1);
}

void minimap_draw(App *a, int w, int h, float bottom)
{
    (void)h;
    load();
    const World *wd = &a->w;
    if (!M.on || wd->width <= 0 || wd->height <= 0 || wd->depth <= 0 || box_in_view(a)) {
        M.drag = 0;
        return;
    }
    const Fonts *fo = shell_fonts();
    float k = MAP_SIDE / (float)fmax(wd->width, wd->height);
    float mw = fmaxf(30, (float)wd->width * k), mh = fmaxf(30, (float)wd->height * k);
    float sh = fmaxf(24, (float)wd->depth * k);
    if (sh > MAP_SIDE)
        sh = MAP_SIDE;
    float kz = sh / (float)wd->depth;
    float total = MAP_TITLE + 4 + mh + 6 + sh + 4;
    if (mw + 40 > (float)w || total + 12 > bottom - SHELL_BAR_H)
        return;
    UIRect frame = {(float)w - 16 - mw - 8, bottom - 12 - total, mw + 8, total};
    uiin_solid(frame);
    skin_panel(frame, true);
    UIRect tb = {frame.x + 1, frame.y + 1, frame.w - 2, MAP_TITLE - 1};
    Rgb tc = skin_titlebar(tb);
    char size[48];
    snprintf(size, sizeof size, "%d x %d x %d", wd->width, wd->height, wd->depth);
    float ty = tb.y + (tb.h - skin_text_height(fo->mono_small_bold)) * 0.5f;
    skin_text_rgb(fo->mono_small_bold, "MAP", tb.x + 8, ty, tc.r, tc.g, tc.b, ALIGN_LEFT);
    skin_text_rgb(fo->mono_small, size, tb.x + tb.w - 8, ty, tc.r, tc.g, tc.b, ALIGN_RIGHT);
    UIRect top = {frame.x + 4, frame.y + MAP_TITLE + 4, mw, mh};
    UIRect side = {top.x, top.y + mh + 6, mw, sh};
    skin_fill(top, (Rgb){0, 0, 0}, 0.88f);
    skin_fill(side, (Rgb){0, 0, 0}, 0.88f);
    skin_text(fo->mono_small, "above", top.x + 4, top.y + 2, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->mono_small, "side", side.x + 4, side.y + 2, TONE_MUTED, ALIGN_LEFT);

    /* the organisms: x and y from above, x and depth from the side, the
     * surface at the top */
    Look look = look_current();
    for (int i = 0; i < wd->norgs; i++) {
        const Organism *o = wd->orgs[i];
        if (o->removed)
            continue;
        int c = main_colour(o);
        float rgb[3];
        look_rgb(look, c, rgb);
        Rgb col = {(uint8_t)(rgb[0] * 255), (uint8_t)(rgb[1] * 255), (uint8_t)(rgb[2] * 255)};
        float al = o->alive ? 1 : 0.45f;
        float x = top.x + (float)o->pos.x * k;
        skin_fill(ui_rect(x - 1, top.y + (float)o->pos.y * k - 1, 2, 2), col, al);
        skin_fill(ui_rect(x - 1, side.y + sh - (float)o->pos.z * kz - 1, 2, 2), col, al);
    }

    /* where the camera looks, and the way it looks from */
    Rgb acc = skin_tone(TONE_ACCENT);
    float ax = top.x + (float)a->aim.x * k, ay = top.y + (float)a->aim.y * k;
    float az = side.y + sh - (float)a->aim.z * kz;
    mark(ax, ay, acc);
    mark(ax, az, acc);
    V3 back = v3_scale(a->cam_fwd, -1);
    double hx = back.x, hy = back.y, hl = hypot(hx, hy);
    if (hl > 1e-6)
        skin_line(ax, ay, ax + (float)(hx / hl) * 22, ay + (float)(hy / hl) * 22, 1.5f, acc, 0.8f);
    double vx = back.x, vz = back.z, vl = hypot(vx, vz);
    if (vl > 1e-6)
        skin_line(ax, az, ax + (float)(vx / vl) * 22, az - (float)(vz / vl) * 22, 1.5f, acc, 0.8f);

    /* a click or a drag moves the camera there */
    if (uiin_pressed(top, 0))
        M.drag = 1;
    else if (uiin_pressed(side, 0))
        M.drag = 2;
    if (M.drag && !uiin_mouse_down(0))
        M.drag = 0;
    if (M.drag) {
        float mx, my;
        uiin_mouse(&mx, &my);
        double x = fmin(fmax((mx - top.x) / k, 0), wd->width);
        if (M.drag == 1)
            a->aim = v3(x, fmin(fmax((my - top.y) / k, 0), wd->height), a->aim.z);
        else
            a->aim = v3(x, a->aim.y, fmin(fmax((side.y + sh - my) / kz, 0), wd->depth));
        a->follow = FOLLOW_NONE;
    }
    if (uiin_over(frame) && !M.drag)
        uiin_tooltip("The box from above and from the side: click or drag to look there  (M hides it)");
}
