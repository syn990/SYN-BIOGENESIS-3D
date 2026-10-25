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

/* The world in a window.
 *
 * The world keeps its own fixed tick (a speed in world frames per second)
 * and the picture is drawn at the display's rate: each organism is drawn
 * between where it began its last frame and where it ended it, so motion is
 * smooth at any speed. The camera orbits an aim point that follows the
 * selected organism, the life, or the box's centre. The SYN-OS interface
 * (interface.c) is drawn over the scene; a press on it never reaches the
 * camera. The World is only read here, except by the actions (app_do).
 *
 *   drag            turn the camera          right-drag   move the aim
 *   wheel           closer / further         click        select an organism
 *   right-click     a menu                   `            the console
 *   space           pause                    .            one frame (paused)
 *   up / down       faster / slower          f            follow / stop following
 *   n / r           next seed / restart      home         camera back to the box
 *   t / shift+t     next / previous theme    h            hide / show the key hints
 *   l / p / s / a   the library / the parameters / the statistics / the arena
 *   ctrl+s          save the world (~/.config/syn-biogenesis-3d/worlds)
 *   esc             drop the selection, then quit
 */

#include "app/app.h"

#include <cglm/cglm.h>
#include <epoxy/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "app/app_internal.h"
#include "compat.h"
#include "app/looks.h"
#include "app/minimap.h"
#include "app/flesh.h"
#include "io/bgg.h"
#include "syn/winner.h"
#include "io/worldfile.h"
#include "render/lighting.h"
#include "sim/params.h"
#include "syn/history.h"
#include "syn/round.h"
#include "syn/sidecar.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

#define FOV_DEG 45.0

const int k_speeds[] = {5, 15, 30, 60, 120, 240, 480, 960, 0};
const int k_nspeeds = (int)(sizeof k_speeds / sizeof k_speeds[0]);

static V3 box_centre(const World *w)
{
    return v3(w->width / 2.0, w->height / 2.0, w->depth / 2.0);
}

static void camera_home(App *a)
{
    V3 box = v3(a->w.width, a->w.height, a->w.depth);
    bool slab = a->w.depth * 4 < (a->w.width < a->w.height ? a->w.width : a->w.height);
    a->yaw = -60;
    a->pitch = slab ? 38 : 26;
    a->distance = slab ? 560 : 1.15 * v3_len(box);
    a->follow = slab ? FOLLOW_LIFE : FOLLOW_NONE;
    a->aim = slab ? world_life_centre_or_box(&a->w) : box_centre(&a->w);
}

bool app_watch_round(App *a, const ArenaRoundSpec *s)
{
    P = s->params;
    world_free(&a->w);
    world_init(&a->w);
    int first[ARENA_SLOTS], placed[ARENA_SLOTS];
    round_begin(&a->w, s->seed, s->code, s->nf, s->wild, NULL, first, placed);
    a->wo.seed = s->seed;
    a->wo.nimports = 0;
    syn_history_reset(&a->w, false);
    a->loaded[0] = 0;
    a->world_path[0] = 0;
    a->ff_target = 0;
    a->selected = -1;
    a->acc = 0;
    return true;
}

void app_being_menu(App *a, const GeneticCode *gc, const char *name, const char *origin, const char *folder)
{
    if (!gc)
        return;
    gc_assign(&a->menu_gc, (GeneticCode *)gc);
    snprintf(a->menu_name, sizeof a->menu_name, "%s", name && *name ? name : "BEING");
    snprintf(a->menu_origin, sizeof a->menu_origin, "%s", origin ? origin : "");
    snprintf(a->menu_folder, sizeof a->menu_folder, "%s", folder && *folder ? folder : "Saved");
    static const ShellCommand items[] = {
        {.id = ACT_BEING_LAB, .label = "Open in the laboratory", .icon = ICON_FLASK},
        {.id = ACT_BEING_COPY, .label = "Copy genetic code", .icon = ICON_COPY},
        {.id = ACT_BEING_KEEP, .label = "Keep in the library", .icon = ICON_BOOKMARK},
        {.id = ACT_BEING_WHERE, .label = "Where it lives", .icon = ICON_DICE},
    };
    float x, y;
    uiin_mouse(&x, &y);
    shell_menu_open(x, y, a->menu_name, items, 4);
}

bool app_make_world(App *a)
{
    char err[512];
    if (world_options_genesis(&a->wo, &a->w, err, sizeof err) < 0) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return false;
    }
    syn_history_reset(&a->w, false);
    a->loaded[0] = 0;
    a->world_path[0] = 0;
    a->ff_target = 0;
    for (long f = 0; f < (long)a->warm * 256; f++)
        app_step(a);
    a->selected = -1;
    a->acc = 0;
    return true;
}

void app_worlds_dir(char *out, size_t n)
{
    snprintf(out, n, "%s/syn-biogenesis-3d/worlds", config_home());
}

bool app_save_world(App *a, const char *name, char *path, size_t pathlen, char *err, int errlen)
{
    char dir[1024];
    app_worlds_dir(dir, sizeof dir);
    for (char *p = dir + 1;; p++) {
        if (*p == '/' || !*p) {
            char c = *p;
            *p = 0;
            if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
                snprintf(err, (size_t)errlen, "%s: cannot make the folder", dir);
                return false;
            }
            if (!c)
                break;
            *p = c;
        }
    }
    char auto_name[96];
    if (!name || !name[0]) {
        snprintf(auto_name, sizeof auto_name, "seed %lld time %lld", (long long)a->wo.seed,
                 (long long)(a->w.frame / 256));
        name = auto_name;
    }
    snprintf(path, pathlen, "%s/%s.%s", dir, name, WORLDFILE_EXT);
    if (!worldfile_save(&a->w, path, err, errlen))
        return false;
    /* the history and the family tree beside it */
    syn_sidecar_save(&a->w, path);
    snprintf(a->world_path, sizeof a->world_path, "%s", path);
    return true;
}

bool app_load_world(App *a, const char *name, char *err, int errlen)
{
    char path[1200];
    snprintf(path, sizeof path, "%s", name);
    if (!strchr(path_slashes(path), '/')) {
        char dir[1024];
        app_worlds_dir(dir, sizeof dir);
        size_t n = strlen(name);
        bool ext = n > 4 && !strcmp(name + n - 4, "." WORLDFILE_EXT);
        snprintf(path, sizeof path, "%s/%s%s", dir, name, ext ? "" : "." WORLDFILE_EXT);
    }
    if (!worldfile_load(&a->w, path, err, errlen))
        return false;
    const char *base = strrchr(path, '/');
    snprintf(a->loaded, sizeof a->loaded, "%.120s", base ? base + 1 : path);
    snprintf(a->world_path, sizeof a->world_path, "%.1099s", path);
    /* the history from the opening, or as it was saved beside the world */
    syn_history_reset(&a->w, true);
    syn_sidecar_load(&a->w, path);
    a->selected = -1;
    a->acc = 0;
    return true;
}

void app_list_worlds(void (*line)(const char *s))
{
    char dir[1024];
    app_worlds_dir(dir, sizeof dir);
    DIR *d = opendir(dir);
    if (!d) {
        line("(no saved worlds yet)");
        return;
    }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len > 4 && !strcmp(e->d_name + len - 4, "." WORLDFILE_EXT)) {
            char s[300];
            snprintf(s, sizeof s, "  %.*s", (int)(len - 4), e->d_name);
            line(s);
            n++;
        }
    }
    closedir(d);
    if (!n)
        line("(no saved worlds yet)");
}

void app_run_to(App *a, int64_t frame)
{
    a->ff_target = frame > a->w.frame ? frame : 0;
    a->paused = false;
}

void app_step(App *a)
{
    world_time(&a->w);
    syn_history_frame(&a->w);
}

Organism *app_find(const World *w, int id)
{
    for (int i = 0; i < w->norgs; i++)
        if (w->orgs[i]->id == id)
            return w->orgs[i];
    return NULL;
}

const char *app_speed_text(const App *a)
{
    static char s[16];
    if (k_speeds[a->speed])
        snprintf(s, sizeof s, "%d/s", k_speeds[a->speed]);
    else
        snprintf(s, sizeof s, "max");
    return s;
}

/* ---- drawing ---- */

static Quat quat_nlerp(Quat a, Quat b, double t)
{
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0)
        b = (Quat){-b.w, -b.x, -b.y, -b.z};
    Quat q = {a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t};
    return quat_normalize(q);
}

void app_draw_pose(const Organism *o, double alpha, V3 *pos, Quat *q)
{
    *pos = v3_add(o->prev_pos, v3_scale(v3_sub(o->pos, o->prev_pos), alpha));
    *q = quat_nlerp(o->prev_q, o->q, alpha);
}

