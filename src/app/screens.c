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

/* The screens under the bar, as panels over the running world: the being
 * library and the parameters. */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app/app_internal.h"
#include "guide/kind.h"
#include "sim/params.h"
#include "ui/input.h"
#include "ui/shell.h"

static float text_w(TTF_Font *f, const char *s)
{
    return s && s[0] ? skin_text_width(f, s) : 0;
}

static float mid(TTF_Font *f, const char *s, float x, float cy, Rgb c, Align al)
{
    return skin_text_rgb(f, s, x, cy - skin_text_height(f) * 0.5f, c.r, c.g, c.b, al);
}

/* text wrapped to width w from (x, y), at most lines lines; the height used */
static float wrap(TTF_Font *f, const char *text, float x, float y, float w, int lines, Rgb c)
{
    float lh = skin_text_height(f) + 3;
    const char *p = text;
    int ln = 0;
    while (*p && ln < lines) {
        char line[400] = "";
        size_t n = 0;
        const char *q = p;
        while (*q) {
            const char *wd = q + strspn(q, " "), *we = wd + strcspn(wd, " ");
            char cand[400];
            snprintf(cand, sizeof cand, "%.*s%s%.*s", (int)n, line, n ? " " : "", (int)(we - wd),
                     wd);
            if (n && text_w(f, cand) > w)
                break;
            n = strlen(cand) < sizeof line - 1 ? strlen(cand) : sizeof line - 1;
            memcpy(line, cand, n);
            line[n] = 0;
            q = we;
        }
        skin_text_rgb(f, line, x, y + (float)ln * lh, c.r, c.g, c.b, ALIGN_LEFT);
        p = q + strspn(q, " ");
        ln++;
    }
    return (float)ln * lh;
}

/* The screen's panel under the bar, with a title bar; returns its inside. */
static UIRect panel(int w, int h, bool hints, const char *title, const char *subtitle)
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

static bool button(UIRect b, const char *icon, const char *label, bool disabled, const char *tip)
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

/* A genetic code's adult body in box, turning about its axis and tilted
 * toward the eye, in 0.9's colours. */
static void draw_code(const GeneticCode *gc, UIRect box, double seconds)
{
    int n = gc->ngenes * gc->symmetry;
    V3 *bs = malloc((size_t)n * 2 * sizeof *bs);
    if (!bs)
        return;
    V3 *be = bs + n;
    gc_body_shape(gc, 1.0, bs, be);
    V3 lo = v3(1e30, 1e30, 1e30), hi = v3(-1e30, -1e30, -1e30);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 2; k++) {
            V3 p = k ? be[i] : bs[i];
            lo = v3(fmin(lo.x, p.x), fmin(lo.y, p.y), fmin(lo.z, p.z));
            hi = v3(fmax(hi.x, p.x), fmax(hi.y, p.y), fmax(hi.z, p.z));
        }
    V3 c = v3_scale(v3_add(lo, hi), 0.5);
    double rad = 1;
    for (int i = 0; i < n; i++)
        rad = fmax(rad, fmax(v3_len(v3_sub(bs[i], c)), v3_len(v3_sub(be[i], c))));
    double turn = seconds * 0.6, tilt = 0.55;
    double ct = cos(turn), st = sin(turn), cx = cos(tilt), sx = sin(tilt);
    double scale = (fmin(box.w, box.h) * 0.5 - 10) / rad;
    float mx = box.x + box.w * 0.5f, my = box.y + box.h * 0.5f;
    for (int i = 0; i < n; i++) {
        float sxy[2][2];
        for (int k = 0; k < 2; k++) {
            V3 p = v3_sub(k ? be[i] : bs[i], c);
            double x = p.x * ct - p.y * st, y = p.x * st + p.y * ct;
            double yy = y * cx - p.z * sx;
            sxy[k][0] = mx + (float)(x * scale);
            sxy[k][1] = my - (float)(yy * scale);
        }
        uint8_t r, g, b;
        bio_color_rgb(gc->genes[i % gc->ngenes].color, &r, &g, &b);
        skin_line(sxy[0][0], sxy[0][1], sxy[1][0], sxy[1][1], 2.5f, (Rgb){r, g, b}, 1);
    }
    free(bs);
}

/* ---- the library ---- */

