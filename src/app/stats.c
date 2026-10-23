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

/* The statistics screen: the world's story since genesis (app/history.c)
 * as charts in the SYN-OS skin. Life and death, the air, the bodies by
 * colour, what happens a time unit, and what only a third dimension has:
 * how many bodies stay flat and how level they lie. */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/app_internal.h"
#include "ui/input.h"
#include "ui/shell.h"

typedef double (*Get)(const Sample *s, int series);

typedef struct {
    const char *label;
    Rgb colour;
} Key;

static float mid(TTF_Font *f, const char *s, float x, float cy, Rgb c, Align al)
{
    return skin_text_rgb(f, s, x, cy - skin_text_height(f) * 0.5f, c.r, c.g, c.b, al);
}

static void axis_value(double v, char *out, size_t n)
{
    if (v >= 10000)
        snprintf(out, n, "%.0fk", v / 1000);
    else if (v >= 100 || v == floor(v))
        snprintf(out, n, "%.0f", v);
    else
        snprintf(out, n, "%.2g", v);
}

/* A round top for an axis: 1, 2 or 5 times a power of ten. */
static double nice_max(double v)
{
    if (v <= 0)
        return 1;
    double p = pow(10, floor(log10(v)));
    for (int k = 0; k < 3; k++) {
        double m = (k == 0 ? 1 : k == 1 ? 2 : 5) * p;
        if (m >= v)
            return m;
    }
    return 10 * p;
}

/* One chart in its well: a line per series, or the series stacked. */
static void chart(UIRect r, const char *title, const History *h, const Key *keys, int nk, Get get,
                  bool stacked, double fixed_max, const char *hover_tip)
{
    const Fonts *fo = shell_fonts();
    skin_well(r);
    Rgb muted = skin_tone(TONE_MUTED);
    mid(fo->mono_small_bold, title, r.x + 10, r.y + 13, skin_tone(TONE_ACCENT), ALIGN_LEFT);
    /* the key, right to left */
    float kx = r.x + r.w - 10;
    for (int k = nk - 1; k >= 0; k--) {
        kx -= mid(fo->small, keys[k].label, kx, r.y + 13, muted, ALIGN_RIGHT) + 6;
        skin_fill(ui_rect(kx - 10, r.y + 8, 10, 10), keys[k].colour, 1);
        kx -= 22;
    }
    UIRect p = {r.x + 46, r.y + 28, r.w - 56, r.h - 46};
    if (p.w < 40 || p.h < 30 || h->n < 1)
        return;
    double top = fixed_max;
    if (top <= 0) {
        for (int i = 0; i < h->n; i++) {
            double sum = 0;
            for (int k = 0; k < nk; k++) {
                double v = get(&h->s[i], k);
                sum = stacked ? sum + v : fmax(sum, v);
            }
            top = fmax(top, sum);
        }
        top = nice_max(top);
    }
    for (int g = 0; g <= 4; g++) {
        float y = p.y + p.h - p.h * (float)g / 4;
        skin_line(p.x, y, p.x + p.w, y, 1, skin_theme()->border, g ? 0.25f : 0.6f);
        char v[16];
        axis_value(top * g / 4, v, sizeof v);
        mid(fo->mono_small, v, p.x - 6, y, muted, ALIGN_RIGHT);
    }
    char t0[24], t1[32];
    snprintf(t0, sizeof t0, "%.0f", h->s[0].t);
    snprintf(t1, sizeof t1, "time %.0f", h->s[h->n - 1].t);
    mid(fo->mono_small, t0, p.x, p.y + p.h + 9, muted, ALIGN_LEFT);
    mid(fo->mono_small, t1, p.x + p.w, p.y + p.h + 9, muted, ALIGN_RIGHT);
    if (h->n < 2)
        return;
    float span = (float)(h->n - 1);
    if (stacked) {
        /* a column of colour per pixel (or per sample, if fewer) */
        int cols = (int)p.w < h->n ? (int)p.w : h->n;
        float cw = p.w / (float)cols;
        for (int c = 0; c < cols; c++) {
            int i = (int)((double)c / (cols > 1 ? cols - 1 : 1) * (h->n - 1) + 0.5);
            float y = p.y + p.h;
            for (int k = 0; k < nk; k++) {
                float hh = (float)(get(&h->s[i], k) / top) * p.h;
                if (hh <= 0)
                    continue;
                skin_fill(ui_rect(p.x + c * cw, y - hh, cw + 0.5f, hh), keys[k].colour, 0.9f);
                y -= hh;
            }
        }
    } else {
        int step = h->n > (int)p.w ? h->n / (int)p.w : 1;
        for (int k = 0; k < nk; k++) {
            float px = 0, py = 0;
            for (int i = 0; i < h->n; i += step) {
                float x = p.x + p.w * (float)i / span;
                float y = p.y + p.h - (float)(fmin(get(&h->s[i], k), top) / top) * p.h;
                if (i)
                    skin_line(px, py, x, y, 1.8f, keys[k].colour, 1);
                px = x;
                py = y;
            }
        }
    }
    if (hover_tip && uiin_over(r))
        uiin_tooltip(hover_tip);
}

static double get_life(const Sample *s, int k)
{
    return k == 0 ? s->population : s->corpses;
}

