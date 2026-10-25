/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/stats_screen.c, the SYN edition of its C port
 * of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* The statistics screen, as SYN-BIOGENESIS's: OVERVIEW (the world now,
 * its colours, the champions alive and the records), CHARTS (the run's
 * measures, the water's own with them), EPOCHS (each one's best species and
 * champions), FAMILY (the family tree, app/family_view.c) and TIMELINE
 * (the lines' river and what happened). 1 to 5 pick a tab. A being shown
 * opens in the laboratory on a click; a right click offers its menu: the
 * laboratory, copy it, keep it in the library. */

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app/app_internal.h"
#include "app/family_view.h"
#include "guide/palette.h"
#include "syn/family.h"
#include "syn/history.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

typedef enum { TAB_OVERVIEW, TAB_CHARTS, TAB_EPOCHS, TAB_FAMILY, TAB_TIMELINE, TAB_COUNT } Tab;
static const char *const k_tabs[TAB_COUNT] = {"OVERVIEW", "CHARTS", "EPOCHS", "FAMILY", "TIMELINE"};

#define PAD 24.0f
#define GAP 14.0f

static struct {
    Tab tab;
    float scroll[TAB_COUNT];
    float content_h; /* the tab's height last frame, for the scrollbar */
    bool thumb_drag;
    float thumb_grab;
    int range;      /* charts: all, the last quarter, the last twentieth */
    int span;       /* epochs: 0 automatic, else that many base epochs */
    bool oldest;    /* epochs and events: oldest first */
    bool share;     /* the river as shares of the living, not counts */
    int filter;     /* events: -1 all, else an EventKind */
    int64_t hover_t, hover_next; /* the time hovered on any chart (-1: none) */
    int64_t mark_t;              /* an event clicked on the timeline */
} S = {.filter = -1, .hover_t = -1, .mark_t = -1};

/* ---- small helpers ---- */

static int g_mx, g_my;
static UIRect g_view; /* the scrolling area: things outside it can't be hovered */

/* The mouse is over r, on this screen (not on a window above it). */
static bool over(UIRect r)
{
    return uiin_over(r);
}

static bool hot(UIRect r)
{
    return over(r) && over(g_view);
}

static bool visible(UIRect r)
{
    return r.y + r.h > g_view.y && r.y < g_view.y + g_view.h;
}

static bool button(UIRect r, const char *label, bool active)
{
    bool h = over(r);
    skin_button(r, shell_fonts()->bold, label, (WidgetState){.hover = h, .active = active}, true);
    return h && uiin_clicked(r);
}

static SDL_Color tone(Tone t, Uint8 a)
{
    SDL_Color c = skin_tone_color(t);
    c.a = a;
    return c;
}

/* a palette slot's (syn/palette.h) */
static SDL_Color bio(int slot, Uint8 a)
{
    uint8_t r, g, b;
    syn_col_rgb(slot, &r, &g, &b);
    return (SDL_Color){r, g, b, a};
}

static void text_col(TTF_Font *f, const char *s, float x, float y, SDL_Color c, Align al)
{
    skin_text_rgb(f, s, x, y, c.r, c.g, c.b, al);
}

static void tnum(char *out, size_t n, int64_t t)
{
    char v[24];
    fmt_num(v, sizeof v, (double)t);
    snprintf(out, n, "t %s", v);
}

static const char *slot_name(int slot)
{
    return syn_col_name(slot);
}

/* Filled triangles between two polylines, no texture. */
static void band(const float *x, const float *ytop, const float *ybot, int n, SDL_Color c)
{
    if (n < 2)
        return;
    SDL_Vertex *v = malloc((size_t)n * 2 * sizeof *v);
    int *ix = malloc((size_t)(n - 1) * 6 * sizeof *ix);
    if (!v || !ix) {
        free(v);
        free(ix);
        return;
    }
    for (int i = 0; i < n; i++) {
        v[2 * i] = (SDL_Vertex){{x[i], ytop[i]}, c, {0, 0}};
        v[2 * i + 1] = (SDL_Vertex){{x[i], ybot[i]}, c, {0, 0}};
    }
    for (int i = 0; i < n - 1; i++) {
        int *q = ix + 6 * i, a = 2 * i;
        q[0] = a;
        q[1] = a + 1;
        q[2] = a + 2;
        q[3] = a + 2;
        q[4] = a + 1;
        q[5] = a + 3;
    }
    float al = skin_alpha();
    for (int i = 0; i < n * 2; i++)
        v[i].color.a = (Uint8)lroundf((float)v[i].color.a * al);
    gl2d_geometry(v, n * 2, ix, (n - 1) * 6);
    free(v);
    free(ix);
}

static void polyline(const float *x, const float *y, int n, float hw, SDL_Color c)
{
    for (int i = 0; i + 1 < n; i++)
        skin_line(x[i], y[i], x[i + 1], y[i + 1], 2 * hw, (Rgb){c.r, c.g, c.b}, (float)c.a / 255.0f);
}

/* ---- an organism's body, from its code ---- */

static void code_thumb(App *a, UIRect r, GeneticCode *gc, const char *tip)
{
    skin_fill(r, (Rgb){0, 0, 0}, 1);
    if (!gc)
        return;
    gl2d_clip_push(r.x, r.y, r.w, r.h);
    screen_code(gc, r, 0);
    gl2d_clip_pop();
    if (hot(r)) {
        skin_fill(r, skin_theme()->accent, 0.12f);
        if (tip)
            uiin_tooltip(tip);
        if (uiin_clicked(r)) {
            lab_open(a, gc);
        } else if (uiin_right_clicked(r)) {
            char origin[200];
            snprintf(origin, sizeof origin, "the statistics: %.170s", tip ? tip : "a record");
            app_being_menu(a, gc, "CHAMPION", origin, "Saved");
        }
    }
}

/* A line by the name of the family its root began ("VELORAX"), else "#"
 * and its founder's number. */
static void line_label(const Line *l, char *out, size_t n)
{
    const Species *head = syn_family_head(syn_family_line(l->id));
    if (head && head->name_no)
        syn_family_title(head->name_no, out, n);
    else
        snprintf(out, n, "#%d", l->founder);
}

/* A champion: what it is the best at, its body, its figure. */
static void champion_tile(App *a, UIRect r, ChampionKind k, const Champion *c, const char *title)
{
    const Fonts *fo = shell_fonts();
    char buf[64];
    float y = r.y;
    skin_text(fo->small_bold, skin_fit(fo->small_bold, title, r.w, buf, sizeof buf), r.x, y,
              TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 2;
    float side = fminf(r.w, r.h - (y - r.y) - 40);
    UIRect th = {r.x, y, side, side};
    char tip[200] = "";
    if (c->gc) {
        const Line *l = syn_history_line(c->line);
        char lt[48] = "", ln[40];
        if (l) {
            line_label(l, ln, sizeof ln);
            snprintf(lt, sizeof lt, ", line %s", ln);
        }
        char v[32];
        syn_champion_value(k, c->value, v, sizeof v);
        snprintf(tip, sizeof tip, "Organism #%d, generation %d%s: %s at t %lld%s. Click for its genes, right click for its menu.",
                 c->id, c->generation, lt, v, (long long)c->t, c->died ? ", as it died" : "");
    }
    code_thumb(a, th, c->gc, c->gc ? tip : NULL);
    y += side + 4;
    syn_champion_value(k, c->gc ? c->value : -1, buf, sizeof buf);
    skin_text(fo->bold, buf, r.x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->bold) - 2;
    if (c->gc) {
        snprintf(buf, sizeof buf, "#%d  g%d%s", c->id, c->generation, c->died ? "  \xe2\x80\xa0" : "");
        skin_text(fo->small, buf, r.x, y, TONE_MUTED, ALIGN_LEFT);
    }
}

/* ---- charts ---- */

typedef struct {
    size_t off;
    int colour; /* Tone (>= 0) or -1 - BioColor */
    const char *name;
} Ser;

typedef struct {
    const char *title, *unit;
    int nser;
    Ser ser[2];
    bool colours; /* the living colours, stacked */
} ChartDef;

#define F(field) offsetof(HistSample, field)
static const ChartDef k_charts[] = {
    {"POPULATION", "", 2, {{F(pop), TONE_ACCENT, "living"}, {F(corpses), -1 - SYN_DEAD, "corpses"}}, false},
    {"BIRTHS AND DEATHS", "per unit", 2, {{F(births), TONE_GOOD, "births"}, {F(deaths), TONE_BAD, "deaths"}}, false},
    {"INFECTIONS", "per unit", 1, {{F(infections), TONE_WARN, "infections"}}, false},
    {"OXYGEN", "O2 in the air", 1, {{F(o2), 100, "O2"}}, false},
    {"CARBON DIOXIDE", "CO2 in the air", 1, {{F(co2), TONE_TEXT, "CO2"}}, false},
    {"METHANE", "CH4 in the water", 1, {{F(ch4), TONE_WARN, "CH4"}}, false},
    {"COLOURS OF THE LIVING", "share of segments", 0, {{0}}, true},
    {"GENERATIONS", "", 2, {{F(gen_mean), TONE_ACCENT, "mean"}, {F(gen_max), TONE_MUTED, "highest"}}, false},
    {"LINES LIVING", "families with a living member", 1, {{F(lines), TONE_ACCENT, "lines"}}, false},
    {"ENERGY", "mean per organism", 1, {{F(energy_mean), TONE_WARN, "energy"}}, false},
    {"BIOMASS", "mass of the living", 1, {{F(mass_total), TONE_GOOD, "mass"}}, false},
    {"BODY SIZE", "mean segments", 1, {{F(size_mean), TONE_ACCENT, "segments"}}, false},
    {"AGE", "mean, in units", 1, {{F(age_mean), TONE_MUTED, "age"}}, false},
    {"KILLS AND BITES", "per unit", 2, {{F(kills), TONE_BAD, "kills"}, {F(bites), TONE_WARN, "bites"}}, false},
    {"DEPTH", "where the length lives: 0 the surface, 1 the floor", 2,
     {{F(leaves_depth), -1 - COL_GREEN, "leaves"}, {F(hunters_depth), -1 - COL_RED, "hunters"}}, false},
    {"SHAPE", "of the living: flat bodies, and how level they lie", 2,
     {{F(flat), TONE_ACCENT, "flat"}, {F(level), TONE_MUTED, "level"}}, false},
};
#undef F
#define N_CHARTS (int)(sizeof k_charts / sizeof k_charts[0])

static float val(const HistSample *s, size_t off)
{
    return *(const float *)((const char *)s + off);
}

#define SER_O2 100 /* a blue that reads on dark themes too */

static SDL_Color ser_colour(const Ser *s, Uint8 a)
{
    if (s->colour == SER_O2)
        return skin_theme()->light ? (SDL_Color){40, 80, 220, a} : (SDL_Color){100, 150, 255, a};
    return s->colour >= 0 ? tone((Tone)s->colour, a) : bio(-1 - s->colour, a);
}

static void range_of(const History *hi, int *i0, int *i1)
{
    *i1 = hi->n;
    *i0 = 0;
    if (S.range == 1)
        *i0 = hi->n - (hi->n + 3) / 4;
    else if (S.range == 2)
        *i0 = hi->n - (hi->n + 19) / 20;
    if (*i1 - *i0 < 2)
        *i0 = *i1 - 2 < 0 ? 0 : *i1 - 2;
}

/* A round step for gridlines about a third of the range. */
static double nice_step(double range)
{
    if (range <= 0)
        return 1;
    double raw = range / 3, p = pow(10, floor(log10(raw))), f = raw / p;
    return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * p;
}

static void chart(UIRect card, const ChartDef *d, const History *hi)
{
    const Fonts *fo = shell_fonts();
    const Theme *th = skin_theme();
    skin_panel(card, false);
    int i0, i1;
    range_of(hi, &i0, &i1);
    int n = i1 - i0;
    const HistSample *s = hi->s + i0;
    skin_text(fo->bold, d->title, card.x + 14, card.y + 10, TONE_TEXT, ALIGN_LEFT);
    if (d->unit[0])
        skin_text(fo->small, d->unit, card.x + 14, card.y + 10 + skin_text_height(fo->bold) - 2,
                  TONE_MUTED, ALIGN_LEFT);
    UIRect plot = {card.x + 58, card.y + 52, card.w - 58 - 14, card.h - 52 - 26};
    if (n < 1 || plot.w < 20 || plot.h < 20)
        return;

    /* the latest values, top right */
    float rx = card.x + card.w - 14;
    char v[48];
    for (int k = d->nser - 1; k >= 0; k--) {
        fmt_num(v, sizeof v, val(&s[n - 1], d->ser[k].off));
        char lab[96];
        snprintf(lab, sizeof lab, "%s %s", d->ser[k].name, v);
        float wv = skin_text_width(fo->small_bold, lab);
        text_col(fo->small_bold, lab, rx - wv, card.y + 12, ser_colour(&d->ser[k], 255), ALIGN_LEFT);
        rx -= wv + 16;
    }

    double lo = 0, hi_v = 1;
    if (!d->colours) {
        hi_v = 0;
        for (int i = 0; i < n; i++)
            for (int k = 0; k < d->nser; k++)
                hi_v = fmax(hi_v, val(&s[i], d->ser[k].off));
        if (hi_v <= 0)
            hi_v = 1;
        double step = nice_step(hi_v - lo);
        hi_v = ceil(hi_v / step) * step;
    }
    /* gridlines and their values */
    double step = d->colours ? 0.25 : nice_step(hi_v - lo);
    for (double g = lo; g <= hi_v + step * 0.01; g += step) {
        float y = plot.y + plot.h - (float)((g - lo) / (hi_v - lo)) * plot.h;
        skin_fill(ui_rect(plot.x, y, plot.w, 1), th->border, 0.5f);
        if (d->colours)
            snprintf(v, sizeof v, "%.0f%%", g * 100);
        else
            fmt_num(v, sizeof v, g);
        skin_text(fo->small, v, plot.x - 6, y - skin_text_height(fo->small) / 2, TONE_MUTED,
                  ALIGN_RIGHT);
    }
    char t0[32], t1[32];
    tnum(t0, sizeof t0, s[0].t);
    tnum(t1, sizeof t1, s[n - 1].t);
    float ly = plot.y + plot.h + 4;
    skin_text(fo->small, t0, plot.x, ly, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->small, t1, plot.x + plot.w, ly, TONE_MUTED, ALIGN_RIGHT);

    /* at most a point per pixel column, each the mean of its samples */
    int m = n < (int)plot.w ? n : (int)plot.w;
    if (m < 1)
        m = 1;
    float *xs = malloc((size_t)m * 3 * sizeof *xs);
    if (!xs)
        return;
    float *ya = xs + m, *yb = xs + 2 * m;
    double tspan = n > 1 ? (double)(s[n - 1].t - s[0].t) : 1;
#define B0(j) ((int)((long long)(j) * n / m))
    for (int j = 0; j < m; j++) {
        int a = B0(j), b = B0(j + 1);
        double tm = (s[a].t + s[b - 1].t) / 2.0;
        xs[j] = plot.x + (n > 1 ? (float)((tm - s[0].t) / tspan) * plot.w : plot.w / 2);
    }
    if (d->colours) {
        /* each colour a band, green at the bottom */
        float *acc = calloc((size_t)m, sizeof *acc);
        if (acc) {
            for (int c = 0; c < HIST_COLOURS; c++) {
                for (int j = 0; j < m; j++) {
                    int a = B0(j), b = B0(j + 1);
                    float f = 0;
                    for (int i = a; i < b; i++) {
                        float all = 0;
                        for (int k = 0; k < HIST_COLOURS; k++)
                            all += s[i].seg[k];
                        f += all > 0 ? s[i].seg[c] / all : 0;
                    }
                    f /= (float)(b - a);
                    yb[j] = plot.y + plot.h - acc[j] * plot.h;
                    acc[j] += f;
                    ya[j] = plot.y + plot.h - acc[j] * plot.h;
                }
                band(xs, ya, yb, m, bio(c, 230));
            }
            free(acc);
        }
    } else {
        for (int k = d->nser - 1; k >= 0; k--) {
            for (int j = 0; j < m; j++) {
                int a = B0(j), b = B0(j + 1);
                double sum = 0;
                for (int i = a; i < b; i++)
                    sum += val(&s[i], d->ser[k].off);
                ya[j] = plot.y + plot.h - (float)((sum / (b - a) - lo) / (hi_v - lo)) * plot.h;
                yb[j] = plot.y + plot.h;
            }
            if (k == 0)
                band(xs, ya, yb, m, ser_colour(&d->ser[0], 46));
            polyline(xs, ya, m, 1.25f, ser_colour(&d->ser[k], 255));
        }
    }
#undef B0

    /* hovering any chart marks the same time on all of them */
    if (hot(plot) && n > 1) {
        double f = (g_mx - plot.x) / plot.w;
        int64_t t = s[0].t + (int64_t)llround(f * tspan);
        S.hover_next = t;
    }
    if (S.hover_t >= s[0].t && S.hover_t <= s[n - 1].t && n > 1) {
        int best = 0;
        for (int i = 1; i < n; i++)
            if (llabs(s[i].t - S.hover_t) < llabs(s[best].t - S.hover_t))
                best = i;
        float hx = plot.x + (float)((s[best].t - s[0].t) / tspan) * plot.w;
        skin_fill(ui_rect(hx, plot.y, 1, plot.h), th->text, 0.55f);
        if (hot(plot)) {
            char tip[256], tt[32];
            tnum(tt, sizeof tt, s[best].t);
            int k = snprintf(tip, sizeof tip, "%s", tt);
            if (d->colours) {
                /* the commonest six */
                float all = 0;
                bool used[HIST_COLOURS] = {false};
                for (int c = 0; c < HIST_COLOURS; c++)
                    all += s[best].seg[c];
                for (int n6 = 0; n6 < 6 && k < (int)sizeof tip - 1; n6++) {
                    int top = -1;
                    for (int c = 0; c < HIST_COLOURS; c++)
                        if (!used[c] && s[best].seg[c] > 0 && (top < 0 || s[best].seg[c] > s[best].seg[top]))
                            top = c;
                    if (top < 0)
                        break;
                    used[top] = true;
                    k += snprintf(tip + k, sizeof tip - (size_t)k, "  %s %.0f%%", slot_name(top),
                                  all > 0 ? 100 * s[best].seg[top] / all : 0);
                }
            } else {
                for (int j = 0; j < d->nser && k < (int)sizeof tip - 1; j++) {
                    fmt_num(v, sizeof v, val(&s[best], d->ser[j].off));
                    k += snprintf(tip + k, sizeof tip - (size_t)k, "  %s %s", d->ser[j].name, v);
                }
            }
            uiin_tooltip(tip);
        }
    }
    free(xs);
}

static float tab_charts(App *a, UIRect area, float y, const History *hi)
{
    (void)a;
    int cols = area.w >= 1300 ? 3 : 2;
    float cw = (area.w - GAP * (float)(cols - 1)) / (float)cols, ch = 230;
    for (int i = 0; i < N_CHARTS; i++) {
        UIRect r = {area.x + (float)(i % cols) * (cw + GAP), y + (float)(i / cols) * (ch + GAP), cw, ch};
        if (visible(r))
            chart(r, &k_charts[i], hi);
    }
    return (float)((N_CHARTS + cols - 1) / cols) * (ch + GAP);
}

/* ---- overview ---- */

static float figure(float x, float y, float w, const char *label, const char *value, Tone t)
{
    const Fonts *fo = shell_fonts();
    UIRect r = {x, y, w, 64};
    skin_panel(r, false);
    char buf[64];
    skin_text(fo->small, skin_fit(fo->small, label, w - 24, buf, sizeof buf), x + 12, y + 9,
              TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->big, skin_fit(fo->big, value, w - 24, buf, sizeof buf), x + 12,
              y + 11 + skin_text_height(fo->small) - 4, t, ALIGN_LEFT);
    return 64;
}

static float tab_overview(App *a, UIRect area, float y0, const History *hi)
{
    const Fonts *fo = shell_fonts();
    World *w = &a->w;
    WorldStats *st = &w->stats;
    float y = y0;
    char v[13][64];
    int corpses = 0;
    for (int i = 0; i < w->norgs; i++)
        corpses += !w->orgs[i]->removed && !w->orgs[i]->alive;
    fmt_num(v[0], sizeof v[0], (double)st->time);
    fmt_num(v[1], sizeof v[1], w->population);
    fmt_num(v[2], sizeof v[2], corpses);
    fmt_num(v[3], sizeof v[3], hi->n ? hi->s[hi->n - 1].lines : 0);
    fmt_num(v[4], sizeof v[4], w->o2);
    fmt_num(v[5], sizeof v[5], w->co2);
    fmt_num(v[6], sizeof v[6], w->ch4);
    fmt_num(v[7], sizeof v[7], stats_average_births(st));
    fmt_num(v[8], sizeof v[8], stats_average_deaths(st));
    fmt_num(v[9], sizeof v[9], stats_average_infections(st));
    fmt_num(v[10], sizeof v[10], st->created_organisms);
    fmt_num(v[11], sizeof v[11], st->mass_extinctions);
    char mp[24], mt[24];
    fmt_num(mp, sizeof mp, st->max_population);
    fmt_num(mt, sizeof mt, (double)st->max_population_time);
    snprintf(v[12], sizeof v[12], "%s (t %s)", mp, mt);
    /* the averages are the original's: over the world's whole life; the
     * methane beside the other gases, as SYN-BIOGENESIS shows Color Mod's */
    const char *labels[13] = {"TIME",           "LIVING",         "CORPSES",            "LINES LIVING",
                              "O2",             "CO2",            "CH4",                "AVERAGE BIRTHS",
                              "AVERAGE DEATHS", "AVERAGE INFECTIONS", "ORGANISMS MADE", "MASS EXTINCTIONS",
                              "MOST LIVING"};
    Tone tones[13] = {TONE_TEXT, TONE_TEXT, TONE_TEXT, TONE_TEXT, TONE_TEXT, TONE_TEXT, TONE_TEXT,
                      TONE_GOOD, TONE_BAD,  TONE_WARN, TONE_TEXT, TONE_BAD,  TONE_TEXT};
    int nfig = 13;
    int cols = area.w >= 1100 ? 6 : 4;
    float fw = (area.w - GAP * (float)(cols - 1)) / (float)cols;
    for (int i = 0; i < nfig; i++)
        figure(area.x + (float)(i % cols) * (fw + GAP), y + (float)(i / cols) * (64 + GAP), fw,
               labels[i], v[i], tones[i]);
    y += (float)((nfig + cols - 1) / cols) * (64 + GAP) + 6;

    /* the living colours now */
    skin_text(fo->heading, "COLOURS OF THE LIVING", area.x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->heading) + 6;
    double seg[HIST_COLOURS] = {0}, all = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (o->removed || !o->alive)
            continue;
        for (int s = 0; s < o->segments; s++) {
            int slot = o->seg_color[s];
            if (slot >= 0 && slot < HIST_COLOURS) {
                seg[slot]++;
                all++;
            }
        }
    }
    UIRect bar = {area.x, y, area.w, 22};
    skin_fill(bar, (Rgb){0, 0, 0}, 1);
    float x = bar.x;
    for (int c = 0; c < HIST_COLOURS && all > 0; c++) {
        float bw = (float)(seg[c] / all) * bar.w;
        SDL_Color k = bio(c, 255);
        skin_fill(ui_rect(x, bar.y, bw, bar.h), (Rgb){k.r, k.g, k.b}, 1);
        x += bw;
    }
    y += bar.h + 8;
    x = area.x;
    /* the legend: every colour, or for Color Mod's many the commonest, in
     * rows as wide as there is room */
    int order[HIST_COLOURS], shown = 0;
    for (int c = 0; c < HIST_COLOURS; c++)
        if (HIST_COLOURS <= 12 || seg[c] > 0)
            order[shown++] = c;
    if (HIST_COLOURS > 12) {
        for (int i = 1; i < shown; i++)
            for (int j = i; j > 0 && seg[order[j]] > seg[order[j - 1]]; j--) {
                int t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;
            }
        if (shown > 12)
            shown = 12;
    }
    double listed = 0;
    for (int i = 0; i < shown; i++) {
        int c = order[i];
        listed += seg[c];
        char lab[48];
        snprintf(lab, sizeof lab, "%s %.1f%%", slot_name(c), all > 0 ? 100 * seg[c] / all : 0);
        float lw = 16 + skin_text_width(fo->small, lab) + 18;
        if (x + lw > area.x + area.w) {
            x = area.x;
            y += skin_text_height(fo->small) + 4;
        }
        SDL_Color k = bio(c, 255);
        skin_fill(ui_rect(x, y + 4, 10, 10), (Rgb){k.r, k.g, k.b}, 1);
        skin_text(fo->small, lab, x + 16, y, TONE_TEXT, ALIGN_LEFT);
        x += lw;
    }
    if (all > listed + 0.5) {
        char lab[48];
        snprintf(lab, sizeof lab, "the rest %.1f%%", 100 * (all - listed) / all);
        if (x + skin_text_width(fo->small, lab) > area.x + area.w) {
            x = area.x;
            y += skin_text_height(fo->small) + 4;
        }
        skin_text(fo->small, lab, x, y, TONE_MUTED, ALIGN_LEFT);
    }
    y += skin_text_height(fo->small) + 18;

    /* who is best now, and the records */
    float tile_w = 132, tile_h = 150;
    int per_row = (int)((area.w + GAP) / (tile_w + GAP));
    if (per_row < 3)
        per_row = 3;
    skin_text(fo->heading, "CHAMPIONS ALIVE", area.x, y, TONE_TEXT, ALIGN_LEFT);
    skin_text(fo->small, "the best of the living, now", area.x + area.w, y + 6, TONE_MUTED, ALIGN_RIGHT);
    y += skin_text_height(fo->heading) + 8;
    Champion now[CH_COUNT];
    syn_history_alive_best(w, now);
    for (int k = 0; k < CH_COUNT; k++) {
        UIRect r = {area.x + (float)(k % per_row) * (tile_w + GAP), y + (float)(k / per_row) * (tile_h + GAP),
                    tile_w, tile_h};
        if (visible(r))
            champion_tile(a, r, (ChampionKind)k, &now[k],
                          syn_champion_name((ChampionKind)k));
    }
    y += (float)((CH_COUNT + per_row - 1) / per_row) * (tile_h + GAP) + 6;

    skin_text(fo->heading, "RECORDS", area.x, y, TONE_TEXT, ALIGN_LEFT);
    skin_text(fo->small, "the most any organism has had, as the original keeps them", area.x + area.w,
              y + 6, TONE_MUTED, ALIGN_RIGHT);
    y += skin_text_height(fo->heading) + 8;
    struct {
        const char *title;
        GeneticCode *gc;
        int n;
        int64_t t;
    } rec[] = {{"EVER: CHILDREN", st->most_children, st->most_children_n, st->most_children_time},
               {"EVER: VICTIMS", st->most_kills, st->most_kills_n, st->most_kills_time},
               {"EVER: INFECTED", st->most_infections, st->most_infections_n, st->most_infections_time},
               {"LAST BORN", st->last_born, -1, -1},
               {"LAST DEAD", st->last_dead, -1, -1},
               {"LAST INFECTED", st->last_infected, -1, -1}};
    const Fonts *f = fo;
    for (int k = 0; k < 6; k++) {
        UIRect r = {area.x + (float)(k % per_row) * (tile_w + GAP), y + (float)(k / per_row) * (tile_h + GAP),
                    tile_w, tile_h};
        if (!visible(r))
            continue;
        char buf[64];
        skin_text(f->small_bold, rec[k].title, r.x, r.y, TONE_MUTED, ALIGN_LEFT);
        float side = r.h - 58;
        UIRect thr = {r.x, r.y + skin_text_height(f->small_bold) + 2, side, side};
        code_thumb(a, thr, rec[k].gc,
                   rec[k].gc ? "Click for its genes; right click to copy or keep it" : NULL);
        float ty = thr.y + side + 4;
        if (rec[k].n >= 0) {
            fmt_num(buf, sizeof buf, rec[k].n);
            skin_text(f->bold, buf, r.x, ty, TONE_TEXT, ALIGN_LEFT);
            tnum(buf, sizeof buf, rec[k].t);
            skin_text(f->small, buf, r.x, ty + skin_text_height(f->bold) - 2, TONE_MUTED, ALIGN_LEFT);
        }
    }
    y += (float)((6 + per_row - 1) / per_row) * (tile_h + GAP);
    return y - y0;
}

/* ---- epochs ---- */

static int epoch_span(const History *hi)
{
    int64_t len = hi->t_now - hi->t_start + 1;
    if (S.span > 0)
        return hi->epoch_base * S.span;
    int span = hi->epoch_base;
    while (len / span > 24)
        span *= 2;
    return span;
}

int syn_stats_epoch_span(const History *hi)
{
    return epoch_span(hi);
}

/* An epoch's best species: the most living at once. A click follows it in
 * the FAMILY tab. */
static void species_tile(App *a, UIRect r, const Epoch *ep, uint32_t id)
{
    (void)a;
    const Fonts *fo = shell_fonts();
    char buf[64], v[32];
    float y = r.y;
    skin_text(fo->small_bold, skin_fit(fo->small_bold, "SPECIES", r.w, buf, sizeof buf), r.x, y, TONE_ACCENT,
              ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 2;
    float side = fminf(r.w, r.h - (y - r.y) - 40);
    UIRect th = {r.x, y, side, side};
    const Species *s = ep->best ? syn_family_get(ep->best) : NULL;
    skin_fill(th, (Rgb){0, 0, 0}, 1);
    if (s && s->gc) {
        gl2d_clip_push(th.x, th.y, th.w, th.h);
        screen_code(s->gc, th, 0);
        gl2d_clip_pop();
        if (hot(th)) {
            skin_fill(th, skin_theme()->accent, 0.12f);
            char plan[160], tip[300], nm[64];
            syn_family_plan(s->gc, plan, sizeof plan);
            syn_family_label(s, nm, sizeof nm);
            snprintf(tip, sizeof tip, "The epoch's best species, %s (#%d): %d living at once. %s. Click to see its family.",
                     nm, s->founder, ep->best_n, plan);
            uiin_tooltip(tip);
            if (uiin_clicked(th)) {
                S.tab = TAB_FAMILY;
                syn_family_focus(s->id);
            }
        }
    }
    (void)id;
    y += side + 4;
    if (ep->best_n > 0)
        fmt_num(v, sizeof v, ep->best_n);
    else
        snprintf(v, sizeof v, "-");
    skin_text(fo->bold, v, r.x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->bold) - 2;
    if (ep->best) {
        char nm[56], fit_[64];
        if (s) {
            syn_family_label(s, nm, sizeof nm);
            snprintf(buf, sizeof buf, "%s%s", nm, s->alive > 0 ? "" : "  \xe2\x80\xa0");
        } else {
            snprintf(buf, sizeof buf, "forgotten");
        }
        skin_text(fo->small, skin_fit(fo->small, buf, r.w, fit_, sizeof fit_), r.x, y, TONE_MUTED, ALIGN_LEFT);
    }
}

static float tab_epochs(App *a, UIRect area, float y0, const History *hi)
{
    const Fonts *fo = shell_fonts();
    int span = epoch_span(hi);
    int64_t k_first = hi->t_start / span, k_last = hi->t_now / span;
    int count = (int)(k_last - k_first + 1);
    float card_h = 168;
    float left_w = 270;
    int nt = CH_COUNT + 1; /* the best species, then the champions */
    float tile_w = fminf(132, (area.w - left_w - 28 - GAP * (float)(nt - 1)) / (float)nt);
    for (int i = 0; i < count; i++) {
        int64_t k = S.oldest ? k_first + i : k_last - i;
        UIRect card = {area.x, y0 + (float)i * (card_h + GAP), area.w, card_h};
        if (!visible(card))
            continue;
        Epoch ep;
        bool any = syn_history_merge(k * span, (k + 1) * span, &ep);
        skin_panel(card, false);
        bool now = k == k_last;
        char buf[256], b2[48], b3[48];
        float x = card.x + 16, y = card.y + 12;
        snprintf(buf, sizeof buf, "EPOCH %lld", (long long)(k - k_first + 1));
        float wv = skin_text(fo->heading, buf, x, y, now ? TONE_ACCENT : TONE_TEXT, ALIGN_LEFT);
        if (now)
            skin_text(fo->small_bold, "NOW", x + wv + 10, y + 5, TONE_ACCENT, ALIGN_LEFT);
        y += skin_text_height(fo->heading);
        tnum(b2, sizeof b2, ep.t0);
        fmt_num(b3, sizeof b3, (double)ep.t1);
        snprintf(buf, sizeof buf, "%s to %s", b2, b3);
        skin_text(fo->small, buf, x, y, TONE_MUTED, ALIGN_LEFT);
        y += skin_text_height(fo->small) + 6;
        if (!any) {
            skin_text(fo->small, "not seen (before the history began)", x, y, TONE_MUTED, ALIGN_LEFT);
            continue;
        }
        int dom = 0;
        double segall = 0;
        for (int c = 0; c < HIST_COLOURS; c++) {
            segall += ep.seg[c];
            if (ep.seg[c] > ep.seg[dom])
                dom = c;
        }
        SDL_Color dc = bio(dom, 255);
        skin_fill(ui_rect(x, y + 3, 12, 12), (Rgb){dc.r, dc.g, dc.b}, 1);
        snprintf(buf, sizeof buf, "THE %s EPOCH  %.0f%%", slot_name(dom), segall > 0 ? 100 * ep.seg[dom] / segall : 0);
        for (char *p = buf; *p; p++)
            if (*p >= 'a' && *p <= 'z')
                *p = (char)(*p - 'a' + 'A');
        skin_text(fo->small_bold, buf, x + 18, y, TONE_TEXT, ALIGN_LEFT);
        y += skin_text_height(fo->small_bold) + 4;
        fmt_num(b2, sizeof b2, ep.pop_min == INT_MAX ? 0 : ep.pop_min);
        fmt_num(b3, sizeof b3, ep.pop_max);
        char mean[32];
        fmt_num(mean, sizeof mean, ep.samples ? ep.pop_sum / ep.samples : 0);
        /* each line fitted to the column (Color Mod's worlds have big numbers) */
        float room = left_w - 28;
        char fit[200];
        snprintf(buf, sizeof buf, "living %s to %s, %s on average", b2, b3, mean);
        skin_text(fo->small, skin_fit(fo->small, buf, room, fit, sizeof fit), x, y, TONE_TEXT, ALIGN_LEFT);
        y += skin_text_height(fo->small) + 2;
        char b4[48];
        fmt_num(b2, sizeof b2, ep.births);
        fmt_num(b3, sizeof b3, ep.deaths);
        fmt_num(b4, sizeof b4, ep.infections);
        snprintf(buf, sizeof buf, "%s births, %s deaths, %s infections", b2, b3, b4);
        skin_text(fo->small, skin_fit(fo->small, buf, room, fit, sizeof fit), x, y, TONE_TEXT, ALIGN_LEFT);
        y += skin_text_height(fo->small) + 2;
        const Line *l = syn_history_line(ep.lead_line);
        if (l && ep.lead_n > 0) {
            fmt_num(b2, sizeof b2, ep.lead_n);
            char ln[40];
            line_label(l, ln, sizeof ln);
            snprintf(buf, sizeof buf, "led by line %s (%s living)", ln, b2);
            skin_text(fo->small, skin_fit(fo->small, buf, room, fit, sizeof fit), x, y, TONE_MUTED,
                      ALIGN_LEFT);
            y += skin_text_height(fo->small) + 2;
        }
        if (ep.extinctions > 0) {
            snprintf(buf, sizeof buf, "%d mass extinction%s", ep.extinctions, ep.extinctions > 1 ? "s" : "");
            skin_text(fo->small_bold, buf, x, y, TONE_BAD, ALIGN_LEFT);
        }
        species_tile(a, (UIRect){card.x + left_w, card.y + 12, tile_w, card_h - 20}, &ep, (uint32_t)k);
        for (int c = 0; c < CH_COUNT; c++) {
            UIRect r = {card.x + left_w + (float)(c + 1) * (tile_w + GAP), card.y + 12, tile_w, card_h - 20};
            champion_tile(a, r, (ChampionKind)c, &ep.ch[c],
                          syn_champion_name((ChampionKind)c));
        }
    }
    return (float)count * (card_h + GAP);
}

/* ---- timeline ---- */

static Tone kind_tone(EventKind k)
{
    switch (k) {
    case EV_RECORD:
        return TONE_GOOD;
    case EV_EXTINCTION:
        return TONE_BAD;
    case EV_ATMOSPHERE:
        return TONE_WARN;
    case EV_USER:
        return TONE_MUTED;
    default:
        return TONE_ACCENT;
    }
}

static const char *kind_name(EventKind k)
{
    static const char *const names[EV_KINDS] = {"START", "RECORD", "DYING", "COLOUR",
                                                "LINE",  "AIR",    "YOU"};
    return k >= 0 && k < EV_KINDS ? names[k] : "";
}

/* Distinct colours for the river's lines, readable on the theme. */
static SDL_Color line_colour(int i, Uint8 a)
{
    float h = fmodf((float)i * 0.618034f, 1.0f) * 6.0f;
    bool light = skin_theme()->light;
    float s = light ? 0.65f : 0.55f, v = light ? 0.72f : 0.95f;
    float c = v * s, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    int hi = (int)h;
    if (hi == 0) { r = c; g = x; }
    else if (hi == 1) { r = x; g = c; }
    else if (hi == 2) { g = c; b = x; }
    else if (hi == 3) { g = x; b = c; }
    else if (hi == 4) { r = x; b = c; }
    else { r = c; b = x; }
    return (SDL_Color){(Uint8)((r + m) * 255), (Uint8)((g + m) * 255), (Uint8)((b + m) * 255), a};
}

#define RIVER_LINES 10

/* The run's lines, stacked: who rose, who fell. */
static void river(UIRect r, const History *hi)
{
    const Fonts *fo = shell_fonts();
    const Theme *th = skin_theme();
    skin_panel(r, false);
    skin_text(fo->bold, "THE LINES", r.x + 14, r.y + 10, TONE_TEXT, ALIGN_LEFT);
    skin_text(fo->small, "every organism alive, by the line it descends from; the ten biggest named",
              r.x + 14 + skin_text_width(fo->bold, "THE LINES") + 12, r.y + 13, TONE_MUTED, ALIGN_LEFT);
    UIRect plot = {r.x + 14, r.y + 40, r.w - 28, r.h - 40 - 30};
    int n = hi->n;
    if (n < 2 || plot.w < 40) {
        skin_text(fo->small, "the river fills in as time passes", plot.x, plot.y + 8, TONE_MUTED, ALIGN_LEFT);
        return;
    }
    const HistSample *s = hi->s;
    /* the biggest lines over the run */
    uint32_t pick[RIVER_LINES] = {0};
    float peak[RIVER_LINES] = {0};
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < HIST_TOP && s[i].top_n[j] > 0; j++) {
            uint32_t id = s[i].top_line[j];
            float v = s[i].top_n[j];
            int at = -1;
            for (int q = 0; q < RIVER_LINES; q++)
                if (pick[q] == id)
                    at = q;
            if (at >= 0) {
                if (v > peak[at])
                    peak[at] = v;
                continue;
            }
            int low = 0;
            for (int q = 1; q < RIVER_LINES; q++)
                if (peak[q] < peak[low])
                    low = q;
            if (v > peak[low]) {
                pick[low] = id;
                peak[low] = v;
            }
        }
    }
    /* in the order they were founded, oldest at the bottom */
    for (int q = 0; q < RIVER_LINES; q++)
        for (int p = q + 1; p < RIVER_LINES; p++)
            if ((pick[p] && pick[p] < pick[q]) || (!pick[q] && pick[p])) {
                uint32_t t = pick[p];
                pick[p] = pick[q];
                pick[q] = t;
                float f = peak[p];
                peak[p] = peak[q];
                peak[q] = f;
            }
    float maxpop = 1;
    for (int i = 0; i < n; i++)
        maxpop = fmaxf(maxpop, s[i].pop);
    /* at most a point per pixel column, each the mean of its samples */
    int m = n < (int)plot.w ? n : (int)plot.w;
    float *xs = malloc((size_t)m * 4 * sizeof *xs);
    float *acc = calloc((size_t)m, sizeof *acc);
    if (!xs || !acc) {
        free(xs);
        free(acc);
        return;
    }
    float *ya = xs + m, *yb = xs + 2 * m, *popm = xs + 3 * m;
    double tspan = (double)(s[n - 1].t - s[0].t);
    if (tspan <= 0)
        tspan = 1;
#define B0(j) ((int)((long long)(j) * n / m))
    for (int j = 0; j < m; j++) {
        int a = B0(j), b = B0(j + 1);
        double tm = (s[a].t + s[b - 1].t) / 2.0, pop = 0;
        for (int i = a; i < b; i++)
            pop += s[i].pop;
        xs[j] = plot.x + (float)((tm - s[0].t) / tspan) * plot.w;
        popm[j] = (float)(pop / (b - a));
    }
    skin_fill(plot, (Rgb){0, 0, 0}, th->light ? 0.06f : 0.35f);
    for (int q = 0; q <= RIVER_LINES; q++) {
        if (q < RIVER_LINES && !pick[q])
            continue;
        float best = -1;
        int peak_j = 0;
        for (int j = 0; j < m; j++) {
            float v = 0;
            if (q < RIVER_LINES) {
                int a = B0(j), b = B0(j + 1);
                for (int i = a; i < b; i++)
                    for (int k = 0; k < HIST_TOP; k++)
                        if (s[i].top_line[k] == pick[q])
                            v += s[i].top_n[k];
                v /= (float)(b - a);
                if (v > best) {
                    best = v;
                    peak_j = j;
                }
            } else {
                v = popm[j] - acc[j]; /* everyone else */
                if (v < 0)
                    v = 0;
            }
            /* counts against the most there were, or shares of who was alive */
            float scale = S.share ? (popm[j] > 0 ? popm[j] : 1) : maxpop;
            yb[j] = plot.y + plot.h - acc[j] / scale * plot.h;
            acc[j] += v;
            ya[j] = plot.y + plot.h - acc[j] / scale * plot.h;
        }
        SDL_Color c = q < RIVER_LINES ? line_colour((int)pick[q], 220) : tone(TONE_MUTED, 70);
        band(xs, ya, yb, m, c);
        if (q < RIVER_LINES) {
            /* its name where it is thickest */
            int j = peak_j;
            const Line *l = syn_history_line(pick[q]);
            float thick = yb[j] - ya[j];
            if (l && thick >= 15) {
                char nm[40];
                line_label(l, nm, sizeof nm);
                float lw = skin_text_width(fo->small_bold, nm) + 10, lh = skin_text_height(fo->small_bold);
                float tx = fminf(fmaxf(xs[j] - lw / 2, plot.x + 2), plot.x + plot.w - lw - 2);
                float ty = (ya[j] + yb[j]) / 2 - lh / 2;
                skin_fill(ui_rect(tx, ty, lw, lh), (Rgb){0, 0, 0}, 0.6f);
                text_col(fo->small_bold, nm, tx + 5, ty, (SDL_Color){255, 255, 255, 255}, ALIGN_LEFT);
            }
        }
    }
#undef B0
    /* the epochs, as faint marks and numbers along the top */
    int span = epoch_span(hi);
    int64_t k_first = hi->t_start / span;
    for (int64_t k = k_first; k * span <= s[n - 1].t; k++) {
        float x = plot.x + (float)((double)(k * span - s[0].t) / tspan) * plot.w;
        if (x < plot.x)
            continue;
        skin_fill(ui_rect(x, plot.y, 1, plot.h), th->text, 0.18f);
        char an[24];
        snprintf(an, sizeof an, "%lld", (long long)(k - k_first + 1));
        if (x + 14 < plot.x + plot.w)
            skin_text(fo->small, an, x + 3, plot.y + 2, TONE_MUTED, ALIGN_LEFT);
    }
    /* the events, as ticks under it */
    for (int i = 0; i < hi->nev; i++) {
        const Event *e = &hi->ev[i];
        if (S.filter >= 0 && (int)e->kind != S.filter)
            continue;
        float x = plot.x + (float)((double)(e->t - s[0].t) / tspan) * plot.w;
        if (x < plot.x || x > plot.x + plot.w)
            continue;
        SDL_Color c = e->colour >= 0 && e->kind == EV_COLOUR ? bio(e->colour, 255) : tone(kind_tone(e->kind), 255);
        skin_fill(ui_rect(x - 1, plot.y + plot.h + 3, 3, 9), (Rgb){c.r, c.g, c.b}, 1);
    }
    char t0[32], t1[32];
    tnum(t0, sizeof t0, s[0].t);
    tnum(t1, sizeof t1, s[n - 1].t);
    skin_text(fo->small, t0, plot.x, plot.y + plot.h + 13, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->small, t1, plot.x + plot.w, plot.y + plot.h + 13, TONE_MUTED, ALIGN_RIGHT);
    if (S.mark_t >= s[0].t && S.mark_t <= s[n - 1].t) {
        float x = plot.x + (float)((double)(S.mark_t - s[0].t) / tspan) * plot.w;
        skin_fill(ui_rect(x - 1, plot.y, 2, plot.h), th->accent, 1);
    }
    if (over(plot)) {
        int64_t at = s[0].t + (int64_t)llround((g_mx - plot.x) / plot.w * tspan);
        int best = 0;
        for (int i = 1; i < n; i++)
            if (llabs(s[i].t - at) < llabs(s[best].t - at))
                best = i;
        float hx = plot.x + (float)((s[best].t - s[0].t) / tspan) * plot.w;
        skin_fill(ui_rect(hx, plot.y, 1, plot.h), th->text, 0.6f);
        char tip[200], tt[32], pv[32];
        tnum(tt, sizeof tt, s[best].t);
        fmt_num(pv, sizeof pv, s[best].pop);
        int k = snprintf(tip, sizeof tip, "%s  %s living", tt, pv);
        const Line *l = s[best].top_n[0] > 0 ? syn_history_line(s[best].top_line[0]) : NULL;
        if (l) {
            char ln[40];
            line_label(l, ln, sizeof ln);
            snprintf(tip + k, sizeof tip - (size_t)k, "  biggest line %s (%.0f)", ln, s[best].top_n[0]);
        }
        uiin_tooltip(tip);
    }
    free(xs);
    free(acc);
}

