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
#include "io/worldfile.h"
#include "render/lighting.h"
#include "sim/params.h"
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

bool app_make_world(App *a)
{
    char err[512];
    if (world_options_genesis(&a->wo, &a->w, err, sizeof err) < 0) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return false;
    }
    history_reset(&a->hist, &a->w);
    a->loaded[0] = 0;
    for (long f = 0; f < (long)a->warm * 256; f++)
        app_step(a);
    a->selected = -1;
    a->acc = 0;
    return true;
}

static void worlds_dir(char *out, size_t n)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(out, n, "%s/syn-biogenesis-3d/worlds", xdg);
    else
        snprintf(out, n, "%s/.config/syn-biogenesis-3d/worlds", home ? home : ".");
}

bool app_save_world(App *a, const char *name, char *path, size_t pathlen, char *err, int errlen)
{
    char dir[1024];
    worlds_dir(dir, sizeof dir);
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
    return worldfile_save(&a->w, path, err, errlen);
}

bool app_load_world(App *a, const char *name, char *err, int errlen)
{
    char path[1200];
    if (strchr(name, '/')) {
        snprintf(path, sizeof path, "%s", name);
    } else {
        char dir[1024];
        worlds_dir(dir, sizeof dir);
        size_t n = strlen(name);
        bool ext = n > 4 && !strcmp(name + n - 4, "." WORLDFILE_EXT);
        snprintf(path, sizeof path, "%s/%s%s", dir, name, ext ? "" : "." WORLDFILE_EXT);
    }
    if (!worldfile_load(&a->w, path, err, errlen))
        return false;
    const char *base = strrchr(path, '/');
    snprintf(a->loaded, sizeof a->loaded, "%.120s", base ? base + 1 : path);
    history_reset(&a->hist, &a->w);
    a->selected = -1;
    a->acc = 0;
    return true;
}

