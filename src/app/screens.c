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

/* The screens under the bar, as panels over the running world: the
 * parameters, the main menu and the options; and what they share. */

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <cglm/cglm.h>
#include <epoxy/gl.h>

#include "app/app_internal.h"
#include "app/flesh.h"
#include "app/looks.h"
#include "app/minimap.h"
#include "sim/params.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/theme.h"
#include "ui/shell.h"

static float text_w(TTF_Font *f, const char *s)
{
    return s && s[0] ? skin_text_width(f, s) : 0;
}

static float mid(TTF_Font *f, const char *s, float x, float cy, Rgb c, Align al)
{
    return skin_text_rgb(f, s, x, cy - skin_text_height(f) * 0.5f, c.r, c.g, c.b, al);
}

/* The screen's panel under the bar, with a title bar; returns its inside. */
UIRect screen_panel(int w, int h, bool hints, const char *title, const char *subtitle)
{
    const Fonts *fo = shell_fonts();
    float top = SHELL_BAR_H + 12, bottom = (float)h - (hints ? SHELL_HINTS_H : 0) - 12;
    UIRect r = {16, top, (float)w - 32, bottom - top};
    uiin_solid(r);
    skin_panel(r, true);
    float th = 30;
    UIRect tb = {r.x + 1, r.y + 1, r.w - 2, th};
    Rgb tc = skin_titlebar(tb);
    mid(fo->mono_bold, title, tb.x + 12, tb.y + th * 0.5f, tc, ALIGN_LEFT);
    if (subtitle)
        mid(fo->mono_small, subtitle, tb.x + tb.w - 12, tb.y + th * 0.5f, tc, ALIGN_RIGHT);
    return ui_rect(r.x + 12, r.y + th + 12, r.w - 24, r.h - th - 24);
}

bool screen_button(UIRect b, const char *icon, const char *label, bool disabled, const char *tip)
{
    const Fonts *fo = shell_fonts();
    bool hv = !disabled && uiin_over(b);
    Tone tone = skin_button_chrome(b, (WidgetState){.hover = hv, .disabled = disabled}, true);
    Rgb fg = skin_tone(disabled ? TONE_MUTED : tone);
    float cy = b.y + b.h * 0.5f - 1;
    float tw = text_w(fo->small_bold, label) + (icon ? 24 : 0);
    float x = b.x + (b.w - tw) * 0.5f;
    if (icon) {
        mid(fo->icon, icon, x + 8, cy, fg, ALIGN_CENTER);
        x += 24;
    }
    mid(fo->small_bold, label, x, cy, fg, ALIGN_LEFT);
    if (hv && tip)
        uiin_tooltip(tip);
    return hv && uiin_clicked(b);
}

void fmt_num(char *out, size_t n, double v)
{
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.1f", fabs(v));
    char *dot = strchr(tmp, '.');
    if (dot && dot[1] == '0' && dot[2] == '\0')
        *dot = '\0';
    /* the integer digits in threes */
    char *ip_end = strchr(tmp, '.');
    int ilen = ip_end ? (int)(ip_end - tmp) : (int)strlen(tmp);
    char buf[96];
    int k = 0;
    if (v < 0 && strcmp(tmp, "0") != 0)
        buf[k++] = '-';
    for (int i = 0; i < ilen; i++) {
        buf[k++] = tmp[i];
        int left = ilen - i - 1;
        if (left > 0 && left % 3 == 0)
            buf[k++] = ',';
    }
    snprintf(buf + k, sizeof buf - (size_t)k, "%s", ip_end ? ip_end : "");
    snprintf(out, n, "%s", buf);
}

/* A genetic code's adult body in box, turning about its axis and tilted
 * toward the eye: as flesh, in the look's colours (app/flesh.c). */
void screen_code(const GeneticCode *gc, UIRect box, double seconds)
{
    screen_code_view(gc, box, seconds * 0.6, 0.55);
}