/* a point's pseudo-random place in [0, 1), from its number and a salt */
static double hash01(uint32_t i, uint32_t salt)
{
    uint32_t h = i * 0x9E3779B1u ^ salt * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (h & 0xFFFFFF) / (double)0x1000000;
}

static void *grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap)
        return p;
    while (*cap < need)
        *cap = *cap ? *cap * 2 : 1024;
    p = realloc(p, (size_t)*cap * size);
    if (!p)
        abort();
    return p;
}

/* a body's own number in [0, 1), for where its beating is */
static float body_phase(int id)
{
    uint32_t h = (uint32_t)id * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (float)((h & 0xFFFF) / 65536.0);
}

/* the view's six planes, from its matrix (column-major), each a normal
 * and an offset, the inside where n.p + d > 0 */
static void frustum(const float *m, double planes[6][4])
{
    for (int k = 0; k < 6; k++) {
        int row = k / 2;
        double sign = k % 2 ? -1 : 1, len = 0;
        for (int c = 0; c < 4; c++)
            planes[k][c] = m[c * 4 + 3] + sign * m[c * 4 + row];
        for (int c = 0; c < 3; c++)
            len += planes[k][c] * planes[k][c];
        len = sqrt(len);
        for (int c = 0; c < 4 && len > 0; c++)
            planes[k][c] /= len;
    }
}

static bool in_view(double planes[6][4], V3 p, double r)
{
    for (int k = 0; k < 6; k++)
        if (planes[k][0] * p.x + planes[k][1] * p.y + planes[k][2] * p.z + planes[k][3] < -r)
            return false;
    return true;
}

/* Every body in view as flesh (app/flesh.c), by how big it is on the
 * screen: fine near, coarse further, lines far off (all lines in a look
 * that wants them); and every body's shadow on the floor. */
static void build_instances(App *a)
{
    const World *w = &a->w;
    a->nshadows = 0;
    Look look = look_current();
    LookScene ls;
    look_scene(look, skin_theme(), &ls);
    float pal[COL_COUNT][3];
    for (int c = 0; c < COL_COUNT; c++)
        look_rgb(look, c, pal[c]);
    double planes[6][4];
    frustum(a->view.view_proj, planes);
    int nt[3] = {0};
    V3 *ends = NULL;
    int ends_cap = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        V3 pos;
        Quat q;
        app_draw_pose(o, a->alpha, &pos, &q);
        if (ls.floor) {
            a->shadows = grow(a->shadows, &a->shadows_cap, a->nshadows + 1, sizeof *a->shadows);
            double up = w->depth > 0 ? fmin(fmax(pos.z / w->depth, 0), 1) : 0;
            a->shadows[a->nshadows++] = (WaterShadow){(float)pos.x, (float)pos.y,
                                                       (float)(o->radius * (0.7 + 0.8 * up)),
                                                       (float)(0.55 * (1 - 0.75 * up))};
        }
        if (!in_view(planes, pos, o->radius + 8))
            continue;
        double px = 2 * (o->radius + 2) * a->view.px_scale / (v3_len(v3_sub(pos, a->cam_eye)) + 1e-3);
        int t = ls.lines ? 2 : px >= 110 ? 0 : px >= 28 ? 1 : 2;
        int need = nt[t] + flesh_count(o->segments), was = a->tier_cap[t];
        a->tier[t] = grow(a->tier[t], &a->tier_cap[t], need, sizeof *a->tier[t]);
        a->tier_org[t] = grow(a->tier_org[t], &was, need, sizeof *a->tier_org[t]);
        ends = grow(ends, &ends_cap, 2 * o->segments, sizeof *ends);
        M3 R = m3_from_quat(q);
        for (int s = 0; s < o->segments; s++) {
            ends[s] = v3_add(pos, m3_mul_v(&R, o->bs[s]));
            ends[o->segments + s] = v3_add(pos, m3_mul_v(&R, o->be[s]));
        }
        BodyInstance *b = &a->tier[t][nt[t]];
        int main_col;
        int made = flesh_build(b, ends, ends + o->segments, o->segments, o->gc->ngenes, o->seg_color,
                               m3_mul_v(&R, v3(0, 0, 1)), body_phase(o->id), P.segment_radius, &main_col);
        if (ls.lines)
            made--; /* no cell at the hub: plain lines */
        bool selected = o->id == a->selected;
        float flash = o->frames_color > 0 && o->flash_color != FLASH_NONE ? (float)o->frames_color / 10.0f : 0.0f;
        /* the look's colours, a little under full so the light has room,
         * and dimmer where the water's light on the body is: deep down and
         * in the shade (the light leaves have, never quite black); the cell
         * at the hub paler, a membrane */
        float lit = 0.3f + 0.7f * (float)world_light_at(w, pos);
        for (int k = 0; k < made; k++) {
            bool core = b[k].tissue == TISSUE_CORE;
            int col = flash > 0 ? o->flash_color : core ? main_col : o->seg_color[k];
            if (col >= COL_COUNT)
                col = COL_WHITE;
            float kk = (col == COL_BROWN ? 0.55f : 0.8f) * lit;
            for (int c = 0; c < 3; c++) {
                float v = core && o->alive ? pal[col][c] * 0.7f + 0.3f : pal[col][c];
                b[k].rgb[c] = v * kk;
            }
            b[k].glow = flash * 0.9f + (col == COL_BROWN ? 0 : ls.glow);
            if (selected)
                b[k].glow += 0.25f + 0.15f * (float)sin(a->seconds * 5.0);
            a->tier_org[t][nt[t] + k] = o->id;
        }
        nt[t] += made;
    }
    free(ends);
    /* the tiers one after another, as they are drawn */
    int total = nt[0] + nt[1] + nt[2], was = a->inst_cap;
    a->inst = grow(a->inst, &a->inst_cap, total, sizeof *a->inst);
    a->inst_org = grow(a->inst_org, &was, total, sizeof *a->inst_org);
    a->ninst = 0;
    for (int t = 0; t < 3; t++) {
        if (nt[t]) {
            memcpy(a->inst + a->ninst, a->tier[t], (size_t)nt[t] * sizeof *a->inst);
            memcpy(a->inst_org + a->ninst, a->tier_org[t], (size_t)nt[t] * sizeof *a->inst_org);
        }
        a->ninst += nt[t];
        a->ntier[t] = nt[t];
    }
}

/* the look's motes drifting down through the water, or its stars */
static void build_points(App *a)
{
    const World *w = &a->w;
    LookScene ls;
    look_scene(look_current(), skin_theme(), &ls);
    a->npoints = 0;
    if (ls.specks) {
        /* marine snow: sinking slowly, swaying, starting again at the top */
        int n = (int)fmin(2400, fmax(300, (double)w->width * w->height * w->depth / 120000.0));
        a->points = grow(a->points, &a->points_cap, n, sizeof *a->points);
        for (int i = 0; i < n; i++) {
            uint32_t u = (uint32_t)i;
            double fall = fmod(w->depth * hash01(u, 13) - a->seconds * (0.6 + 1.2 * hash01(u, 17)), w->depth);
            if (fall < 0)
                fall += w->depth;
            double ph = a->seconds * 0.15 + 6.28 * hash01(u, 14);
            WaterPoint *p = &a->points[a->npoints++];
            p->p[0] = (float)(w->width * hash01(u, 11) + 4 * sin(ph));
            p->p[1] = (float)(w->height * hash01(u, 12) + 4 * cos(ph * 0.8));
            p->p[2] = (float)fall;
            p->size = (float)(0.35 + 0.9 * hash01(u, 15));
            float b = 0.5f + 0.5f * (float)hash01(u, 16);
            p->rgba[0] = 0.75f * b;
            p->rgba[1] = 0.95f * b;
            p->rgba[2] = 0.90f * b;
            p->rgba[3] = 0.30f + 0.35f * (float)hash01(u, 18);
        }
    }
    if (ls.stars) {
        /* far off all round, each a few pixels across wherever the eye is */
        V3 mid = v3(w->width / 2, w->height / 2, w->depth / 2);
        double diag = v3_len(v3(w->width, w->height, w->depth));
        a->points = grow(a->points, &a->points_cap, a->npoints + 900, sizeof *a->points);
        for (uint32_t i = 0; i < 900; i++) {
            double z = 2 * hash01(i, 1) - 1, th = 2 * D_PI * hash01(i, 2), rr = sqrt(1 - z * z);
            V3 q = v3_add(mid, v3_scale(v3(rr * cos(th), rr * sin(th), z), diag * (1.3 + 0.5 * hash01(i, 3))));
            double d = v3_len(v3_sub(q, a->cam_eye));
            WaterPoint *p = &a->points[a->npoints++];
            p->p[0] = (float)q.x;
            p->p[1] = (float)q.y;
            p->p[2] = (float)q.z;
            p->size = (float)(d * (0.0016 + 0.0022 * hash01(i, 4)));
            float b = (float)(0.6 + 1.2 * hash01(i, 5));
            p->rgba[0] = b * (0.85f + 0.15f * (float)hash01(i, 6));
            p->rgba[1] = b * 0.9f;
            p->rgba[2] = b;
            p->rgba[3] = 1;
        }
    }
}