static float tab_timeline(App *a, UIRect area, float y0, const History *hi)
{
    const Fonts *fo = shell_fonts();
    float row_h = 50, y = y0;
    int shown = 0;
    for (int j = 0; j < hi->nev; j++) {
        int i = S.oldest ? j : hi->nev - 1 - j;
        const Event *e = &hi->ev[i];
        if (S.filter >= 0 && (int)e->kind != S.filter)
            continue;
        UIRect r = {area.x, y, area.w, row_h - 4};
        y += row_h;
        shown++;
        if (!visible(r))
            continue;
        bool h = hot(r);
        if (h || e->t == S.mark_t)
            skin_row_hover(r);
        if (h && uiin_clicked(r))
            S.mark_t = e->t;
        char buf[48];
        tnum(buf, sizeof buf, e->t);
        skin_text(fo->bold, buf, r.x + 10, r.y + 6, TONE_TEXT, ALIGN_LEFT);
        /* what kind, as a chip; then the colour or the line (as the river has it) */
        Tone kt = kind_tone(e->kind);
        SDL_Color kc = tone(kt, 255);
        UIRect chip = {r.x + 120, r.y + 8, 76, 18};
        skin_fill(chip, (Rgb){kc.r, kc.g, kc.b}, 0.18f);
        skin_fill(ui_rect(chip.x, chip.y, 3, chip.h), (Rgb){kc.r, kc.g, kc.b}, 1);
        skin_text(fo->small_bold, kind_name(e->kind), chip.x + 9, chip.y + 1, kt, ALIGN_LEFT);
        float tx = chip.x + chip.w + 14;
        if (e->line || e->colour >= 0) {
            SDL_Color sc = e->line ? line_colour((int)e->line, 255) : bio(e->colour, 255);
            skin_fill(ui_rect(tx, r.y + 10, 12, 12), (Rgb){sc.r, sc.g, sc.b}, 1);
            tx += 20;
        }
        skin_text(fo->body, e->text, tx, r.y + 4, TONE_TEXT, ALIGN_LEFT);
        if (e->detail[0]) {
            char d[160];
            skin_text(fo->small, skin_fit(fo->small, e->detail, r.x + r.w - tx - 60, d, sizeof d), tx,
                      r.y + 4 + skin_text_height(fo->body) - 2, TONE_MUTED, ALIGN_LEFT);
        }
        if (e->gc) {
            UIRect th = {r.x + r.w - 50, r.y + 3, 40, 40};
            code_thumb(a, th, e->gc, "Click for its genes; right click to copy or keep it");
        }
    }
    if (!shown)
        skin_text(fo->body, "Nothing of this kind has happened yet.", area.x + 10, y0 + 10, TONE_MUTED,
                  ALIGN_LEFT);
    return y - y0 + 8;
}