int screen_code_scene(const GeneticCode *gc, double aspect, double px_h, double turn, double tilt, SceneView *out,
                      const BodyInstance **body)
{
    static V3 *ends;
    static uint8_t *cols;
    static BodyInstance *inst;
    static int cap;
    int n = gc->ngenes * gc->symmetry;
    if (n <= 0 || aspect <= 0)
        return 0;
    if (n > cap) {
        cap = n * 2;
        ends = realloc(ends, (size_t)cap * 2 * sizeof *ends);
        cols = realloc(cols, (size_t)cap * sizeof *cols);
        inst = realloc(inst, (size_t)flesh_count(cap) * sizeof *inst);
        if (!ends || !cols || !inst)
            abort();
    }
    V3 *bs = ends, *be = ends + n;
    gc_body_shape(gc, 1.0, bs, be);
    V3 lo = v3(1e30, 1e30, 1e30), hi = v3(-1e30, -1e30, -1e30);
    for (int i = 0; i < n; i++) {
        cols[i] = gc->genes[i % gc->ngenes].color;
        for (int k = 0; k < 2; k++) {
            V3 p = k ? be[i] : bs[i];
            lo = v3(fmin(lo.x, p.x), fmin(lo.y, p.y), fmin(lo.z, p.z));
            hi = v3(fmax(hi.x, p.x), fmax(hi.y, p.y), fmax(hi.z, p.z));
        }
    }
    V3 c = v3_scale(v3_add(lo, hi), 0.5);
    double rad = 1;
    for (int i = 0; i < n; i++)
        rad = fmax(rad, fmax(v3_len(v3_sub(bs[i], c)), v3_len(v3_sub(be[i], c))));
    rad += 3; /* the flesh round the bones */

    /* the body as flesh, in the look's colours, the cell at the hub paler */
    Look look = look_current();
    LookScene ls;
    look_scene(look, skin_theme(), &ls);
    float phase = (float)fmod(gc->genes[0].length * 0.137, 1.0);
    int main_col;
    int m = flesh_build(inst, bs, be, n, gc->ngenes, cols, v3(0, 0, 1), phase, 1.0, &main_col);
    for (int k = 0; k < m; k++) {
        bool core = k == m - 1;
        float rgb[3];
        look_rgb(look, core ? main_col : cols[k], rgb);
        for (int q = 0; q < 3; q++)
            inst[k].rgb[q] = (core ? rgb[q] * 0.7f + 0.3f : rgb[q]) * 0.85f;
        inst[k].glow = ls.glow;
    }

    /* the eye: turned about the axis, tilted from straight above, near
     * enough that the body fills the box */
    double ct = cos(turn), st = sin(turn), cx = cos(tilt), sx = sin(tilt);
    V3 back = v3(sx * st, sx * ct, cx), right = v3(ct, -st, 0), up = v3(cx * st, cx * ct, -sx);
    double fovy = 30 * D_PI / 180;
    double half = fmin(fovy / 2, atan(tan(fovy / 2) * aspect));
    double dist = rad / sin(half) * 1.04;
    V3 eye = v3_add(c, v3_scale(back, dist));
    SceneView v;
    memset(&v, 0, sizeof v);
    mat4 proj, view, vp;
    vec3 e = {(float)eye.x, (float)eye.y, (float)eye.z}, at = {(float)c.x, (float)c.y, (float)c.z};
    vec3 u = {(float)up.x, (float)up.y, (float)up.z};
    glm_perspective((float)fovy, (float)aspect, (float)fmax(0.1, dist - rad * 1.6), (float)(dist + rad * 1.6), proj);
    glm_lookat(e, at, u, view);
    glm_mat4_mul(proj, view, vp);
    memcpy(v.view_proj, vp, sizeof v.view_proj);
    memcpy(v.eye, e, sizeof v.eye);
    V3 l = v3_unit_or(v3_add(v3_add(v3_scale(up, 0.7), v3_scale(right, -0.35)), v3_scale(back, 0.75)), back);
    v.light_dir[0] = (float)l.x;
    v.light_dir[1] = (float)l.y;
    v.light_dir[2] = (float)l.z;
    for (int q = 0; q < 3; q++) {
        v.fog_rgb[q] = ls.water[q] * 1.6f + 0.04f;
        v.deep_rgb[q] = ls.water[q] * 0.8f;
    }
    v.fog_near = v.fog_far = 1e9f;
    v.box[0] = v.box[1] = 1;
    v.box[2] = (float)(c.z + rad);
    v.time = (float)fmod(SDL_GetTicks() / 1000.0, 10000.0);
    v.px_scale = (float)(px_h / (2 * tan(fovy / 2)));
    v.tissue = ls.tissue;
    v.sss = ls.sss;
    v.lines = ls.lines;
    v.line_radius = 1;
    v.direct = 1;
    *out = v;
    *body = inst;
    return m;
}