static void line(App *a, V3 p, V3 q, const float rgba[4])
{
    if (a->nlines + 2 > a->lines_cap) {
        a->lines_cap = a->lines_cap ? a->lines_cap * 2 : 1024;
        a->lines = realloc(a->lines, (size_t)a->lines_cap * sizeof *a->lines);
        if (!a->lines)
            abort();
    }
    LineVertex *v = &a->lines[a->nlines];
    V3 ends[2] = {p, q};
    for (int k = 0; k < 2; k++) {
        v[k].p[0] = (float)ends[k].x;
        v[k].p[1] = (float)ends[k].y;
        v[k].p[2] = (float)ends[k].z;
        memcpy(v[k].rgba, rgba, sizeof v[k].rgba);
    }
    a->nlines += 2;
}

static void rgba_of(Rgb c, float alpha, float out[4])
{
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = alpha;
}

/* a grid on the wall at coordinate `at` of axis `fixed`, lines every step
 * along the other two */
static void wall_grid(App *a, int fixed, double at, double step, const float rgba[4])
{
    const World *w = &a->w;
    double size[3] = {w->width, w->height, w->depth};
    int u = (fixed + 1) % 3, v = (fixed + 2) % 3;
    for (int pass = 0; pass < 2; pass++) {
        int along = pass ? v : u, across = pass ? u : v;
        for (double s = step; s < size[along] - 1e-9; s += step) {
            double p[3], q[3];
            p[fixed] = q[fixed] = at;
            p[along] = q[along] = s;
            p[across] = 0;
            q[across] = size[across];
            line(a, v3(p[0], p[1], p[2]), v3(q[0], q[1], q[2]), rgba);
        }
    }
}

/* The box and the floor in the look's colours (Classic: the theme's, its
 * border for the grid and its accent for the box's edges), the selection's
 * marks in the theme's accent, and the look's own walls. */
static void build_lines(App *a)
{
    const World *w = &a->w;
    const Theme *t = skin_theme();
    LookScene ls;
    look_scene(look_current(), t, &ls);
    a->nlines = 0;
    float grid[4] = {ls.line[0], ls.line[1], ls.line[2], ls.grid};
    float edge[4] = {ls.edge_rgb[0], ls.edge_rgb[1], ls.edge_rgb[2], ls.edge}, mark[4];
    rgba_of(t->accent, 0.9f, mark);
    double step = 100;
    while ((w->width / step) > 40 || (w->height / step) > 40)
        step *= 2;
    if (ls.grid > 0) {
        for (double x = 0; x <= w->width + 1e-9; x += step)
            line(a, v3(x, 0, 0), v3(x, w->height, 0), grid);
        for (double y = 0; y <= w->height + 1e-9; y += step)
            line(a, v3(0, y, 0), v3(w->width, y, 0), grid);
        /* the surface, where the light comes in, more faintly */
        float surface[4] = {ls.edge_rgb[0], ls.edge_rgb[1], ls.edge_rgb[2], ls.grid * 0.6f};
        for (double x = step; x < w->width - 1e-9; x += step)
            line(a, v3(x, 0, w->depth), v3(x, w->height, w->depth), surface);
        for (double y = step; y < w->height - 1e-9; y += step)
            line(a, v3(0, y, w->depth), v3(w->width, y, w->depth), surface);
    }
    if (ls.walls) {
        /* SYN-OS: every wall a grid */
        float wall[4] = {ls.line[0], ls.line[1], ls.line[2], ls.grid * 0.55f};
        double ws = step / 2;
        wall_grid(a, 0, 0, ws, wall);
        wall_grid(a, 0, w->width, ws, wall);
        wall_grid(a, 1, 0, ws, wall);
        wall_grid(a, 1, w->height, ws, wall);
    }
    V3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = v3(i & 1 ? w->width : 0, i & 2 ? w->height : 0, i & 4 ? w->depth : 0);
    static const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                 {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int k = 0; k < 12; k++)
        line(a, c[e[k][0]], c[e[k][1]], edge);
    Organism *s = a->selected >= 0 ? app_find(w, a->selected) : NULL;
    if (s) {
        V3 pos;
        Quat q;
        app_draw_pose(s, a->alpha, &pos, &q);
        line(a, pos, v3(pos.x, pos.y, 0), mark);
        double r = s->radius + 4;
        for (int k = 0; k < 4; k++) {
            double th = k * D_PI / 2 + a->seconds * 0.8;
            V3 d = v3(cos(th), sin(th), 0);
            line(a, v3_add(pos, v3_scale(d, r)), v3_add(pos, v3_scale(d, r + 6)), mark);
        }
    }
}

static void set_view(App *a)
{
    PlatformWindow *pw = &a->win;
    double yaw = a->yaw * D_PI / 180, pitch = a->pitch * D_PI / 180;
    V3 eye = v3_add(a->aim, v3_scale(v3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch)),
                                      a->distance));
    a->cam_eye = eye;
    a->cam_fwd = v3_unit_or(v3_sub(a->aim, eye), v3(0, 1, 0));
    a->cam_right = v3_unit_or(v3_cross(a->cam_fwd, v3(0, 0, 1)), v3(1, 0, 0));
    a->cam_up = v3_cross(a->cam_right, a->cam_fwd);
    double diag = v3_len(v3(a->w.width, a->w.height, a->w.depth));
    /* far enough for the stars (a look's, up to 1.8 diagonals from the
     * box's middle) */
    float near = (float)fmax(0.5, a->distance * 0.01), far = (float)(a->distance + diag * 2.5);
    mat4 proj, view;
    vec3 e = {(float)eye.x, (float)eye.y, (float)eye.z};
    vec3 c = {(float)a->aim.x, (float)a->aim.y, (float)a->aim.z};
    vec3 up = {0, 0, 1};
    glm_perspective(glm_rad((float)FOV_DEG),
                    (float)pw->width / (float)(pw->height > 0 ? pw->height : 1), near, far, proj);
    glm_lookat(e, c, up, view);
    mat4 vp;
    glm_mat4_mul(proj, view, vp);
    memcpy(a->view.view_proj, vp, sizeof a->view.view_proj);
    mat4 inv;
    glm_mat4_inv(vp, inv);
    memcpy(a->view.inv_view_proj, inv, sizeof a->view.inv_view_proj);
    memcpy(a->view.eye, e, sizeof a->view.eye);
    V3 cr = a->cam_right, cu = a->cam_up;
    float right[3] = {(float)cr.x, (float)cr.y, (float)cr.z}, upv[3] = {(float)cu.x, (float)cu.y, (float)cu.z};
    memcpy(a->view.right, right, sizeof right);
    memcpy(a->view.up, upv, sizeof upv);
    V3 l = v3_unit_or(v3(0.35, 0.25, 1.0), v3(0, 0, 1));
    a->view.light_dir[0] = (float)l.x;
    a->view.light_dir[1] = (float)l.y;
    a->view.light_dir[2] = (float)l.z;
    /* the water is the look's: Classic, the theme's background */
    LookScene ls;
    look_scene(look_current(), skin_theme(), &ls);
    for (int k = 0; k < 3; k++) {
        a->view.fog_rgb[k] = ls.water[k];
        a->view.deep_rgb[k] = ls.deep[k];
    }
    a->view.box[0] = (float)a->w.width;
    a->view.box[1] = (float)a->w.height;
    a->view.box[2] = (float)a->w.depth;
    a->view.time = (float)fmod(a->seconds, 10000.0);
    a->view.light_depth = (float)P.light_depth;
    a->view.tissue = ls.tissue;
    a->view.sss = ls.sss;
    a->view.caustics = ls.caustics;
    a->view.lines = ls.lines;
    a->view.line_radius = (float)P.segment_radius;
    a->view.direct = 0;
    a->view.px_scale = (float)(pw->height / (2 * tan(FOV_DEG * D_PI / 360)));
    /* two steps of the depth buffer's precision at the aim (near is a
     * hundredth of the distance, 24 bits of depth) */
    a->view.depth_step = (float)(a->distance * 1.2e-5);
    a->view.fog_near = (float)(a->distance * 0.6);
    a->view.fog_far = (float)(a->distance + diag);
}