void library_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    if (!a->lib_read) {
        library_scan(&a->lib);
        a->lib_read = true;
        if (a->lib_folder >= a->lib.nfolders)
            a->lib_folder = 0;
        /* the first folder with anything in it */
        if (a->lib.nfolders && !a->lib.folders[a->lib_folder].count)
            for (int i = 0; i < a->lib.nfolders; i++)
                if (a->lib.folders[i].count) {
                    a->lib_folder = i;
                    break;
                }
        a->lib_sel = -1;
    }
    if (a->lib_sel < 0 && a->lib.nfolders && a->lib.folders[a->lib_folder].count)
        a->lib_sel = a->lib.folders[a->lib_folder].first;
    char sub[64];
    snprintf(sub, sizeof sub, "%d beings", a->lib.nbeings);
    UIRect in = panel(w, h, a->hints, "LIBRARY", sub);
    const Theme *t = skin_theme();

    /* the folders */
    float fw = 190, row = 30;
    for (int i = 0; i < a->lib.nfolders; i++) {
        const Folder *f = &a->lib.folders[i];
        UIRect r = {in.x, in.y + (float)i * (row + 4), fw, row};
        bool hv = uiin_over(r), on = i == a->lib_folder;
        Tone tone = skin_tab_chrome(r, (WidgetState){.hover = hv, .active = on});
        Rgb c = skin_tone(tone);
        mid(fo->bold, f->name, r.x + 12, r.y + row * 0.5f, c, ALIGN_LEFT);
        char n[16];
        snprintf(n, sizeof n, "%d", f->count);
        mid(fo->mono_small, n, r.x + r.w - 10, r.y + row * 0.5f, skin_tone(TONE_MUTED), ALIGN_RIGHT);
        if (hv) {
            uiin_tooltip(f->dir);
            if (uiin_clicked(r)) {
                a->lib_folder = i;
                a->lib_sel = -1;
                a->lib_scroll = 0;
            }
        }
    }
    float fy = in.y + (float)a->lib.nfolders * (row + 4) + 8;
    skin_text(fo->small, "Kept: this program's.", in.x, fy, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->small, "The rest: SYN-BIOGENESIS's", in.x, fy + 18, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->small, "0.9 beings, read as they are.", in.x, fy + 36, TONE_MUTED, ALIGN_LEFT);
    if (button(ui_rect(in.x, in.y + in.h - 32, fw, 32), ICON_ROTATE, "Read again", false,
               "Read every folder again"))
        a->lib_read = false;

    /* the beings in it */
    float lx = in.x + fw + 14, lw = fmaxf(260, (in.w - fw - 14) * 0.48f);
    UIRect list = {lx, in.y, lw, in.h};
    skin_well(list);
    const Folder *fo_ = a->lib.nfolders ? &a->lib.folders[a->lib_folder] : NULL;
    float lrow = 46;
    if (fo_) {
        if (uiin_over(list))
            a->lib_scroll -= uiin_wheel() * lrow;
        float most = fmaxf(0, (float)fo_->count * lrow - list.h + 8);
        a->lib_scroll = fminf(fmaxf(a->lib_scroll, 0), most);
        if (!fo_->count)
            skin_text(fo->small, fo_->ours ? "Nothing kept yet: Keep on an organism's card."
                                           : "No 0.9 beings here.",
                      list.x + 12, list.y + 12, TONE_MUTED, ALIGN_LEFT);
        for (int i = 0; i < fo_->count; i++) {
            float y = list.y + 4 + (float)i * lrow - a->lib_scroll;
            if (y + lrow < list.y || y > list.y + list.h - lrow)
                continue;
            const Being *b = &a->lib.beings[fo_->first + i];
            UIRect r = {list.x + 4, y, list.w - 8, lrow - 4};
            bool hv = uiin_over(r), on = fo_->first + i == a->lib_sel;
            if (on)
                skin_fill(r, t->accent, 0.85f);
            else if (hv)
                skin_fill(r, t->panel_hover, 0.8f);
            Rgb fg = on ? skin_tone(TONE_ON_ACCENT) : t->text;
            Rgb mu = on ? skin_tone(TONE_ON_ACCENT) : skin_tone(TONE_MUTED);
            char fit[128];
            skin_text_rgb(fo->mono_small_bold, skin_fit(fo->mono_small_bold, b->name, r.w - 110, fit, sizeof fit),
                          r.x + 10, r.y + 5, fg.r, fg.g, fg.b, ALIGN_LEFT);
            skin_text_rgb(fo->small, skin_fit(fo->small, b->kind, r.w - 20, fit, sizeof fit), r.x + 10,
                          r.y + 23, mu.r, mu.g, mu.b, ALIGN_LEFT);
            char tag[32];
            snprintf(tag, sizeof tag, "%s  %dx%d", gc_is_flat(b->gc) ? "flat" : "bent",
                     b->gc->ngenes, b->gc->symmetry);
            skin_text_rgb(fo->mono_small, tag, r.x + r.w - 10, r.y + 5, mu.r, mu.g, mu.b, ALIGN_RIGHT);
            if (hv && uiin_clicked(r))
                a->lib_sel = fo_->first + i;
        }
    }

    /* the one chosen */
    float dx = lx + lw + 14, dw = in.x + in.w - dx;
    if (a->lib_sel < 0 || a->lib_sel >= a->lib.nbeings || dw < 200) {
        skin_text(fo->body, "Choose a being.", dx, in.y + 8, TONE_MUTED, ALIGN_LEFT);
        return;
    }
    const Being *b = &a->lib.beings[a->lib_sel];
    float side = fminf(dw, in.h * 0.5f);
    UIRect box = {dx, in.y, dw, side};
    skin_well(box);
    draw_code(b->gc, box, a->seconds);
    float y = in.y + side + 10;
    skin_text(fo->heading, b->name, dx, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->heading) + 4;
    skin_text(fo->small_bold, b->kind, dx, y, TONE_ACCENT, ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 6;
    char sentence[256];
    syn_kind_describe(b->gc, sentence, sizeof sentence);
    y += wrap(fo->small, sentence, dx, y, dw, 3, skin_tone(TONE_MUTED)) + 4;
    if (b->origin[0])
        y += wrap(fo->small, b->origin, dx, y, dw, 2, skin_tone(TONE_MUTED)) + 4;
    char shape[96];
    snprintf(shape, sizeof shape, "%d genes x %d arms, %s, %s", b->gc->ngenes, b->gc->symmetry,
             b->gc->mirror ? "mirrored" : "not mirrored",
             gc_is_flat(b->gc) ? "flat as in 0.9" : "bent");
    skin_text(fo->small, shape, dx, y, TONE_MUTED, ALIGN_LEFT);

    float bw = (dw - 8) / 2, by = in.y + in.h - 34;
    if (button(ui_rect(dx, by, bw, 34), ICON_PLUS, "Into this world", false,
               "Ten of it, at random places in the running world")) {
        GeneticCode *gc = gc_ref(b->gc);
        int placed = world_add_founders(&a->w, gc, 10);
        gc_unref(gc);
        interface_note(a, false, "%d of %s put into the world", placed, b->name);
        a->screen = SCREEN_WORLD;
    }
    if (button(ui_rect(dx + bw + 8, by, bw, 34), ICON_DICE, "New world with it", false,
               "A new world from this seed, with ten of it among the founders")) {
        static char path[1024];
        snprintf(path, sizeof path, "%s", b->path);
        a->wo.import = path;
        a->wo.copies = 10;
        if (app_make_world(a)) {
            interface_note(a, false, "Seed %lld with ten of %s", (long long)a->wo.seed, b->name);
            app_do(a, ACT_HOME);
        }
        a->screen = SCREEN_WORLD;
    }
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
    PD_(initial_co2, "Carbon (CO2)", "WORLD", 500, 0, 1e9, true),
    PD_(segment_radius, "Segment thickness", "WORLD", 0.25, 0.25, 20, true),
    PI_(pitch_prob, "Genes that bend (%)", "WORLD", 5, 0, 100, false),
    PI_(max_age, "Life (time units)", "LIFE", 1, 1, 1000, false),
    PD_(mutation_rate, "Mutation rate", "LIFE", 0.01, 0, 1, false),
    PI_(initial_energy, "A founder's energy", "LIFE", 5, 1, 10000, false),
    PI_(segment_cost_divisor, "Upkeep divisor", "LIFE", 500, 1, 1000000, false),
    PD_(decay_energy, "Corpse decay", "LIFE", 0.05, 0, 100, false),
    PI_(green_obtained_energy_divisor, "Light divisor", "LIFE", 50, 1, 100000, false),
    PI_(drain_subs_divisor, "CO2 divisor", "LIFE", 500, 1, 1000000, false),
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
    PD_(green_energy_consumption, "Green", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(red_energy_consumption, "Red", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(cyan_energy_consumption, "Cyan", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(blue_energy_consumption, "Blue", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(white_energy_consumption, "White", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(gray_energy_consumption, "Gray", "WHAT A USE COSTS", 0.1, 0, 100, false),
    PD_(yellow_energy_consumption, "Yellow", "WHAT A USE COSTS", 0.1, 0, 100, false),
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

bool params_set_text(const char *name, const char *value, char *err, int errlen)
{
    for (int i = 0; i < NPARAMS; i++) {
        if (strcasecmp(k_params[i].name, name))
            continue;
        char *end;
        double v = strtod(value, &end);
        if (end == value || *end || v < k_params[i].min || v > k_params[i].max) {
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
    UIRect in = panel(w, h, a->hints, "PARAMETERS", "0.9's, and the box's");
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
            char v[32];
            param_text(d, v, sizeof v);
            float bx = r.x + r.w - 2 * 28 - 4;
            mid(fo->mono_small_bold, v, bx - 10, r.y + r.h * 0.5f, skin_tone(TONE_TEXT), ALIGN_RIGHT);
            if (button(ui_rect(bx, r.y + 1, 26, r.h - 2), ICON_MINUS, "", false, NULL))
                param_put(d, param_get(d) - d->step);
            if (button(ui_rect(bx + 28, r.y + 1, 26, r.h - 2), ICON_PLUS, "", false, NULL))
                param_put(d, param_get(d) + d->step);
            if (uiin_over(ui_rect(r.x, r.y, r.w - 60, r.h))) {
                static char tip[96];
                snprintf(tip, sizeof tip, "%s   (console: set %s VALUE)", d->name, d->name);
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
    if (button(ui_rect(in.x + in.w - bw, by, bw, 34), ICON_DICE, "New world with these", false,
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
    if (button(ui_rect(in.x + in.w - 2 * bw - 8, by, bw, 34), ICON_ROTATE, "Defaults", false,
               "Every parameter back to its default: 0.9's, and the box's (the world keeps going)"))
        params_set_defaults(&P);
}