void screen_code_view(const GeneticCode *gc, UIRect box, double turn, double tilt)
{
    if (box.w < 4 || box.h < 4)
        return;
    SceneView v;
    const BodyInstance *inst;
    int m = screen_code_scene(gc, box.w / box.h, box.h, turn, tilt, &v, &inst);
    if (!m)
        return;

    /* drawn into the box, cut to whatever cuts the 2D drawing there */
    float x0 = box.x, y0 = box.y, x1 = box.x + box.w, y1 = box.y + box.h, cx0, cy0, cx1, cy1;
    if (gl2d_clip_current(&cx0, &cy0, &cx1, &cy1)) {
        x0 = fmaxf(x0, cx0);
        y0 = fmaxf(y0, cy0);
        x1 = fminf(x1, cx1);
        y1 = fminf(y1, cy1);
    }
    if (x1 - x0 < 1 || y1 - y0 < 1)
        return;
    gl2d_flush();
    float H = gl2d_height();
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)floorf(x0), (int)(H - ceilf(y1)), (int)(ceilf(x1) - floorf(x0)), (int)(ceilf(y1) - floorf(y0)));
    glViewport((int)lroundf(box.x), (int)lroundf(H - box.y - box.h), (int)lroundf(box.w), (int)lroundf(box.h));
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    bodies_draw(inst, m, &v);
    gl2d_resume();
}

/* ---- the parameters ---- */

typedef struct {
    const char *name, *label, *group;
    char type; /* 'i' or 'd' */
    size_t off;
    double step, min, max;
    bool new_world; /* takes a new world to apply */
} ParamDef;

#define PI_(n, l, g, st, lo, hi, nw) {#n, l, g, 'i', offsetof(Params, n), st, lo, hi, nw}
#define PD_(n, l, g, st, lo, hi, nw) {#n, l, g, 'd', offsetof(Params, n), st, lo, hi, nw}