static void draw(App *a)
{
    PlatformWindow *pw = &a->win;
    /* the look's bloom, against the lighting's own */
    static float bloom0 = -1;
    if (bloom0 < 0)
        bloom0 = g_graphics.bloom_strength;
    LookScene ls;
    look_scene(look_current(), skin_theme(), &ls);
    g_graphics.bloom_strength = bloom0 * ls.bloom;
    set_view(a);
    build_instances(a);
    build_lines(a);
    build_points(a);
    water_prepare(&a->view, ls.floor ? ls.sediment[0] : NULL, ls.floor ? ls.sediment[1] : NULL);
    lighting_scene_begin(pw->width, pw->height, a->view.fog_rgb[0], a->view.fog_rgb[1],
                         a->view.fog_rgb[2]);
    if (ls.gradient)
        water_background(&a->view, pw->width, pw->height);
    if (ls.floor) {
        /* the light that reaches the floor, a little more than the
         * world's so it can be seen */
        double down = P.light_depth > 0 ? exp(-a->w.depth / P.light_depth) : 1;
        water_floor(&a->view, (float)(0.22 + 0.78 * down));
        water_shadows(&a->view, a->shadows, a->nshadows);
    }
    bodies_draw_tiers(a->inst, a->ntier, &a->view);
    lines_draw(a->lines, a->nlines, &a->view);
    water_shafts(&a->view, 22, ls.shafts);
    water_points(&a->view, a->points, a->npoints, !ls.stars);
    if (ls.surface)
        water_surface(&a->view);
    lighting_scene_end();
    if (a->interface)
        interface_draw(a, pw->width, pw->height);
}

/* ---- picking ---- */

/* The organism under drawable pixel (mx, my): the nearest piece of flesh
 * the ray through it passes within reach of. */
static int pick(App *a, float mx, float my)
{
    PlatformWindow *pw = &a->win;
    double nx = (2.0 * mx / pw->width - 1), ny = (1 - 2.0 * my / pw->height);
    double th = tan(FOV_DEG * D_PI / 360), aspect = (double)pw->width / pw->height;
    V3 dir = v3_unit_or(v3_add(a->cam_fwd, v3_add(v3_scale(a->cam_right, nx * th * aspect),
                                                  v3_scale(a->cam_up, ny * th))),
                        a->cam_fwd);
    V3 o = a->cam_eye;
    double best = 1e300;
    int best_id = -1;
    for (int i = 0; i < a->ninst; i++) {
        const BodyInstance *c = &a->inst[i];
        double reach = fmax(c->r0, c->r1) * 1.3 + a->distance * 0.004;
        V3 p = v3(c->a[0], c->a[1], c->a[2]), q = v3(c->b[0], c->b[1], c->b[2]);
        /* closest points of the ray o + t dir (t >= 0) and the segment pq */
        V3 d2 = v3_sub(q, p), r = v3_sub(o, p);
        double b = v3_dot(dir, d2), e = v3_dot(d2, d2), f = v3_dot(d2, r), cc = v3_dot(dir, r);
        double denom = e - b * b, t, s;
        if (e < 1e-12 || denom < 1e-12) {
            s = 0;
            t = -cc;
        } else {
            t = (b * f - cc * e) / denom;
            s = (b * t + f) / e;
            s = s < 0 ? 0 : s > 1 ? 1 : s;
            t = v3_dot(dir, v3_sub(v3_add(p, v3_scale(d2, s)), o));
        }
        if (t < 0)
            continue;
        V3 gap = v3_sub(v3_add(o, v3_scale(dir, t)), v3_add(p, v3_scale(d2, s)));
        if (v3_len(gap) < reach && t < best) {
            best = t;
            best_id = a->inst_org[i];
        }
    }
    return best_id;
}

V3 app_point_under(App *a, float mx, float my)
{
    PlatformWindow *pw = &a->win;
    double nx = (2.0 * mx / pw->width - 1), ny = (1 - 2.0 * my / pw->height);
    double th = tan(FOV_DEG * D_PI / 360), aspect = (double)pw->width / pw->height;
    V3 dir = v3_unit_or(v3_add(a->cam_fwd, v3_add(v3_scale(a->cam_right, nx * th * aspect),
                                                  v3_scale(a->cam_up, ny * th))),
                        a->cam_fwd);
    double along = v3_dot(dir, a->cam_fwd);
    double t = along > 1e-6 ? v3_dot(v3_sub(a->aim, a->cam_eye), a->cam_fwd) / along : 0;
    V3 p = v3_add(a->cam_eye, v3_scale(dir, t));
    const World *w = &a->w;
    p.x = p.x < 1 ? 1 : p.x > w->width - 1 ? w->width - 1 : p.x;
    p.y = p.y < 1 ? 1 : p.y > w->height - 1 ? w->height - 1 : p.y;
    p.z = p.z < 1 ? 1 : p.z > w->depth - 1 ? w->depth - 1 : p.z;
    return p;
}

bool app_paste(App *a, GeneticCode *gc, V3 p)
{
    Organism *o = org_new(&a->w);
    if (!org_place_at(o, gc, p)) {
        org_free(o);
        interface_note(a, true, "No room there: something is in the way, or it would reach out of the water");
        return false;
    }
    world_add_organism(&a->w, o, NULL);
    a->selected = o->id;
    a->selected_was_alive = true;
    return true;
}

/* where the next organism made goes, used once */
V3 app_take_place(App *a)
{
    V3 p = a->place_set ? a->place : a->aim;
    a->place_set = false;
    return p;
}

static Organism *selected_org(App *a)
{
    return a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
}

/* Create a winner (syn/winner.h): the world waits while the search works
 * on a copy of it, then the winner is pasted where the search put it. */
static bool g_winner_resume; /* the world was running when it was asked for */

static void winner_begin(App *a)
{
    char err[300];
    V3 p = app_take_place(a);
    /* three minutes, runs of 256 time units; shorter for tests */
    const char *ts = getenv("SYN3D_WINNER_SECONDS"), *tu = getenv("SYN3D_WINNER_UNITS");
    int seconds = ts && atoi(ts) > 0 ? atoi(ts) : 180, units = tu && atoi(tu) > 0 ? atoi(tu) : 256;
    if (!syn_winner_start(&a->w, p, seconds, units, err, sizeof err)) {
        interface_note(a, true, "No winner: %s", err);
        return;
    }
    g_winner_resume = !a->paused;
    a->paused = true;
    a->ff_target = 0;
    interface_note(a, false, "Working out a winner for the world as it is, on every core: the world waits until it "
                             "is placed");
}

/* each frame: the search's lines, the verdict when its time comes, and the
 * winner pasted when it is found */
static void winner_poll(App *a)
{
    const char *s;
    while ((s = syn_winner_line()))
        shell_print(SHELL_OUT, "%s", s);
    if ((s = syn_winner_verdict()))
        interface_note(a, !strstr(s, "as calculated"), "%s", s);
    if (!syn_winner_running())
        return;
    if (world_hash(&a->w) != syn_winner_start_hash()) {
        syn_winner_cancel();
        interface_note(a, true, "The world moved on while the winner was worked out, so nothing was placed. Pause it "
                                "and ask again");
        return;
    }
    WinnerResult r;
    if (!syn_winner_poll(&r))
        return;
    while ((s = syn_winner_line()))
        shell_print(SHELL_OUT, "%s", s);
    a->paused = !g_winner_resume;
    char err[256];
    if (!r.found) {
        interface_note(a, true, "No winner: %s", r.why);
        return;
    }
    GeneticCode *gc = bgg_load(r.bgg, err, sizeof err);
    if (!gc) {
        interface_note(a, true, "No winner: %s", err);
        return;
    }
    bool placed = app_paste(a, gc, r.p);
    gc_unref(gc);
    if (!placed)
        return;
    a->follow = FOLLOW_SELECTED;
    syn_history_user(&a->w, 'W', 0, "You created a winner");
    syn_winner_expect(&a->w, &r);
    if (r.owns)
        interface_note(a, false, "Winner placed at (%.0f, %.0f, %.0f deep): %s. Calculated: its bloodline is all "
                                 "that lives from time %lld (%d organisms), %d at time %lld",
                       r.p.x, r.p.y, r.p.z, r.label, (long long)r.t, r.alive, r.end_n, (long long)r.end_t);
    else
        interface_note(a, false, "Winner placed at (%.0f, %.0f, %.0f deep): %s. Calculated: its bloodline is %d "
                                 "of the %d living at time %lld",
                       r.p.x, r.p.y, r.p.z, r.label, r.mine, r.alive, (long long)r.t);
}

/* ---- actions ---- */

static void select_org(App *a, int id)
{
    a->selected = id;
    Organism *o = id >= 0 ? app_find(&a->w, id) : NULL;
    a->selected_was_alive = o && o->alive;
    if (id >= 0)
        a->follow = FOLLOW_SELECTED;
    else if (a->follow == FOLLOW_SELECTED)
        a->follow = FOLLOW_NONE;
}