static double get_air(const Sample *s, int k)
{
    return k == 0 ? s->o2 : s->co2;
}

static const int k_colour_order[7] = {COL_GREEN, COL_RED, COL_WHITE, COL_GRAY,
                                      COL_BLUE,  COL_CYAN, COL_YELLOW};

static double get_colour(const Sample *s, int k)
{
    return s->colour[k_colour_order[k]];
}

static double get_events(const Sample *s, int k)
{
    switch (k) {
    case 0: return s->births;
    case 1: return s->deaths;
    case 2: return s->kills;
    default: return s->infections;
    }
}

static double get_depth(const Sample *s, int k)
{
    return k == 0 ? s->flat : s->level;
}

static Rgb colour_of(int c)
{
    uint8_t r, g, b;
    bio_color_rgb(c, &r, &g, &b);
    return (Rgb){r, g, b};
}

void stats_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    const History *hs = &a->hist;
    char sub[96];
    snprintf(sub, sizeof sub, "%d samples, %d time unit%s each", hs->n, hs->stride,
             hs->stride == 1 ? "" : "s");
    UIRect in = screen_panel(w, h, a->hints, "STATISTICS", sub);
    const Theme *t = skin_theme();
    Rgb accent = skin_tone(TONE_ACCENT), muted = skin_tone(TONE_MUTED);
    Rgb good = skin_tone(TONE_GOOD), bad = skin_tone(TONE_BAD), warn = skin_tone(TONE_WARN);

    float gap = 10, cw = (in.w - gap) / 2, ch = (in.h - 2 * gap) / 3;
    UIRect c[6];
    for (int i = 0; i < 6; i++)
        c[i] = ui_rect(in.x + (i % 2) * (cw + gap), in.y + (i / 2) * (ch + gap), cw, ch);

    Key life[] = {{"alive", accent}, {"dead", muted}};
    chart(c[0], "LIFE", hs, life, 2, get_life, false, 0, "Organisms alive, and corpses still decaying");
    Key air[] = {{"O2", t->text}, {"CO2", accent}};
    chart(c[1], "THE AIR", hs, air, 2, get_air, false, 0,
          "O2 is the energy all bodies hold; CO2 is what green segments live on. They always add "
          "up to the same.");
    Key cols[7];
    for (int k = 0; k < 7; k++)
        cols[k] = (Key){bio_color_name(k_colour_order[k]), colour_of(k_colour_order[k])};
    chart(c[2], "BODIES BY COLOUR", hs, cols, 7, get_colour, true, 0,
          "The living's segment length, by colour");
    Key ev[] = {{"births", good}, {"deaths", muted}, {"kills", bad}, {"infections", warn}};
    chart(c[3], "A TIME UNIT", hs, ev, 4, get_events, false, 0,
          "What happens in a time unit (256 world frames)");
    Key dims[] = {{"flat bodies", accent}, {"lying level", t->text}};
    chart(c[4], "THE THIRD DIMENSION", hs, dims, 2, get_depth, false, 1.0,
          "The share of the living whose genes are all flat (0.9's), and how level they lie: 1 "
          "flat to the floor, 0.5 any way at all");

    /* now, in figures */
    UIRect f = c[5];
    skin_well(f);
    mid(fo->mono_small_bold, "NOW", f.x + 10, f.y + 13, accent, ALIGN_LEFT);
    const World *wd = &a->w;
    const Sample *last = hs->n ? &hs->s[hs->n - 1] : NULL;
    double total = 0;
    for (int k = 0; k < COL_COUNT && last; k++)
        if (k != COL_BROWN)
            total += last->colour[k];
    char lines[8][96];
    int nl = 0;
    snprintf(lines[nl++], 96, "time %lld   frame %lld", (long long)(wd->frame / 256),
             (long long)wd->frame);
    snprintf(lines[nl++], 96, "%d alive, %d dead   in %d x %d x %d", wd->population,
             world_n_corpses(wd), wd->width, wd->height, wd->depth);
    snprintf(lines[nl++], 96, "O2 %.0f   CO2 %.0f   (always %.0f)", wd->o2, wd->co2, wd->o2 + wd->co2);
    snprintf(lines[nl++], 96, "since genesis: %lld births, %lld deaths", (long long)wd->counts.births,
             (long long)wd->counts.deaths);
    snprintf(lines[nl++], 96, "%lld kills, %lld infections, %lld bites", (long long)wd->counts.kills,
             (long long)wd->counts.infections, (long long)wd->counts.bites);
    if (last && total > 0)
        snprintf(lines[nl++], 96, "green %.0f%%   hunters (red, gray, white) %.0f%%",
                 100 * last->colour[COL_GREEN] / total,
                 100 * (last->colour[COL_RED] + last->colour[COL_GRAY] + last->colour[COL_WHITE]) / total);
    if (last)
        snprintf(lines[nl++], 96, "flat bodies %.0f%%   lying level %.2f", 100 * last->flat, last->level);
    float lh = skin_text_height(fo->mono_small) + 6;
    for (int i = 0; i < nl; i++)
        skin_text(fo->mono_small, lines[i], f.x + 12, f.y + 30 + lh * (float)i,
                  i == 0 ? TONE_TEXT : TONE_MUTED, ALIGN_LEFT);
}