static const ParamDef k_params[] = {
    PI_(world_width, "Width", "WORLD", 50, 8, 20000, true),
    PI_(world_height, "Height", "WORLD", 50, 8, 20000, true),
    PI_(world_depth, "Depth", "WORLD", 8, 4, 20000, true),
    PI_(initial_organisms, "Founders", "WORLD", 1, 0, 5000, true),
    PD_(co2_density, "CO2 per million cubic units", "WORLD", 10, 0, 1e6, true),
    PD_(segment_radius, "Segment thickness", "WORLD", 0.25, 0.25, 20, true),
    PI_(pitch_prob, "Genes that bend (%)", "WORLD", 5, 0, 100, false),
    PI_(cone_prob, "New bodies that are cones (%)", "WORLD", 5, 0, 100, false),
    PI_(orb_prob, "New bodies that are orbs (%)", "WORLD", 5, 0, 100, false),
    PD_(light_depth, "Light's depth (0: no dimming)", "WATER", 10, 0, 100000, false),
    PD_(shade, "Shade a body casts", "WATER", 0.1, 0, 10, false),
    PD_(co2_diffusion, "CO2's spreading", "WATER", 1, 0, 150, false),
    PD_(co2_saturation, "CO2 that leaves need", "WATER", 10, 0, 1e6, false),
    PD_(sink, "The dead's sinking", "WATER", 0.005, 0, 1, false),
    PD_(sight, "Sight", "WATER", 10, 0, 10000, false),
    PD_(leaf_turn, "Leaves turning to the light", "WATER", 0.02, 0, 10, false),
    PD_(rise, "A float's lift", "WATER", 0.001, 0, 1, false),
    PD_(day_length, "A day (time units; 0: always noon)", "WATER", 1, 0, 1000, false),
    PD_(ch4_saturation, "Methane that purple needs", "WATER", 1, 0, 1e6, false),
    PI_(methane_divisor, "Purple's divisor", "WATER", 50, 1, 100000, false),
    PD_(ch4_oxidation, "Methane's life (frames; 0: forever)", "WATER", 100, 0, 1e7, false),
    PI_(max_age, "Life (time units)", "LIFE", 1, 1, 1000, false),
    PD_(mutation_rate, "Mutation rate", "LIFE", 0.01, 0, 1, false),
    PI_(initial_energy, "A founder's energy", "LIFE", 5, 1, 10000, false),
    PI_(segment_cost_divisor, "Upkeep divisor", "LIFE", 500, 1, 1000000, false),
    PD_(decay_energy, "Corpse decay", "LIFE", 0.05, 0, 100, false),
    PI_(green_obtained_energy_divisor, "Light divisor", "LIFE", 50, 1, 100000, false),
    PD_(organic_obtained_energy, "A bite takes", "LIFE", 0.05, 0, 10, false),
    PD_(organic_subs_produced, "A bite breathes out", "LIFE", 0.05, 0, 1, false),
    PD_(rubbing, "Water's drag", "MOTION", 0.005, 0.5, 1, false),
    PD_(elasticity, "Elasticity", "MOTION", 0.05, 0, 1, false),
    PD_(max_vel, "Top speed", "MOTION", 0.5, 0.5, 100, false),
    PD_(max_rot, "Top spin", "MOTION", 0.02, 0.01, 3.14, false),
    PI_(green_prob, "Green", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(red_prob, "Red", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(cyan_prob, "Cyan", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(blue_prob, "Blue", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(white_prob, "White", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(gray_prob, "Gray", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(yellow_prob, "Yellow", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PI_(purple_prob, "Purple", "NEW GENES' COLOURS", 1, 0, 1000, false),
    PD_(green_energy_consumption, "Green", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(red_energy_consumption, "Red", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(cyan_energy_consumption, "Cyan", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(blue_energy_consumption, "Blue", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(white_energy_consumption, "White", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(gray_energy_consumption, "Gray", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(yellow_energy_consumption, "Yellow", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(purple_energy_consumption, "Purple", "WHAT A USE COSTS", 0.1, 0, 100, false),
};
#define NPARAMS ((int)(sizeof k_params / sizeof k_params[0]))

static double param_get(const ParamDef *d)
{
    const char *base = (const char *)&P + d->off;
    return d->type == 'i' ? (double)*(const int *)base : *(const double *)base;
}

static void param_put(const ParamDef *d, double v)
{
    v = fmin(fmax(v, d->min), d->max);
    char *base = (char *)&P + d->off;
    if (d->type == 'i')
        *(int *)base = (int)lround(v);
    else
        *(double *)base = v;
}

static void param_text(const ParamDef *d, char *out, size_t n)
{
    double v = param_get(d);
    if (d->type == 'i')
        snprintf(out, n, "%d", (int)v);
    else
        snprintf(out, n, "%g", v);
}

/* *v from text, if it is a number d takes */
static bool param_parse(const ParamDef *d, const char *text, double *v)
{
    char *end;
    *v = strtod(text, &end);
    while (isspace((unsigned char)*end))
        end++;
    return end != text && !*end && *v >= d->min && *v <= d->max;
}

bool params_set_text(const char *name, const char *value, char *err, int errlen)
{
    for (int i = 0; i < NPARAMS; i++) {
        if (strcasecmp(k_params[i].name, name))
            continue;
        double v;
        if (!param_parse(&k_params[i], value, &v)) {
            snprintf(err, (size_t)errlen, "%s takes %g to %g", name, k_params[i].min,
                     k_params[i].max);
            return false;
        }
        param_put(&k_params[i], v);
        return true;
    }
    snprintf(err, (size_t)errlen, "no parameter %s ('params' lists them)", name);
    return false;
}

void params_print(void)
{
    const char *group = NULL;
    for (int i = 0; i < NPARAMS; i++) {
        const ParamDef *d = &k_params[i];
        if (!group || strcmp(group, d->group)) {
            group = d->group;
            shell_print(SHELL_HEAD, "%s", group);
        }
        char v[32];
        param_text(d, v, sizeof v);
        shell_print(SHELL_OUT, "  %-32s %-12s %s%s", d->name, v, d->label,
                    d->new_world ? " (a new world)" : "");
    }
}

void params_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    UIRect in = screen_panel(w, h, a->hints, "PARAMETERS", "0.9's, and the water's");
    float colw = fminf(420, (in.w - 20) / 2), row = 30;
    float bottom_bar = 44;
    /* two columns of groups, scrolled together if they don't fit */
    UIRect area = {in.x, in.y, in.w, in.h - bottom_bar};
    if (uiin_over(area))
        a->params_scroll -= uiin_wheel() * row;
    float x = in.x, y = in.y - a->params_scroll, col_top = y;
    int col = 0;
    const char *group = NULL;
    float tallest = 0;
    for (int i = 0; i < NPARAMS; i++) {
        const ParamDef *d = &k_params[i];
        if (!group || strcmp(group, d->group)) {
            int left = 0;
            for (int k = i; k < NPARAMS && !strcmp(k_params[k].group, d->group); k++)
                left++;
            /* a group that won't fit under the last goes to the next column */
            if (group && col == 0 && y + 28 + left * row > area.y + area.h && colw * 2 + 20 <= in.w) {
                tallest = fmaxf(tallest, y - col_top);
                col = 1;
                x = in.x + colw + 20;
                y = col_top;
            }
            group = d->group;
            if (y >= area.y - 2 && y + 20 <= area.y + area.h)
                skin_text(fo->mono_small_bold, group, x, y + 4, TONE_ACCENT, ALIGN_LEFT);
            y += 28;
        }
        if (y >= area.y - 2 && y + row <= area.y + area.h) {
            UIRect r = {x, y, colw, row - 4};
            skin_well(r);
            char label[64];
            snprintf(label, sizeof label, "%s%s", d->label, d->new_world ? " *" : "");
            mid(fo->small, label, r.x + 8, r.y + r.h * 0.5f, skin_tone(TONE_MUTED), ALIGN_LEFT);
            char v[32], typed[64];
            param_text(d, v, sizeof v);
            float bx = r.x + r.w - 2 * 28 - 4, fw = fminf(110, r.w * 0.3f);
            UIRect field = {bx - 4 - fw, r.y + 1, fw, r.h - 2};
            if (shell_field(d, field, fo->mono_small_bold, v, typed, sizeof typed) &&
                typed[strspn(typed, " ")]) {
                double nv;
                if (param_parse(d, typed, &nv))
                    param_put(d, nv);
                else
                    interface_note(a, true, "%s takes a number from %g to %g", d->label, d->min,
                                   d->max);
            }
            if (screen_button(ui_rect(bx, r.y + 1, 26, r.h - 2), ICON_MINUS, "", false, NULL))
                param_put(d, param_get(d) - d->step);
            if (screen_button(ui_rect(bx + 28, r.y + 1, 26, r.h - 2), ICON_PLUS, "", false, NULL))
                param_put(d, param_get(d) + d->step);
            if (uiin_over(ui_rect(r.x, r.y, field.x - r.x, r.h))) {
                static char tip[160];
                snprintf(tip, sizeof tip, "%s   (click the number to type one; console: set %s VALUE)",
                         d->name, d->name);
                uiin_tooltip(tip);
            }
        }
        y += row;
    }
    tallest = fmaxf(tallest, y - col_top);
    float most = fmaxf(0, tallest - area.h);
    a->params_scroll = fminf(fmaxf(a->params_scroll, 0), most);

    float by = in.y + in.h - 34;
    skin_text(fo->small, "* takes a new world. The rest act on the running world at once.", in.x,
              by + 9, TONE_MUTED, ALIGN_LEFT);
    float bw = 190;
    if (screen_button(ui_rect(in.x + in.w - bw, by, bw, 34), ICON_DICE, "New world with these", false,
               "A new world from this seed with these parameters")) {
        char err[300];
        if (!world_options_check(err, sizeof err)) {
            interface_note(a, true, "%s", err);
        } else if (app_make_world(a)) {
            app_do(a, ACT_HOME);
            interface_note(a, false, "Seed %lld: %d organisms in %d x %d x %d",
                           (long long)a->wo.seed, a->w.population, a->w.width, a->w.height,
                           a->w.depth);
            a->screen = SCREEN_WORLD;
        }
    }
    if (screen_button(ui_rect(in.x + in.w - 2 * bw - 8, by, bw, 34), ICON_ROTATE, "Defaults", false,
               "Every parameter back to its default: 0.9's, and the box's (the world keeps going)"))
        params_set_defaults(&P);
}

/* ---- the main menu, over the living world ---- */

void menu_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    /* the grid: 260px buttons, 48 tall, 30 across and 12 down,
     * starting 40% of the way down */
    float btn_h = 48, gap_x = 30, gap_y = 12, total_w = 260 * 2 + gap_x;
    int rows = 4;
    float total_h = btn_h * (float)rows + gap_y * (float)(rows - 1);
    UIRect area = ui_rect(((float)w - total_w) * 0.5f, (float)h * 0.40f, total_w, total_h);
    struct {
        UIRect r;
        const char *label;
        int act;
        bool enabled;
    } b[] = {
        {ui_grid_cell(area, 2, rows, 0, 0, gap_x, gap_y), "CONTINUE", ACT_WORLD_TAB, true},
        {ui_grid_cell(area, 2, rows, 1, 0, gap_x, gap_y), "NEW WORLD", ACT_NEW, true},
        {ui_grid_cell(area, 2, rows, 0, 1, gap_x, gap_y), "LOAD WORLD", ACT_OPEN, true},
        {ui_grid_cell(area, 2, rows, 1, 1, gap_x, gap_y), "OPTIONS", ACT_OPTIONS, true},
        {ui_grid_cell(area, 2, rows, 0, 2, gap_x, gap_y), "ARENA", ACT_ARENA_TAB, true},
        {ui_grid_cell(area, 2, rows, 1, 2, gap_x, gap_y), "LIBRARY", ACT_LIBRARY_TAB, true},
        {ui_grid_cell(area, 2, rows, 0, 3, gap_x, gap_y), "TELESCOPE", ACT_TELESCOPE_TAB, true},
        {ui_grid_cell(area, 2, rows, 1, 3, gap_x, gap_y), "QUIT", ACT_QUIT, true},
    };
    int nb = (int)(sizeof b / sizeof b[0]);
    UIRect all = {0, 0, (float)w, (float)h};
    uiin_solid(all);
    /* a light shade over the living world: the point is to see it */
    skin_fill(all, (Rgb){0, 0, 0}, 0.34f);
    float y = (float)h * 0.14f;
    skin_text_rgb(fo->title, "SYN-BIOGENESIS-3D", (float)w * 0.5f, y, 235, 235, 235, ALIGN_CENTER);
    y += skin_text_height(fo->title) + 10;
    skin_text_rgb(fo->button, "Biogenesis 0.9 in three dimensions", (float)w * 0.5f, y, 170, 170, 170,
                  ALIGN_CENTER);
    int act = 0;
    for (int i = 0; i < nb; i++) {
        bool hov = b[i].enabled && uiin_over(b[i].r);
        skin_button(b[i].r, fo->button, b[i].label, (WidgetState){.hover = hov, .disabled = !b[i].enabled},
                    true);
        if (hov && uiin_clicked(b[i].r))
            act = b[i].act;
    }
    if (uiin_key(SDLK_RETURN) || uiin_key(SDLK_KP_ENTER))
        act = ACT_WORLD_TAB;
    /* which theme is on, bottom left, quietly */
    skin_text_rgb(fo->small, skin_theme()->name, 16, (float)h - skin_text_height(fo->small) - 12 -
                  (a->hints ? SHELL_HINTS_H : 0), 150, 150, 150, ALIGN_LEFT);
    if (act == ACT_WORLD_TAB)
        a->screen = SCREEN_WORLD;
    else if (act == ACT_LIBRARY_TAB || act == ACT_ARENA_TAB || act == ACT_TELESCOPE_TAB) {
        a->screen = SCREEN_WORLD; /* the tab actions toggle from the world */
        app_do(a, act);
    } else if (act)
        app_do(a, act);
}

/* ---- options ---- */

static const struct {
    const char *group, *title, *blurb;
} k_families[] = {
    {"vanilla", "VANILLA", "Flat panels, an accent bar"},
    {"bevel", "BEVEL", "Gradients and lit edges"},
    {"flatline", "FLATLINE", "No boxes, just text"},
    {"halo", "HALO", "Outlines that glow"},
    {"slab", "SLAB", "Heavy borders"},
};
#define FAMILY_COUNT (int)(sizeof k_families / sizeof k_families[0])

void options_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    static float scroll;
    /* a panel over the world */
    float pw = fminf((float)w * 0.86f, 1500), ph = (float)h * 0.84f;
    if (w < 1400)
        pw = (float)w - 48;
    if (h < 900)
        ph = (float)h - SHELL_BAR_H - 24;
    UIRect panel = {((float)w - pw) * 0.5f, fmaxf(SHELL_BAR_H + 12, ((float)h - ph) * 0.5f), pw, ph};
    float pad = 24;
    UIRect back = {panel.x + panel.w - pad - 140, panel.y + 18, 140, 40};
    UIRect full = {back.x - 12 - 220, back.y, 220, 40};
    UIRect all = {0, 0, (float)w, (float)h};
    uiin_solid(all);
    skin_fill(all, (Rgb){0, 0, 0}, 0.45f);
    skin_panel(panel, true);
    skin_text(fo->title, "OPTIONS", panel.x + pad, panel.y + 8, TONE_TEXT, ALIGN_LEFT);
    skin_button(back, fo->bold, "BACK", (WidgetState){.hover = uiin_over(back)}, true);
    skin_button(full, fo->bold, a->fullscreen ? "FULLSCREEN: ON" : "FULLSCREEN: OFF",
                (WidgetState){.hover = uiin_over(full), .active = a->fullscreen}, true);
    float y = panel.y + 8 + skin_text_height(fo->title) + 14;
    /* the look, and the minimap */
    skin_text(fo->heading, "LOOK", panel.x + pad, y + 8, TONE_TEXT, ALIGN_LEFT);
    float lx = panel.x + pad + 90;
    int look_clicked = -1;
    for (int l = 0; l < LOOK_COUNT; l++) {
        UIRect r = {lx + (float)l * 206, y, 200, 38};
        bool on = look_current() == (Look)l;
        skin_button(r, fo->bold, look_name((Look)l), (WidgetState){.hover = uiin_over(r), .active = on}, true);
        if (uiin_over(r) && uiin_clicked(r))
            look_clicked = l;
    }
    UIRect mini = {panel.x + panel.w - pad - 220, y, 220, 38};
    skin_button(mini, fo->bold, minimap_on() ? "MINIMAP: ON" : "MINIMAP: OFF",
                (WidgetState){.hover = uiin_over(mini), .active = minimap_on()}, true);
    if (uiin_over(mini) && uiin_clicked(mini))
        minimap_toggle();
    if (look_clicked >= 0)
        look_set((Look)look_clicked);
    y += 38 + 18;
    const Theme *cur = skin_theme();
    char line[256];
    snprintf(line, sizeof line, "Theme: %s  (%s, %s)", cur->name, theme_style_name(cur->style),
             cur->light ? "light" : "dark");
    skin_text(fo->bold, line, panel.x + pad, y, TONE_ACCENT, ALIGN_LEFT);
    y += skin_text_height(fo->bold) + 2;
    skin_text(fo->small,
              "The same palettes SYN-OS uses for labwc, waybar, foot and mako. Click one and everything changes "
              "to it.",
              panel.x + pad, y, TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->small) + 16;

    /* one column per family, each theme a row with its colours beside its name */
    UIRect list = {panel.x + pad, y, panel.w - pad * 2, panel.y + panel.h - pad - y};
    float col_gap = 16, col_w = (list.w - col_gap * (float)(FAMILY_COUNT - 1)) / (float)FAMILY_COUNT;
    float row_h = 30;
    int tallest = 0, nthemes = shell_theme_count();
    for (int f = 0; f < FAMILY_COUNT; f++) {
        int k = 0;
        for (int i = 0; i < nthemes; i++)
            k += !strcmp(shell_theme_at(i)->group, k_families[f].group);
        tallest = k > tallest ? k : tallest;
    }
    float most = fmaxf(0, 56 + row_h * (float)tallest - list.h);
    if (uiin_over(list))
        scroll -= uiin_wheel() * 40;
    scroll = fminf(fmaxf(scroll, 0), most);
    int clicked = -1;
    gl2d_clip_push(list.x, list.y, list.w, list.h);
    for (int f = 0; f < FAMILY_COUNT; f++) {
        float cx = list.x + (col_w + col_gap) * (float)f, cy = list.y - scroll;
        skin_text(fo->heading, k_families[f].title, cx, cy, TONE_TEXT, ALIGN_LEFT);
        skin_text(fo->small, k_families[f].blurb, cx, cy + skin_text_height(fo->heading), TONE_MUTED, ALIGN_LEFT);
        cy += 50;
        skin_separator(cx, cy - 4, col_w);
        for (int i = 0; i < nthemes; i++) {
            const Theme *t = shell_theme_at(i);
            if (strcmp(t->group, k_families[f].group))
                continue;
            UIRect row = {cx, cy, col_w, row_h - 2};
            bool selected = i == shell_theme_index(), hover = uiin_over(list) && uiin_over(row);
            if (hover)
                skin_row_hover(row);
            if (selected)
                skin_fill(ui_rect(row.x, row.y, 4, row.h), cur->accent, 1);
            skin_swatches(ui_rect(row.x + 10, row.y + 7, 44, row.h - 14), t);
            /* the column says the family: HALO-LIGHT-AZURE reads AZURE, LIGHT beside it */
            const char *short_name = theme_short_name(t);
            size_t fl = strlen(k_families[f].title);
            if (!strncmp(short_name, k_families[f].title, fl) && short_name[fl] == '-')
                short_name += fl + 1;
            bool light = !strncmp(short_name, "LIGHT-", 6);
            if (light)
                short_name += 6;
            TTF_Font *nf = selected ? fo->bold : fo->body;
            float nx = row.x + 64, room = row.x + row.w - 4 - nx;
            char buf[96];
            const char *name = skin_fit(nf, short_name, room, buf, sizeof buf);
            float ny = row.y + (row.h - skin_text_height(nf)) * 0.5f;
            float nw = skin_text(nf, name, nx, ny, selected ? TONE_ACCENT : TONE_TEXT, ALIGN_LEFT);
            if (light && nw + 8 + skin_text_width(fo->small, "LIGHT") <= room)
                skin_text(fo->small, "LIGHT", nx + nw + 8, row.y + (row.h - skin_text_height(fo->small)) * 0.5f,
                          TONE_MUTED, ALIGN_LEFT);
            if (hover && uiin_clicked(row))
                clicked = i;
            cy += row_h;
        }
    }
    gl2d_clip_pop();
    if (uiin_clicked(back))
        a->screen = SCREEN_MENU;
    else if (uiin_clicked(full))
        app_do(a, ACT_FULLSCREEN);
    else if (clicked >= 0)
        shell_theme_pick(clicked);
}