void app_do(App *a, int action)
{
    if (action >= ACT_LIBRARY_PICK) {
        library_pick(a, action - ACT_LIBRARY_PICK);
        return;
    }
    if (action >= ACT_LOOK && action <= ACT_LOOK_LAST) {
        look_set((Look)(action - ACT_LOOK));
        return;
    }
    switch (action) {
    case ACT_PAUSE:
        a->paused = !a->paused;
        a->ff_target = 0;
        break;
    case ACT_STEP:
        if (!a->paused)
            a->paused = true;
        app_step(a);
        a->alpha = 1;
        break;
    case ACT_FASTER:
        a->speed = a->speed + 1 < k_nspeeds ? a->speed + 1 : a->speed;
        break;
    case ACT_SLOWER:
        a->speed = a->speed > 0 ? a->speed - 1 : 0;
        break;
    case ACT_NEXT_SEED:
        a->wo.seed++;
        /* fall through */
    case ACT_RESTART:
        if (app_make_world(a))
            interface_note(a, false, "Seed %lld: %d founders in %d x %d x %d",
                           (long long)a->wo.seed, a->w.population, a->w.width, a->w.height,
                           a->w.depth);
        break;
    case ACT_HOME:
        camera_home(a);
        break;
    case ACT_FOLLOW:
        if (a->follow != FOLLOW_NONE)
            a->follow = FOLLOW_NONE;
        else
            a->follow = a->selected >= 0 ? FOLLOW_SELECTED : FOLLOW_LIFE;
        break;
    case ACT_DESELECT:
        select_org(a, -1);
        break;
    case ACT_KILL: {
        Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
        if (o && o->alive) {
            org_die(o, NULL);
            syn_history_user(&a->w, 'k', 0, "You killed an organism");
            a->selected_was_alive = false;
            interface_note(a, false, "#%d killed", o->id);
        }
        break;
    }
    case ACT_THEME_NEXT:
    case ACT_THEME_PREV:
        shell_theme_step(action == ACT_THEME_NEXT ? 1 : -1);
        interface_note(a, false, "Theme %s", shell_theme_name());
        break;
    case ACT_CONSOLE:
        shell_console_toggle();
        break;
    case ACT_HINTS:
        a->hints = !a->hints;
        break;
    case ACT_QUIT:
        a->running = false;
        break;
    case ACT_WORLD_TAB:
        a->screen = SCREEN_WORLD;
        break;
    case ACT_LIBRARY_TAB:
        a->screen = a->screen == SCREEN_LIBRARY ? SCREEN_WORLD : SCREEN_LIBRARY;
        library_refresh(); /* read the folders again, as they are now */
        break;
    case ACT_PARAMS_TAB:
        a->screen = a->screen == SCREEN_PARAMS ? SCREEN_WORLD : SCREEN_PARAMS;
        break;
    case ACT_STATS_TAB:
        a->screen = a->screen == SCREEN_STATS ? SCREEN_WORLD : SCREEN_STATS;
        break;
    case ACT_ARENA_TAB:
        a->screen = a->screen == SCREEN_ARENA ? SCREEN_WORLD : SCREEN_ARENA;
        break;
    case ACT_SAVE: {
        char path[1300], err[300];
        if (app_save_world(a, NULL, path, sizeof path, err, sizeof err))
            interface_note(a, false, "Saved: %s", strrchr(path, '/') + 1);
        else
            interface_note(a, true, "%s", err);
        break;
    }
    case ACT_LAB_TAB:
        /* back to the code being made, if there is one, else the copied one */
        if (a->screen == SCREEN_LAB)
            a->screen = SCREEN_WORLD;
        else if (!a->lab.open)
            lab_open(a, a->clipboard);
        else
            a->screen = SCREEN_LAB;
        break;
    case ACT_GENES: {
        Organism *o = selected_org(a);
        if (o)
            lab_open(a, o->gc);
        else
            interface_note(a, true, "Select an organism first");
        break;
    }
    case ACT_BEING_LAB:
        if (a->menu_gc)
            lab_open(a, a->menu_gc);
        break;
    case ACT_BEING_WHERE:
        if (a->menu_gc)
            telescope_find(a, a->menu_gc, a->menu_name);
        break;
    case ACT_CREATE_WINNER:
        if (syn_winner_running())
            interface_note(a, true, "A winner is being worked out already");
        else
            winner_begin(a);
        break;
    case ACT_MINIMAP:
        minimap_toggle();
        break;
    case ACT_ZOOM_1:
        /* the picture's half height over the half angle */
        a->distance = fmax(10, a->win.height * 0.5 / tan(FOV_DEG * D_PI / 360));
        break;
    case ACT_LOOK_NEXT:
        look_set((Look)((look_current() + 1) % LOOK_COUNT));
        interface_note(a, false, "Look: %s", look_name(look_current()));
        break;
    case ACT_TELESCOPE_TAB:
        a->screen = a->screen == SCREEN_TELESCOPE ? SCREEN_WORLD : SCREEN_TELESCOPE;
        break;
    case ACT_WHERE: {
        Organism *o = selected_org(a);
        if (o) {
            char sp[64];
            stats_species(o, sp, sizeof sp);
            telescope_find(a, o->gc, sp[0] ? sp : "this organism");
        } else {
            interface_note(a, true, "Select an organism first");
        }
        break;
    }
    case ACT_BEING_COPY:
        if (a->menu_gc) {
            gc_assign(&a->clipboard, a->menu_gc);
            interface_note(a, false, "Genetic code copied: %s", a->menu_name);
        }
        break;
    case ACT_BEING_KEEP:
        if (a->menu_gc)
            app_library_keep(a, a->menu_gc, a->menu_folder, a->menu_name, a->menu_origin);
        break;
    case ACT_COMMANDS:
        shell_launcher_open();
        break;
    case ACT_MAIN_MENU:
        a->screen = SCREEN_MENU;
        break;
    case ACT_OPTIONS:
        a->screen = SCREEN_OPTIONS;
        break;
    case ACT_FULLSCREEN:
        a->fullscreen = !a->fullscreen;
        SDL_SetWindowFullscreen(a->win.window, a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        break;
    case ACT_MANUAL:
        if (SDL_OpenURL("http://biogenesis.sourceforge.net/manual.en.php") != 0)
            interface_note(a, true, "The manual didn't open: %s", SDL_GetError());
        break;
    case ACT_ABOUT:
        interface_note(a, false, "SYN-BIOGENESIS-3D: Biogenesis 0.9 (Joan Queralt Molina, 2006-2010) in three "
                                 "dimensions, by syn990. GPL 2 or later.");
        break;
    case ACT_NEW:
        a->wo.seed = (long long)time(NULL) % 1000000;
        a->wo.nimports = 0;
        if (app_make_world(a)) {
            app_do(a, ACT_HOME);
            interface_note(a, false, "A new world, seed %lld: %d founders in %d x %d x %d", (long long)a->wo.seed,
                           a->w.population, a->w.width, a->w.height, a->w.depth);
        }
        a->screen = SCREEN_WORLD;
        break;
    case ACT_OPEN:
        files_open(a, FILES_OPEN_WORLD);
        break;
    case ACT_SAVE_AS:
        files_open(a, FILES_SAVE_WORLD);
        break;
    case ACT_IMPORT:
        files_open(a, FILES_IMPORT_PASTE);
        break;
    case ACT_EXPORT:
        files_open(a, FILES_EXPORT_GENES);
        break;
    case ACT_SAVE_IMAGE:
        files_open(a, FILES_SAVE_IMAGE);
        break;
    case ACT_FAMILY: {
        Organism *o = selected_org(a);
        if (o)
            stats_family(a, o);
        break;
    }
    case ACT_INC_CO2:
        world_add_co2(&a->w, 500);
        syn_history_user(&a->w, 'c', 500, "You added CO2");
        interface_note(a, false, "500 more CO2 in the water: %.0f", a->w.co2);
        break;
    case ACT_DEC_CO2:
        world_take_co2(&a->w, 500);
        syn_history_user(&a->w, 'd', 500, "You took CO2 away");
        interface_note(a, false, "500 less CO2 in the water: %.0f", a->w.co2);
        break;
    case ACT_KILL_ALL:
        syn_history_user(&a->w, 0, a->w.population, "You killed every organism");
        world_kill_all(&a->w);
        a->selected_was_alive = false;
        interface_note(a, false, "Everything alive died where it was");
        break;
    case ACT_DISPERSE_ALL:
        syn_history_user(&a->w, 0, world_n_corpses(&a->w), "You dispersed every corpse");
        world_disperse_all(&a->w);
        interface_note(a, false, "Every corpse breathed out what it held");
        break;
    case ACT_CREATE_RANDOM: {
        GeneticCode *gc = gc_new_random();
        if (app_paste(a, gc, app_take_place(a))) {
            interface_note(a, false, "#%d: a random organism", a->selected);
            syn_history_user(&a->w, 'n', 0, "You created a random organism");
        }
        gc_unref(gc);
        break;
    }
    case ACT_PASTE:
        if (!a->clipboard)
            interface_note(a, true, "Nothing copied yet: Copy on an organism, or a being's menu");
        else if (app_paste(a, a->clipboard, app_take_place(a))) {
            interface_note(a, false, "#%d: the copied code, pasted", a->selected);
            syn_history_user(&a->w, 'p', 0, "You placed a copied organism");
        }
        break;
    case ACT_FEED: {
        /* as in 0.9: the CO2 is added, not taken */
        Organism *o = selected_org(a);
        if (o && o->alive) {
            double q = bio_min(10, a->w.co2);
            world_add_co2(&a->w, q);
            o->energy += q;
            world_add_o2(&a->w, q);
            syn_history_user(&a->w, 'f', 0, "You fed an organism");
        }
        break;
    }
    case ACT_WEAKEN: {
        Organism *o = selected_org(a);
        if (o && o->alive) {
            org_use_energy(o, o->energy / 2);
            syn_history_user(&a->w, 'w', 0, "You weakened an organism");
        }
        break;
    }
    case ACT_REPRODUCE: {
        Organism *o = selected_org(a);
        if (o && o->alive) {
            org_reproduce(o);
            syn_history_user(&a->w, 'r', 0, "You made an organism reproduce");
        }
        break;
    }
    case ACT_REJUVENATE: {
        Organism *o = selected_org(a);
        if (o && o->alive) {
            o->age = 0;
            syn_history_user(&a->w, 'j', 0, "You rejuvenated an organism");
        }
        break;
    }
    case ACT_COPY: {
        Organism *o = selected_org(a);
        if (o) {
            gc_assign(&a->clipboard, o->gc);
            interface_note(a, false, "#%d's genetic code copied: paste it with Ctrl+V", o->id);
        }
        break;
    }
    case ACT_REVIVE: {
        Organism *o = selected_org(a);
        if (o && !o->alive) {
            org_revive(o);
            a->selected_was_alive = true;
            syn_history_user(&a->w, 'v', 0, "You revived a corpse");
        }
        break;
    }
    case ACT_DISPERSE: {
        Organism *o = selected_org(a);
        if (o && !o->alive) {
            org_use_energy(o, o->energy);
            syn_history_user(&a->w, 'x', 0, "You dispersed a corpse");
        }
        break;
    }
    case ACT_ZOOM_IN:
        a->distance = fmax(10, a->distance / 1.25);
        break;
    case ACT_ZOOM_OUT:
        a->distance *= 1.25;
        break;
    case ACT_UNTRACK:
        if (a->follow == FOLLOW_SELECTED)
            a->follow = FOLLOW_NONE;
        break;
    case ACT_KEEP: {
        Organism *o = selected_org(a);
        if (!o) {
            interface_note(a, true, "Nothing selected to keep");
            break;
        }
        char sp[64], origin[200];
        stats_species(o, sp, sizeof sp);
        const char *f = strrchr(a->world_path, '/');
        if (a->world_path[0])
            snprintf(origin, sizeof origin, "%.120s, organism %d at time %lld", f ? f + 1 : a->world_path, o->id,
                     (long long)world_time_units(&a->w));
        else
            snprintf(origin, sizeof origin, "seed %lld, organism %d at time %lld", (long long)a->wo.seed, o->id,
                     (long long)world_time_units(&a->w));
        app_library_keep(a, o->gc, "Saved", sp[0] ? sp : "ORGANISM", origin);
        break;
    }
    }
}

/* ---- the loop ---- */

static void usage(FILE *f)
{
    fputs("usage: syn-biogenesis-3d [options]          the world in a window\n"
          "       syn-biogenesis-3d --headless [...]   the world as CSV (--headless --help)\n"
          "       syn-biogenesis-3d --arena [...]      the arena's ladder without a window\n"
          "                                            (--arena --help)\n" WORLD_OPTIONS_HELP
          "  --warm UNITS      time units to run before showing it (default 0)\n"
          "  --shot FILE.ppm   draw one picture of the world, save it and quit\n"
          "  --camera Y P D    start the camera at yaw Y, pitch P degrees, distance D,\n"
          "                    aimed at the life\n"
          "  --select ID       start with organism ID selected (-1: the biggest living)\n"
          "  --screen NAME     start on a screen: world, library, parameters, statistics,\n"
          "                    arena, menu, options, laboratory (the selected one's\n"
          "                    genes), telescope (default: the main menu)\n"
          "  --load NAME       open a saved world (a name in the worlds folder, or a path)\n"
          "  --goto T          run to time T as fast as it goes, and pause there\n"
          "  --seconds S       quit after S seconds and print the frame rate (with\n"
          "                    --shot, the picture is taken then)\n"
          "  --window W H      window size (default 1440 900)\n"
          "\n"
          "  drag: turn   right-drag: move   wheel: zoom   click: select   right-click: menu\n"
          "  space: pause   .: one frame   up/down: speed   f: follow   `: console\n"
          "  n: next seed   r: restart   home: whole box   t: theme   h: key hints\n"
          "  l: library   p: parameters   s: statistics   a: arena   ctrl+s: save the world\n"
          "  esc: drop the selection, then quit\n",
          f);
}

/* Test hook: SYN3D_SCRIPT plays input as a person would give it, commands
 * split by ';': wait N (frames) | move X Y | click X Y [l|r] | down X Y |
 * up X Y | wheel N | key NAME | type TEXT | shot FILE.ppm (this frame's
 * picture) | quit. With SDL_VIDEODRIVER=offscreen it drives the interface
 * without a screen, as SYN-BIOGENESIS's SYN_BIOGENESIS_SCRIPT does. */
typedef struct {
    char *buf;
    char *cmd[512];
    int n, i, wait, phase;
    char shot[1024];
} Script;

static void script_load(Script *s, const char *text)
{
    memset(s, 0, sizeof *s);
    s->buf = strdup(text);
    for (char *tok = s->buf ? strtok(s->buf, ";") : NULL; tok && s->n < 512; tok = strtok(NULL, ";")) {
        while (*tok == ' ')
            tok++;
        if (*tok)
            s->cmd[s->n++] = tok;
    }
}

static void push_mouse(Uint32 window, Uint32 type, int x, int y, int button)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.motion.windowID = window;
    if (type == SDL_MOUSEMOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.x = x;
        e.button.y = y;
        e.button.button = (Uint8)button;
        e.button.clicks = 1;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    }
    SDL_PushEvent(&e);
}