/* ---- the screen ---- */

void stats_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    const History *hi = syn_history();
    float mxf, myf;
    uiin_mouse(&mxf, &myf);
    g_mx = (int)mxf;
    g_my = (int)myf;
    bool keys = !shell_field_active(NULL);
    S.hover_t = S.hover_next;
    S.hover_next = -1;
    static bool first = true;
    if (first) {
        /* screenshots: SYN3D_SHOT_TAB = overview | charts | epochs | family | timeline */
        const char *t = getenv("SYN3D_SHOT_TAB");
        for (int i = 0; t && i < TAB_COUNT; i++)
            if (!strcasecmp(t, k_tabs[i]))
                S.tab = (Tab)i;
        first = false;
    }

    skin_fill(ui_rect(0, 0, (float)w, (float)h), (Rgb){0, 0, 0}, 0.5f);
    /* under the shell's bar, whose STATISTICS tab names it and whose WORLD
     * tab (or Esc) goes back */
    float under_bar = SHELL_BAR_H + 10;
    float pw = fminf((float)w - 32, 1800), ph = (float)h - under_bar - (a->hints ? SHELL_HINTS_H : 0) - 12;
    UIRect panel = {((float)w - pw) / 2, under_bar, pw, ph};
    uiin_solid(panel);
    skin_panel(panel, true);

    /* the tabs */
    float x = panel.x + PAD, y = panel.y + 14;
    for (int t = 0; t < TAB_COUNT; t++) {
        float tw = skin_text_width(fo->bold, k_tabs[t]) + 34;
        UIRect r = {x, y, tw, 36};
        bool hv = over(r);
        skin_tab(r, fo->bold, k_tabs[t], (WidgetState){.hover = hv, .active = S.tab == (Tab)t});
        if (hv && uiin_clicked(r)) {
            S.tab = (Tab)t;
            S.thumb_drag = false;
        }
        x += tw + 6;
    }
    if (keys)
        for (int t = 0; t < TAB_COUNT; t++)
            if (uiin_key(SDLK_1 + t))
                S.tab = (Tab)t;

    /* the tab's own controls */
    y += 50;
    float cx = panel.x + PAD, ch = 32;
    char info[160], tn[32];
    fmt_num(tn, sizeof tn, (double)hi->t_now);
    if (S.tab == TAB_CHARTS) {
        static const char *const rn[3] = {"WHOLE RUN", "LAST QUARTER", "LAST 5%"};
        for (int i = 0; i < 3; i++) {
            UIRect r = {cx, y, skin_text_width(fo->bold, rn[i]) + 30, ch};
            if (button(r, rn[i], S.range == i))
                S.range = i;
            cx += r.w + 8;
        }
        snprintf(info, sizeof info, "a sample every %d unit%s, %d kept; hover to read them all at once",
                 hi->stride, hi->stride > 1 ? "s" : "", hi->n);
    } else if (S.tab == TAB_EPOCHS || S.tab == TAB_TIMELINE) {
        UIRect r = {cx, y, 150, ch};
        if (button(r, S.oldest ? "OLDEST FIRST" : "NEWEST FIRST", false))
            S.oldest = !S.oldest;
        cx += r.w + 18;
        if (S.tab == TAB_EPOCHS) {
            int span = epoch_span(hi);
            UIRect ra = {cx, y, 70, ch};
            if (button(ra, "AUTO", S.span == 0))
                S.span = 0;
            cx += ra.w + 8;
            /* the lengths that give between two and a few hundred epochs */
            int64_t len = hi->t_now - hi->t_start + 1;
            int shown = 0;
            for (int m = 1; m <= 64 && shown < 5; m *= 2) {
                if (m > 1 && len / (hi->epoch_base * m) < 2)
                    break;
                if (len / (hi->epoch_base * m) > 400)
                    continue;
                shown++;
                char lab[24];
                snprintf(lab, sizeof lab, "%d", hi->epoch_base * m);
                UIRect rb = {cx, y, skin_text_width(fo->bold, lab) + 26, ch};
                if (button(rb, lab, S.span == m || (S.span == 0 && span == hi->epoch_base * m)))
                    S.span = m;
                cx += rb.w + 6;
            }
            snprintf(info, sizeof info, "epochs of %d units; each one's best species, and its champions of those alive in it",
                     span);
        } else {
            static const char *const fl[] = {"ALL", "RECORDS", "DYING", "COLOURS", "LINES", "AIR", "YOU"};
            static const int fk[] = {-1, EV_RECORD, EV_EXTINCTION, EV_COLOUR, EV_LINE, EV_ATMOSPHERE, EV_USER};
            for (int i = 0; i < 7; i++) {
                UIRect rb = {cx, y, skin_text_width(fo->bold, fl[i]) + 26, ch};
                if (button(rb, fl[i], S.filter == fk[i]))
                    S.filter = fk[i];
                cx += rb.w + 6;
            }
            cx += 12;
            UIRect rs = {cx, y, 86, ch};
            if (button(rs, S.share ? "SHARE" : "COUNT", S.share))
                S.share = !S.share;
            cx += rs.w + 6;
            snprintf(info, sizeof info, "%d events", hi->nev);
        }
    } else if (S.tab == TAB_FAMILY) {
        cx = syn_family_controls(a, cx, y, ch, info, sizeof info);
    } else {
        char ts[24];
        fmt_num(ts, sizeof ts, (double)hi->t_start);
        snprintf(info, sizeof info, "t %s; recorded since t %s", tn, ts);
    }
    {
        /* what the tab shows, if it fits beside its controls */
        char fit[160];
        float room = panel.x + panel.w - PAD - cx - 16;
        if (room > 80)
            skin_text(fo->small, skin_fit(fo->small, info, room, fit, sizeof fit),
                      panel.x + panel.w - PAD, y + 8, TONE_MUTED, ALIGN_RIGHT);
    }
    y += ch + 14;

    /* the timeline's river stays put above its list */
    if (S.tab == TAB_TIMELINE) {
        UIRect rv = {panel.x + PAD, y, panel.w - PAD * 2, 230};
        g_view = rv;
        river(rv, hi);
        y += rv.h + GAP;
    }

    /* the family tree moves and zooms instead of scrolling */
    if (S.tab == TAB_FAMILY) {
        UIRect fv = {panel.x + PAD, y, panel.w - PAD * 2, panel.y + panel.h - PAD - y};
        g_view = fv;
        syn_family_view(a, fv, keys);
        return;
    }

    /* the scrolling part */
    UIRect view = {panel.x + PAD, y, panel.w - PAD * 2 - 16, panel.y + panel.h - PAD - y};
    g_view = view;
    float *sc = &S.scroll[S.tab];
    float max_scroll = fmaxf(0, S.content_h - view.h);
    if (over(view))
        *sc -= uiin_wheel() * 60;
    if (keys) {
        if (uiin_key(SDLK_PAGEDOWN))
            *sc += view.h * 0.9f;
        if (uiin_key(SDLK_PAGEUP))
            *sc -= view.h * 0.9f;
        if (uiin_key(SDLK_HOME))
            *sc = 0;
        if (uiin_key(SDLK_END))
            *sc = max_scroll;
    }
    /* the scrollbar */
    UIRect track = {view.x + view.w + 6, view.y, 8, view.h};
    if (S.content_h > view.h) {
        float th_h = fmaxf(30, view.h * view.h / S.content_h);
        float th_y = track.y + (track.h - th_h) * (max_scroll > 0 ? *sc / max_scroll : 0);
        UIRect thumb = {track.x, th_y, track.w, th_h};
        if (over(thumb) && uiin_pressed(thumb, 0)) {
            S.thumb_drag = true;
            S.thumb_grab = (float)g_my - th_y;
        } else if (over(track) && uiin_pressed(track, 0)) {
            *sc += ((float)g_my < th_y ? -1 : 1) * view.h * 0.9f;
        }
        if (S.thumb_drag) {
            if (uiin_mouse_down(0))
                *sc = ((float)g_my - S.thumb_grab - track.y) / fmaxf(1, track.h - th_h) * max_scroll;
            else
                S.thumb_drag = false;
        }
        if (*sc > max_scroll)
            *sc = max_scroll;
        if (*sc < 0)
            *sc = 0;
        th_y = track.y + (track.h - th_h) * (max_scroll > 0 ? *sc / max_scroll : 0);
        skin_fill(track, skin_theme()->border, 0.35f);
        skin_fill(ui_rect(track.x, th_y, track.w, th_h), skin_theme()->accent,
                  S.thumb_drag || over(track) ? 1.0f : 0.7f);
    } else {
        *sc = 0;
    }

    gl2d_clip_push(view.x, view.y, view.w, view.h);
    float top = view.y - *sc;
    switch (S.tab) {
    case TAB_OVERVIEW:
        S.content_h = tab_overview(a, view, top, hi);
        break;
    case TAB_CHARTS:
        S.content_h = tab_charts(a, view, top, hi);
        break;
    case TAB_EPOCHS:
        S.content_h = tab_epochs(a, view, top, hi);
        break;
    case TAB_TIMELINE:
        S.content_h = tab_timeline(a, view, top, hi);
        break;
    default:
        break;
    }
    gl2d_clip_pop();
}

void stats_family(App *a, const Organism *o)
{
    S.tab = TAB_FAMILY;
    syn_family_focus(syn_family_of(o));
    a->screen = SCREEN_STATS;
}

void stats_species(const Organism *o, char *out, size_t n)
{
    const Species *s = syn_family_get(syn_family_of(o));
    if (s)
        syn_family_label(s, out, n);
    else
        out[0] = '\0';
}

void stats_screen_free(void)
{
    syn_family_view_free();
}