void app_list_worlds(void (*line)(const char *s))
{
    char dir[1024];
    worlds_dir(dir, sizeof dir);
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

void app_step(App *a)
{
    world_time(&a->w);
    history_frame(&a->hist, &a->w);
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

static void build_instances(App *a)
{
    const World *w = &a->w;
    a->ninst = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        V3 pos;
        Quat q;
        app_draw_pose(o, a->alpha, &pos, &q);
        M3 R = m3_from_quat(q);
        bool selected = o->id == a->selected;
        float flash = o->frames_color > 0 && o->flash_color != FLASH_NONE
                          ? (float)o->frames_color / 10.0f
                          : 0.0f;
        for (int s = 0; s < o->segments; s++) {
            if (a->ninst == a->inst_cap) {
                a->inst_cap = a->inst_cap ? a->inst_cap * 2 : 8192;
                a->inst = realloc(a->inst, (size_t)a->inst_cap * sizeof *a->inst);
                a->inst_org = realloc(a->inst_org, (size_t)a->inst_cap * sizeof *a->inst_org);
                if (!a->inst || !a->inst_org)
                    abort();
            }
            CapsuleInstance *ci = &a->inst[a->ninst];
            a->inst_org[a->ninst] = o->id;
            V3 pa = v3_add(pos, m3_mul_v(&R, o->bs[s])), pb = v3_add(pos, m3_mul_v(&R, o->be[s]));
            ci->a[0] = (float)pa.x;
            ci->a[1] = (float)pa.y;
            ci->a[2] = (float)pa.z;
            ci->b[0] = (float)pb.x;
            ci->b[1] = (float)pb.y;
            ci->b[2] = (float)pb.z;
            int col = flash > 0 ? o->flash_color : o->seg_color[s];
            uint8_t r, g, b;
            bio_color_rgb(col, &r, &g, &b);
            /* 0.9's colours, a little under full so the light has room */
            float k = col == COL_BROWN ? 0.55f : 0.8f;
            ci->rgb[0] = r / 255.0f * k;
            ci->rgb[1] = g / 255.0f * k;
            ci->rgb[2] = b / 255.0f * k;
            ci->radius = (float)P.segment_radius;
            ci->glow = flash * 0.9f;
            if (selected)
                ci->glow += 0.25f + 0.15f * (float)sin(a->seconds * 5.0);
            a->ninst++;
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

/* The box and the floor in the theme's colours: its border for the grid,
 * its accent for the box's edges and the selection's marks. */
static void build_lines(App *a)
{
    const World *w = &a->w;
    const Theme *t = skin_theme();
    a->nlines = 0;
    float grid[4], edge[4], mark[4];
    rgba_of(t->border, 0.30f, grid);
    rgba_of(t->accent, 0.65f, edge);
    rgba_of(t->accent, 0.9f, mark);
    double step = 100;
    while ((w->width / step) > 40 || (w->height / step) > 40)
        step *= 2;
    for (double x = 0; x <= w->width + 1e-9; x += step)
        line(a, v3(x, 0, 0), v3(x, w->height, 0), grid);
    for (double y = 0; y <= w->height + 1e-9; y += step)
        line(a, v3(0, y, 0), v3(w->width, y, 0), grid);
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
    float near = (float)fmax(0.5, a->distance * 0.01), far = (float)(a->distance + diag * 1.5);
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
    memcpy(a->view.eye, e, sizeof a->view.eye);
    V3 l = v3_unit_or(v3(0.35, 0.25, 1.0), v3(0, 0, 1));
    a->view.light_dir[0] = (float)l.x;
    a->view.light_dir[1] = (float)l.y;
    a->view.light_dir[2] = (float)l.z;
    /* the water is the theme's background */
    const Theme *t = skin_theme();
    a->view.fog_rgb[0] = t->bg.r / 255.0f;
    a->view.fog_rgb[1] = t->bg.g / 255.0f;
    a->view.fog_rgb[2] = t->bg.b / 255.0f;
    /* two steps of the depth buffer's precision at the aim (near is a
     * hundredth of the distance, 24 bits of depth) */
    a->view.depth_step = (float)(a->distance * 1.2e-5);
    a->view.fog_near = (float)(a->distance * 0.6);
    a->view.fog_far = (float)(a->distance + diag);
}

static void draw(App *a)
{
    PlatformWindow *pw = &a->win;
    set_view(a);
    build_instances(a);
    build_lines(a);
    lighting_scene_begin(pw->width, pw->height, a->view.fog_rgb[0], a->view.fog_rgb[1],
                         a->view.fog_rgb[2]);
    capsules_draw(a->inst, a->ninst, &a->view);
    lines_draw(a->lines, a->nlines, &a->view);
    lighting_scene_end();
    if (a->interface)
        interface_draw(a, pw->width, pw->height);
}

/* ---- picking ---- */

/* The organism under drawable pixel (mx, my): the nearest capsule the ray
 * through it passes within reach of. */
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
    double reach = P.segment_radius * 2.5 + a->distance * 0.004;
    for (int i = 0; i < a->ninst; i++) {
        const CapsuleInstance *c = &a->inst[i];
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
    switch (action) {
    case ACT_PAUSE:
        a->paused = !a->paused;
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
        a->lib_read = false; /* read the folders again, as they are now */
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
    case ACT_KEEP: {
        Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
        char path[1100], err[300];
        if (!o)
            interface_note(a, true, "Nothing selected to keep");
        else if (library_keep(o, (long long)a->wo.seed, path, sizeof path, err, sizeof err))
            interface_note(a, false, "Kept: %s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
        else
            interface_note(a, true, "%s", err);
        break;
    }
    }
}

/* ---- the loop ---- */

static void usage(FILE *f)
{
    fputs("usage: syn-biogenesis-3d [options]          the world in a window\n"
          "       syn-biogenesis-3d --headless [...]   the world as CSV (--headless --help)\n" WORLD_OPTIONS_HELP
          "  --warm UNITS      time units to run before showing it (default 0)\n"
          "  --shot FILE.ppm   draw one picture of the world, save it and quit\n"
          "  --camera Y P D    start the camera at yaw Y, pitch P degrees, distance D,\n"
          "                    aimed at the life\n"
          "  --select ID       start with organism ID selected (-1: the biggest living)\n"
          "  --screen NAME     start on a screen: world, library, parameters, statistics,\n"
          "                    arena\n"
          "  --load NAME       open a saved world (a name in the worlds folder, or a path)\n"
          "  --arena A B       fight library beings A and B (names) in the arena\n"
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
    switch (k) {
    case SDLK_q:
        app_do(a, ACT_QUIT);
        break;
    case SDLK_ESCAPE:
        if (a->screen != SCREEN_WORLD)
            app_do(a, ACT_WORLD_TAB);
        else
            app_do(a, a->selected >= 0 ? ACT_DESELECT : ACT_QUIT);
        break;
    case SDLK_l:
        if (a->interface)
            app_do(a, ACT_LIBRARY_TAB);
        break;
    case SDLK_p:
        if (a->interface)
            app_do(a, ACT_PARAMS_TAB);
        break;
    case SDLK_s:
        if (mods & KMOD_CTRL)
            app_do(a, ACT_SAVE);
        else if (a->interface)
            app_do(a, ACT_STATS_TAB);
        break;
    case SDLK_a:
        if (a->interface)
            app_do(a, ACT_ARENA_TAB);
        break;
    case SDLK_SPACE:
        app_do(a, ACT_PAUSE);
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
        app_do(a, ACT_FOLLOW);
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
        app_do(a, mods & KMOD_SHIFT ? ACT_THEME_PREV : ACT_THEME_NEXT);
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
    }
}

int app_main(int argc, char **argv)
{
    static App app;
    App *a = &app;
    memset(a, 0, sizeof *a);
    world_options_init(&a->wo);
    const char *shot = NULL;
    int win_w = 1440, win_h = 900, select_at_start = -2;
    double cam[3] = {0, 0, 0}; /* yaw, pitch, distance from --camera; 0 distance keeps home */
    double run_for = 0;        /* --seconds: quit after this long and report the frame rate */
    const char *fight[2] = {NULL, NULL};
    const char *load = NULL;
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
        else if (strcmp(s, "--arena") == 0 && i + 2 < argc) {
            fight[0] = argv[++i];
            fight[1] = argv[++i];
        }
        else if (strcmp(s, "--select") == 0 && i + 1 < argc)
            select_at_start = atoi(argv[++i]);
        else if (strcmp(s, "--screen") == 0 && i + 1 < argc) {
            const char *n = argv[++i];
            a->screen = !strcmp(n, "library")                             ? SCREEN_LIBRARY
                        : !strcmp(n, "parameters") || !strcmp(n, "params") ? SCREEN_PARAMS
                        : !strcmp(n, "statistics") || !strcmp(n, "stats")  ? SCREEN_STATS
                        : !strcmp(n, "arena")                              ? SCREEN_ARENA
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
    capsules_init();
    lines_init();
    gl2d_init();
    a->arena = arena_new();
    const char *assets = find_assets();
    a->interface = assets && interface_init(a, assets);
    if (!a->interface)
        fprintf(stderr, "syn-biogenesis-3d: no assets (fonts, themes): no SYN-OS interface\n");
    a->hints = true;
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
    a->speed = 3; /* 60 world frames a second */
    a->alpha = 1;
    a->running = true;
    if (a->loaded[0])
        interface_note(a, false, "%s: frame %lld, %d alive in %d x %d x %d", a->loaded,
                       (long long)a->w.frame, a->w.population, a->w.width, a->w.height, a->w.depth);
    else
        interface_note(a, false, "Seed %lld: %d organisms in %d x %d x %d", (long long)a->wo.seed,
                       a->w.population, a->w.width, a->w.height, a->w.depth);

    if (fight[0] && !(a->interface && arena_start(a, fight[0], fight[1])))
        fprintf(stderr, "syn-biogenesis-3d: no such beings in the library: %s, %s\n", fight[0],
                fight[1]);
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
    while (a->running) {
        int ww, wh;
        SDL_GetWindowSize(a->win.window, &ww, &wh);
        float scale = ww > 0 ? (float)a->win.width / (float)ww : 1;
        uiin_begin_frame(scale);
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
                    else if (a->interface)
                        interface_context_menu(a, e.button.x * scale, e.button.y * scale, id);
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
        if (!a->paused) {
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
            fprintf(stderr, "%ld pictures in %.1f s: %.1f fps, %d segments, %d organisms, time %lld\n",
                    frames_total, a->seconds, frames_total / a->seconds, a->ninst, a->w.norgs,
                    (long long)(a->w.frame / 256));
            a->running = false;
        }
    }
    arena_free(a->arena);
    library_free(&a->lib);
    if (a->interface)
        interface_shutdown();
    gl2d_shutdown();
    lines_destroy();
    capsules_destroy();
    lighting_destroy();
    platform_window_destroy(&a->win);
    free(a->inst);
    free(a->inst_org);
    free(a->lines);
    world_free(&a->w);
    return 0;
}