static void script_step(App *a, Script *s)
{
    if (s->wait > 0) {
        s->wait--;
        return;
    }
    if (s->i >= s->n)
        return;
    Uint32 win = SDL_GetWindowID(a->win.window);
    const char *c = s->cmd[s->i];
    int x = 0, y = 0, n = 0;
    char arg[256] = "";
    if (sscanf(c, "wait %d", &n) == 1) {
        s->wait = n;
        s->i++;
    } else if (sscanf(c, "move %d %d", &x, &y) == 2) {
        push_mouse(win, SDL_MOUSEMOTION, x, y, 0);
        s->i++;
    } else if (sscanf(c, "click %d %d %255s", &x, &y, arg) >= 2) {
        int b = arg[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        if (s->phase == 0) {
            push_mouse(win, SDL_MOUSEMOTION, x, y, 0);
            push_mouse(win, SDL_MOUSEBUTTONDOWN, x, y, b);
            s->phase = 1;
        } else {
            push_mouse(win, SDL_MOUSEBUTTONUP, x, y, b);
            s->phase = 0;
            s->i++;
            s->wait = 1;
        }
    } else if (sscanf(c, "down %d %d", &x, &y) == 2) {
        push_mouse(win, SDL_MOUSEMOTION, x, y, 0);
        push_mouse(win, SDL_MOUSEBUTTONDOWN, x, y, SDL_BUTTON_LEFT);
        s->i++;
    } else if (sscanf(c, "up %d %d", &x, &y) == 2) {
        push_mouse(win, SDL_MOUSEMOTION, x, y, 0);
        push_mouse(win, SDL_MOUSEBUTTONUP, x, y, SDL_BUTTON_LEFT);
        s->i++;
        s->wait = 1;
    } else if (sscanf(c, "wheel %d", &n) == 1) {
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = SDL_MOUSEWHEEL;
        e.wheel.windowID = win;
        e.wheel.y = n;
        e.wheel.preciseY = (float)n;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        SDL_PushEvent(&e);
        s->i++;
        s->wait = 1;
    } else if (sscanf(c, "key %255s", arg) == 1) {
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = SDL_KEYDOWN;
        e.key.windowID = win;
        e.key.keysym.sym = SDL_GetKeyFromName(arg);
        SDL_PushEvent(&e);
        e.type = SDL_KEYUP;
        SDL_PushEvent(&e);
        s->i++;
        s->wait = 1;
    } else if (!strncmp(c, "type ", 5)) {
        /* sdl2-compat can't carry pushed SDL_TEXTINPUT events to SDL3 */
        uiin_inject_text(c + 5);
        s->i++;
        s->wait = 1;
    } else if (sscanf(c, "shot %1023s", s->shot) == 1) {
        s->i++;
    } else if (!strcmp(c, "quit")) {
        a->running = false;
        s->i++;
    } else {
        fprintf(stderr, "SYN3D_SCRIPT: what is \"%s\"?\n", c);
        s->i++;
    }
}

/* fonts/ and themes/: beside the program, or where it is run from */
static const char *find_assets(void)
{
    static char path[1024];
    char *base = SDL_GetBasePath();
    const char *tries[3] = {base, "./", NULL};
    for (int i = 0; i < 2; i++) {
        if (!tries[i])
            continue;
        snprintf(path, sizeof path, "%sassets/fonts/NotoSans-Regular.ttf", tries[i]);
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            snprintf(path, sizeof path, "%sassets", tries[i]);
            SDL_free(base);
            return path;
        }
    }
    SDL_free(base);
    return NULL;
}

static void handle_key(App *a, SDL_Keycode k, SDL_Keymod mods)
{
    bool ctrl = (mods & KMOD_CTRL) != 0;
    /* a screen over the world has the keys but these: the world's own keys
     * (f feeds, + zooms ...) would act on what isn't shown */
    if (a->screen != SCREEN_WORLD && k != SDLK_ESCAPE && k != SDLK_SLASH && k != SDLK_BACKQUOTE &&
        k != SDLK_F11 && !(ctrl && (k == SDLK_q || k == SDLK_s || k == SDLK_o)))
        return;
    switch (k) {
    case SDLK_q:
        if (ctrl)
            app_do(a, ACT_QUIT);
        break;
    case SDLK_ESCAPE:
        /* the selection first, then the main menu; a screen goes back to the world */
        if (a->screen == SCREEN_MENU)
            a->screen = SCREEN_WORLD;
        else if (a->screen == SCREEN_LAB)
            lab_close(a); /* as Cancel */
        else if (a->screen == SCREEN_FILES)
            ; /* the browser cancels itself */
        else if (a->screen != SCREEN_WORLD)
            app_do(a, ACT_WORLD_TAB);
        else if (a->selected >= 0)
            app_do(a, ACT_DESELECT);
        else if (a->interface)
            app_do(a, ACT_MAIN_MENU);
        else
            app_do(a, ACT_QUIT);
        break;
    case SDLK_SLASH:
        if (a->interface)
            app_do(a, ACT_COMMANDS);
        break;
    case SDLK_o:
        if (ctrl)
            app_do(a, ACT_OPEN);
        break;
    case SDLK_s:
        app_do(a, ctrl && (mods & KMOD_SHIFT) ? ACT_SAVE_AS : ACT_SAVE);
        break;
    case SDLK_c:
        if (ctrl)
            app_do(a, ACT_COPY);
        break;
    case SDLK_v:
        if (ctrl) {
            float mx, my;
            uiin_mouse(&mx, &my);
            a->place = app_point_under(a, mx, my);
            a->place_set = true;
            app_do(a, ACT_PASTE);
        }
        break;
    case SDLK_p:
    case SDLK_SPACE:
        app_do(a, ACT_PAUSE);
        break;
    case SDLK_DELETE:
        app_do(a, ACT_KILL);
        break;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS:
        app_do(a, ACT_ZOOM_IN);
        break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        app_do(a, ACT_ZOOM_OUT);
        break;
    case SDLK_F11:
        app_do(a, ACT_FULLSCREEN);
        break;
    case SDLK_PERIOD:
        app_do(a, ACT_STEP);
        break;
    case SDLK_UP:
        app_do(a, ACT_FASTER);
        break;
    case SDLK_DOWN:
        app_do(a, ACT_SLOWER);
        break;
    case SDLK_f:
        app_do(a, ACT_FEED);
        break;
    case SDLK_n:
        app_do(a, ACT_NEXT_SEED);
        break;
    case SDLK_r:
        app_do(a, ACT_RESTART);
        break;
    case SDLK_HOME:
        app_do(a, ACT_HOME);
        break;
    case SDLK_t:
        app_do(a, ACT_FOLLOW);
        break;
    case SDLK_h:
        app_do(a, ACT_HINTS);
        break;
    case SDLK_BACKQUOTE:
        if (a->interface)
            app_do(a, ACT_CONSOLE);
        break;
    case SDLK_k:
        app_do(a, ACT_KILL);
        break;
    case SDLK_g:
        app_do(a, ACT_GENES);
        break;
    case SDLK_l:
        app_do(a, ACT_LOOK_NEXT);
        break;
    case SDLK_m:
        app_do(a, ACT_MINIMAP);
        break;
    case SDLK_0:
    case SDLK_KP_0:
        app_do(a, ACT_ZOOM_1);
        break;
    }
}

int app_main(int argc, char **argv)
{
    static App app;
    App *a = &app;
    memset(a, 0, sizeof *a);
    world_options_init(&a->wo);
    /* the parameters as the window last set them (--set overrides them) */
    params_load(&P);
    const char *shot = NULL;
    int win_w = 1440, win_h = 900, select_at_start = -2;
    double cam[3] = {0, 0, 0}; /* yaw, pitch, distance from --camera; 0 distance keeps home */
    double run_for = 0;        /* --seconds: quit after this long and report the frame rate */
    const char *load = NULL;
    long long goto_time = 0;
    bool screen_given = false;
    for (int i = 1; i < argc; i++) {
        if (world_option(&a->wo, argc, argv, &i))
            continue;
        const char *s = argv[i];
        if (strcmp(s, "--warm") == 0 && i + 1 < argc)
            a->warm = atoi(argv[++i]);
        else if (strcmp(s, "--shot") == 0 && i + 1 < argc)
            shot = argv[++i];
        else if (strcmp(s, "--seconds") == 0 && i + 1 < argc)
            run_for = atof(argv[++i]);
        else if (strcmp(s, "--load") == 0 && i + 1 < argc)
            load = argv[++i];
        else if (strcmp(s, "--goto") == 0 && i + 1 < argc)
            goto_time = atoll(argv[++i]);
        else if (strcmp(s, "--select") == 0 && i + 1 < argc)
            select_at_start = atoi(argv[++i]);
        else if (strcmp(s, "--screen") == 0 && i + 1 < argc) {
            const char *n = argv[++i];
            screen_given = true;
            a->screen = !strcmp(n, "library")                             ? SCREEN_LIBRARY
                        : !strcmp(n, "parameters") || !strcmp(n, "params") ? SCREEN_PARAMS
                        : !strcmp(n, "statistics") || !strcmp(n, "stats")  ? SCREEN_STATS
                        : !strcmp(n, "arena")                              ? SCREEN_ARENA
                        : !strcmp(n, "menu")                               ? SCREEN_MENU
                        : !strcmp(n, "options")                            ? SCREEN_OPTIONS
                        : !strcmp(n, "laboratory") || !strcmp(n, "lab")    ? SCREEN_LAB
                        : !strcmp(n, "telescope")                          ? SCREEN_TELESCOPE
                                                                           : SCREEN_WORLD;
        }
        else if (strcmp(s, "--camera") == 0 && i + 3 < argc) {
            cam[0] = atof(argv[++i]);
            cam[1] = atof(argv[++i]);
            cam[2] = atof(argv[++i]);
        } else if (strcmp(s, "--window") == 0 && i + 2 < argc) {
            win_w = atoi(argv[++i]);
            win_h = atoi(argv[++i]);
        } else if (strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "syn-biogenesis-3d: unknown option %s\n", s);
            usage(stderr);
            return 2;
        }
    }
    char err[512];
    if (!world_options_check(err, sizeof err)) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return 2;
    }
    if (!app_make_world(a))
        return 1;
    if (load && !app_load_world(a, load, err, sizeof err)) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return 1;
    }
    if (!platform_window_create(&a->win, "SYN-BIOGENESIS-3D", win_w, win_h))
        return 1;
    lighting_init();
    bodies_init();
    water_init();
    lines_init();
    gl2d_init();
    const char *assets = find_assets();
    a->interface = assets && interface_init(a, assets);
    if (a->screen == SCREEN_ARENA)
        arena_shot_prepare(); /* only with its SYN3D_SHOT_ARENA variables */
    if (!a->interface)
        fprintf(stderr, "syn-biogenesis-3d: no assets (fonts, themes): no SYN-OS interface\n");
    a->hints = true;
    /* SYN-OS opens on its main menu, over the living world */
    if (a->interface && !screen_given && !shot && !load && run_for <= 0)
        a->screen = SCREEN_MENU;
    camera_home(a);
    if (cam[2] > 0) {
        a->yaw = cam[0];
        a->pitch = cam[1];
        a->distance = cam[2];
        a->follow = FOLLOW_LIFE;
        a->aim = world_life_centre_or_box(&a->w);
    }
    if (select_at_start == -1) {
        /* the biggest living organism (for pictures) */
        double most = 0;
        for (int i = 0; i < a->w.norgs; i++)
            if (a->w.orgs[i]->alive && a->w.orgs[i]->mass > most) {
                most = a->w.orgs[i]->mass;
                select_at_start = a->w.orgs[i]->id;
            }
    }
    if (select_at_start >= 0 && app_find(&a->w, select_at_start)) {
        select_org(a, select_at_start);
        a->aim = app_find(&a->w, select_at_start)->pos;
    }
    if (a->screen == SCREEN_LAB) {
        Organism *o = selected_org(a);
        lab_open(a, o ? o->gc : NULL);
    }
    a->speed = 3; /* 60 world frames a second */
    a->alpha = 1;
    a->running = true;
    if (a->loaded[0])
        interface_note(a, false, "%s: frame %lld, %d alive in %d x %d x %d", a->loaded,
                       (long long)a->w.frame, a->w.population, a->w.width, a->w.height, a->w.depth);
    else
        interface_note(a, false, "Seed %lld: %d organisms in %d x %d x %d", (long long)a->wo.seed,
                       a->w.population, a->w.width, a->w.height, a->w.depth);

    if (goto_time > 0)
        app_run_to(a, goto_time * 256);
    if (shot && run_for <= 0) {
        platform_window_refresh_size(&a->win);
        draw(a);
        bool ok = platform_window_shot(&a->win, shot);
        fprintf(stderr, ok ? "%s\n" : "could not write %s\n", shot);
        return ok ? 0 : 1;
    }

    bool dragging = false, panning = false, moved = false;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    Uint64 start = last;
    Uint32 last_fps = SDL_GetTicks();
    int frames_since = 0;
    long frames_total = 0;
    Script script;
    const char *script_text = getenv("SYN3D_SCRIPT");
    if (script_text)
        script_load(&script, script_text);
    while (a->running) {
        /* the arena's rounds go on whatever is on screen */
        syn_arena_poll();
        winner_poll(a);
        int ww, wh;
        SDL_GetWindowSize(a->win.window, &ww, &wh);
        float scale = ww > 0 ? (float)a->win.width / (float)ww : 1;
        uiin_begin_frame(scale);
        if (script_text)
            script_step(a, &script);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                a->running = false;
                continue;
            }
            if (uiin_event(&e))
                continue; /* the interface's */
            switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
                dragging = e.button.button == SDL_BUTTON_LEFT;
                panning = e.button.button == SDL_BUTTON_RIGHT;
                moved = false;
                break;
            case SDL_MOUSEBUTTONUP:
                if (!moved && (dragging || panning)) {
                    int id = pick(a, e.button.x * scale, e.button.y * scale);
                    if (e.button.button == SDL_BUTTON_LEFT)
                        select_org(a, id);
                    else if (a->interface) {
                        a->place = app_point_under(a, e.button.x * scale, e.button.y * scale);
                        a->place_set = true;
                        interface_context_menu(a, e.button.x * scale, e.button.y * scale, id);
                    }
                }
                dragging = panning = false;
                break;
            case SDL_MOUSEMOTION:
                if ((dragging || panning) && abs(e.motion.xrel) + abs(e.motion.yrel) > 1)
                    moved = true;
                if (dragging) {
                    a->yaw -= e.motion.xrel * 0.3;
                    a->pitch += e.motion.yrel * 0.3;
                    a->pitch = a->pitch < -85 ? -85 : a->pitch > 85 ? 85 : a->pitch;
                } else if (panning) {
                    double k = a->distance * 0.0015;
                    a->aim = v3_add(a->aim, v3_add(v3_scale(a->cam_right, -e.motion.xrel * k),
                                                   v3_scale(a->cam_up, e.motion.yrel * k)));
                    a->follow = FOLLOW_NONE;
                }
                break;
            case SDL_MOUSEWHEEL:
                a->distance *= e.wheel.y > 0 ? 1 / 1.12 : 1.12;
                a->distance = a->distance < 10 ? 10 : a->distance;
                break;
            case SDL_KEYDOWN:
                handle_key(a, e.key.keysym.sym, (SDL_Keymod)e.key.keysym.mod);
                break;
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        a->seconds = (double)(now - start) / freq;
        if (dt > 0.25)
            dt = 0.25;
        if (a->ff_target) {
            /* running to a frame: as many as fit, then stop there */
            Uint64 budget = now + freq / 60;
            while (a->w.frame < a->ff_target && SDL_GetPerformanceCounter() < budget)
                app_step(a);
            a->alpha = 1;
            if (a->w.frame >= a->ff_target) {
                a->ff_target = 0;
                a->paused = true;
                interface_note(a, false, "Time %lld: %d alive (paused)",
                               (long long)(a->w.frame / 256), a->w.population);
            }
        } else if (!a->paused) {
            int rate = k_speeds[a->speed];
            Uint64 budget = now + freq / 80; /* stop stepping after ~12 ms */
            if (rate) {
                a->acc += dt * rate;
                while (a->acc >= 1 && SDL_GetPerformanceCounter() < budget) {
                    app_step(a);
                    a->acc -= 1;
                }
                if (a->acc > 4)
                    a->acc = 1; /* behind: drop the debt rather than spiral */
                a->alpha = a->acc < 1 ? a->acc : 1;
            } else {
                while (SDL_GetPerformanceCounter() < budget)
                    app_step(a);
                a->alpha = 1;
            }
        }

        /* the selection: still there, still alive? */
        Organism *sel = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
        if (a->selected >= 0 && !sel) {
            interface_note(a, false, "#%d has decayed away", a->selected);
            select_org(a, -1);
        } else if (sel && a->selected_was_alive && !sel->alive) {
            interface_note(a, true, "#%d died", sel->id);
            a->selected_was_alive = false;
        }
        V3 target = a->aim;
        if (a->follow == FOLLOW_SELECTED && sel) {
            Quat q;
            app_draw_pose(sel, a->alpha, &target, &q);
        } else if (a->follow == FOLLOW_LIFE) {
            target = world_life_centre_or_box(&a->w);
        }
        double k = 1 - exp(-dt * (a->follow == FOLLOW_SELECTED ? 8.0 : 2.0));
        a->aim = v3_add(a->aim, v3_scale(v3_sub(target, a->aim), k));

        platform_window_refresh_size(&a->win);
        draw(a);
        if (shot && run_for > 0 && a->seconds >= run_for)
            fprintf(stderr, platform_window_shot(&a->win, shot) ? "%s\n" : "could not write %s\n",
                    shot);
        if (script_text && script.shot[0]) {
            if (!platform_window_shot(&a->win, script.shot))
                fprintf(stderr, "could not write %s\n", script.shot);
            script.shot[0] = 0;
        }
        platform_window_swap(&a->win);

        frames_since++;
        frames_total++;
        Uint32 ms = SDL_GetTicks();
        if (ms - last_fps >= 500) {
            a->fps = frames_since * 1000.0 / (ms - last_fps);
            last_fps = ms;
            frames_since = 0;
        }
        if (run_for > 0 && a->seconds >= run_for) {
            int segs = 0;
            for (int i = 0; i < a->w.norgs; i++)
                segs += a->w.orgs[i]->segments;
            fprintf(stderr,
                    "%ld pictures in %.1f s: %.1f fps, %d segments, %d organisms, time %lld; drawn: %d fine, %d "
                    "coarse, %d lines\n",
                    frames_total, a->seconds, frames_total / a->seconds, segs, a->w.norgs,
                    (long long)(a->w.frame / 256), a->ntier[0], a->ntier[1], a->ntier[2]);
            fprintf(stderr, "frame %lld, %d alive, hash %016llx\n", (long long)a->w.frame,
                    a->w.population, (unsigned long long)world_hash(&a->w));
            a->running = false;
        }
    }
    if (script_text)
        free(script.buf);
    syn_arena_free();
    arena_screen_free();
    stats_screen_free();
    syn_history_free();
    gc_unref(a->clipboard);
    gc_unref(a->menu_gc);
    library_screen_free();
    syn_winner_cancel();
    telescope_screen_free();
    if (a->interface)
        interface_shutdown();
    gl2d_shutdown();
    lines_destroy();
    bodies_destroy();
    water_destroy();
    lighting_destroy();
    platform_window_destroy(&a->win);
    free(a->inst);
    free(a->inst_org);
    free(a->shadows);
    free(a->points);
    for (int t = 0; t < 3; t++) {
        free(a->tier[t]);
        free(a->tier_org[t]);
    }
    free(a->lines);
    world_free(&a->w);
    return 0;
}
