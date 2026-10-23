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
 * selected organism, the life, or the box's centre. Only this file and
 * src/render touch the window and GL; the World is only read.
 *
 *   drag            turn the camera          right-drag   move the aim
 *   wheel           closer / further         click        select an organism
 *   space           pause                    .            one frame (paused)
 *   up / down       faster / slower          f            follow / stop following
 *   n / r           next seed / restart      home         camera back to the box
 *   esc             drop the selection, then quit
 */

#include "app/app.h"

#include <cglm/cglm.h>
#include <epoxy/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"
#include "platform/window.h"
#include "render/capsules.h"
#include "render/lighting.h"
#include "render/lines.h"
#include "sim/params.h"
#include "sim/world.h"

#define FOV_DEG 45.0

/* world frames per second; the last is "as many as fit" */
static const int k_speeds[] = {5, 15, 30, 60, 120, 240, 480, 960, 0};
#define N_SPEEDS ((int)(sizeof k_speeds / sizeof k_speeds[0]))

typedef enum { FOLLOW_NONE, FOLLOW_LIFE, FOLLOW_SELECTED } Follow;

typedef struct {
    PlatformWindow win;
    World w;
    WorldOptions wo;
    int warm;
    /* time */
    int speed;
    bool paused;
    double acc; /* world frames owed */
    /* camera */
    double yaw, pitch, distance;
    V3 aim;
    Follow follow;
    int selected; /* organism id, -1 for none */
    /* drawing scratch */
    CapsuleInstance *inst;
    int *inst_org; /* which organism each instance is */
    int ninst, inst_cap;
    LineVertex *lines;
    int nlines, lines_cap;
    SceneView view;
    V3 cam_eye, cam_fwd, cam_right, cam_up;
} App;

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

static bool make_world(App *a)
{
    char err[512];
    if (world_options_genesis(&a->wo, &a->w, err, sizeof err) < 0) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return false;
    }
    for (long f = 0; f < (long)a->warm * 256; f++)
        world_time(&a->w);
    a->selected = -1;
    a->acc = 0;
    return true;
}

static Organism *find_org(const World *w, int id)
{
    for (int i = 0; i < w->norgs; i++)
        if (w->orgs[i]->id == id)
            return w->orgs[i];
    return NULL;
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

/* Where an organism is drawn: between the pose it began the world's last
 * frame in and the pose it ended it in. */
static void org_draw_pose(const Organism *o, double alpha, V3 *pos, Quat *q)
{
    *pos = v3_add(o->prev_pos, v3_scale(v3_sub(o->pos, o->prev_pos), alpha));
    *q = quat_nlerp(o->prev_q, o->q, alpha);
}

static void build_instances(App *a, double alpha, double t_seconds)
{
    const World *w = &a->w;
    a->ninst = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        V3 pos;
        Quat q;
        org_draw_pose(o, alpha, &pos, &q);
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
                ci->glow += 0.25f + 0.15f * (float)sin(t_seconds * 5.0);
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

static void build_lines(App *a, double alpha)
{
    const World *w = &a->w;
    a->nlines = 0;
    static const float grid[4] = {0.30f, 0.42f, 0.58f, 0.22f};
    static const float edge[4] = {0.55f, 0.68f, 0.85f, 0.55f};
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
    /* the selected organism: a drop line to the floor and a ring of ticks */
    Organism *s = a->selected >= 0 ? find_org(w, a->selected) : NULL;
    if (s) {
        static const float mark[4] = {1.0f, 0.85f, 0.35f, 0.75f};
        V3 pos;
        Quat q;
        org_draw_pose(s, alpha, &pos, &q);
        line(a, pos, v3(pos.x, pos.y, 0), mark);
        double r = s->radius + 4;
        for (int k = 0; k < 4; k++) {
            double th = k * D_PI / 2;
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
    glm_perspective(glm_rad((float)FOV_DEG), (float)pw->width / (float)(pw->height > 0 ? pw->height : 1),
                    near, far, proj);
    glm_lookat(e, c, up, view);
    mat4 vp;
    glm_mat4_mul(proj, view, vp);
    memcpy(a->view.view_proj, vp, sizeof a->view.view_proj);
    a->view.eye[0] = e[0];
    a->view.eye[1] = e[1];
    a->view.eye[2] = e[2];
    V3 l = v3_unit_or(v3(0.35, 0.25, 1.0), v3(0, 0, 1));
    a->view.light_dir[0] = (float)l.x;
    a->view.light_dir[1] = (float)l.y;
    a->view.light_dir[2] = (float)l.z;
    a->view.fog_rgb[0] = 0.02f;
    a->view.fog_rgb[1] = 0.026f;
    a->view.fog_rgb[2] = 0.038f;
    a->view.fog_near = (float)(a->distance * 0.6);
    a->view.fog_far = (float)(a->distance + diag);
}

static void draw(App *a, double alpha, double t_seconds)
{
    PlatformWindow *pw = &a->win;
    set_view(a);
    build_instances(a, alpha, t_seconds);
    build_lines(a, alpha);
    lighting_scene_begin(pw->width, pw->height, a->view.fog_rgb[0], a->view.fog_rgb[1],
                         a->view.fog_rgb[2]);
    capsules_draw(a->inst, a->ninst, &a->view);
    lines_draw(a->lines, a->nlines, &a->view);
    lighting_scene_end();
}

/* ---- picking ---- */

/* The organism under window point (mx, my): the nearest capsule the ray
 * through it passes within reach of. */
static int pick(App *a, int mx, int my)
{
    PlatformWindow *pw = &a->win;
    int ww, wh;
    SDL_GetWindowSize(pw->window, &ww, &wh);
    double nx = (2.0 * mx / ww - 1), ny = (1 - 2.0 * my / wh);
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
        if (e < 1e-12) {
            s = 0;
            t = -cc;
        } else if (denom < 1e-12) {
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

/* ---- the title bar (until the SYN-OS interface) ---- */

static void update_title(App *a, double fps)
{
    char title[512];
    const World *w = &a->w;
    char speed[16];
    if (k_speeds[a->speed])
        snprintf(speed, sizeof speed, "%d/s", k_speeds[a->speed]);
    else
        snprintf(speed, sizeof speed, "max");
    int n = snprintf(title, sizeof title,
                     "SYN-BIOGENESIS-3D  |  seed %lld  |  %d x %d x %d  |  time %lld  |  "
                     "alive %d  dead %d  |  kills %lld  |  %s%s  |  %.0f fps",
                     (long long)a->wo.seed, w->width, w->height, w->depth,
                     (long long)(w->frame / 256), w->population, world_n_corpses(w),
                     (long long)w->counts.kills, speed, a->paused ? "  PAUSED" : "", fps);
    Organism *s = a->selected >= 0 ? find_org(w, a->selected) : NULL;
    if (s && n > 0 && n < (int)sizeof title)
        snprintf(title + n, sizeof title - (size_t)n,
                 "  |  #%d %s, generation %d, energy %.1f, age %d/%d, %d children, %d kills%s",
                 s->id, s->alive ? "alive" : "dead", s->generation, s->energy, s->age >> 8,
                 s->gc->max_age + 1, s->total_children, s->total_kills,
                 a->follow == FOLLOW_SELECTED ? ", followed" : "");
    SDL_SetWindowTitle(a->win.window, title);
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
          "  --seconds S       quit after S seconds and print the frame rate\n"
          "  --window W H      window size (default 1440 900)\n"
          "\n"
          "  drag: turn   right-drag: move   wheel: zoom   click: select\n"
          "  space: pause   .: one frame   up/down: speed   f: follow\n"
          "  n: next seed   r: restart   home: whole box   esc: drop selection, quit\n",
          f);
}

int app_main(int argc, char **argv)
{
    App a;
    memset(&a, 0, sizeof a);
    world_options_init(&a.wo);
    const char *shot = NULL;
    int win_w = 1440, win_h = 900;
    double cam[3] = {0, 0, 0}; /* yaw, pitch, distance from --camera; 0 distance keeps home */
    double run_for = 0;        /* --seconds: quit after this long and report the frame rate */
    for (int i = 1; i < argc; i++) {
        if (world_option(&a.wo, argc, argv, &i))
            continue;
        const char *s = argv[i];
        if (strcmp(s, "--warm") == 0 && i + 1 < argc)
            a.warm = atoi(argv[++i]);
        else if (strcmp(s, "--shot") == 0 && i + 1 < argc)
            shot = argv[++i];
        else if (strcmp(s, "--seconds") == 0 && i + 1 < argc)
            run_for = atof(argv[++i]);
        else if (strcmp(s, "--camera") == 0 && i + 3 < argc) {
            cam[0] = atof(argv[++i]);
            cam[1] = atof(argv[++i]);
            cam[2] = atof(argv[++i]);
        }
        else if (strcmp(s, "--window") == 0 && i + 2 < argc) {
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
    if (!make_world(&a))
        return 1;
    if (!platform_window_create(&a.win, "SYN-BIOGENESIS-3D", win_w, win_h))
        return 1;
    lighting_init();
    capsules_init();
    lines_init();
    camera_home(&a);
    if (cam[2] > 0) {
        a.yaw = cam[0];
        a.pitch = cam[1];
        a.distance = cam[2];
        a.follow = FOLLOW_LIFE;
        a.aim = world_life_centre_or_box(&a.w);
    }
    a.speed = 3; /* 60 world frames a second */

    if (shot) {
        draw(&a, 1.0, 0);
        bool ok = platform_window_shot(&a.win, shot);
        fprintf(stderr, ok ? "%s\n" : "could not write %s\n", shot);
        return ok ? 0 : 1;
    }

    bool running = true, dragging = false, panning = false, moved = false;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    Uint64 start = last;
    Uint32 last_title = 0;
    int frames_since = 0;
    long frames_total = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                dragging = e.button.button == SDL_BUTTON_LEFT;
                panning = e.button.button == SDL_BUTTON_RIGHT;
                moved = false;
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT && !moved) {
                    a.selected = pick(&a, e.button.x, e.button.y);
                    if (a.selected >= 0)
                        a.follow = FOLLOW_SELECTED;
                    else if (a.follow == FOLLOW_SELECTED)
                        a.follow = FOLLOW_NONE;
                }
                dragging = panning = false;
                break;
            case SDL_MOUSEMOTION:
                if (dragging || panning) {
                    if (abs(e.motion.xrel) + abs(e.motion.yrel) > 1)
                        moved = true;
                }
                if (dragging) {
                    a.yaw -= e.motion.xrel * 0.3;
                    a.pitch += e.motion.yrel * 0.3;
                    a.pitch = a.pitch < -85 ? -85 : a.pitch > 85 ? 85 : a.pitch;
                } else if (panning) {
                    double k = a.distance * 0.0015;
                    a.aim = v3_add(a.aim, v3_add(v3_scale(a.cam_right, -e.motion.xrel * k),
                                                 v3_scale(a.cam_up, e.motion.yrel * k)));
                    a.follow = FOLLOW_NONE;
                }
                break;
            case SDL_MOUSEWHEEL:
                a.distance *= e.wheel.y > 0 ? 1 / 1.12 : 1.12;
                a.distance = a.distance < 10 ? 10 : a.distance;
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_q:
                    running = false;
                    break;
                case SDLK_ESCAPE:
                    if (a.selected >= 0) {
                        a.selected = -1;
                        if (a.follow == FOLLOW_SELECTED)
                            a.follow = FOLLOW_NONE;
                    } else {
                        running = false;
                    }
                    break;
                case SDLK_SPACE:
                    a.paused = !a.paused;
                    break;
                case SDLK_PERIOD:
                    if (a.paused)
                        world_time(&a.w);
                    break;
                case SDLK_UP:
                    a.speed = a.speed + 1 < N_SPEEDS ? a.speed + 1 : a.speed;
                    break;
                case SDLK_DOWN:
                    a.speed = a.speed > 0 ? a.speed - 1 : 0;
                    break;
                case SDLK_f:
                    if (a.follow != FOLLOW_NONE)
                        a.follow = FOLLOW_NONE;
                    else
                        a.follow = a.selected >= 0 ? FOLLOW_SELECTED : FOLLOW_LIFE;
                    break;
                case SDLK_n:
                    a.wo.seed++;
                    make_world(&a);
                    break;
                case SDLK_r:
                    make_world(&a);
                    break;
                case SDLK_HOME:
                    camera_home(&a);
                    break;
                }
                break;
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        if (dt > 0.25)
            dt = 0.25;
        double alpha = 1;
        if (!a.paused) {
            int rate = k_speeds[a.speed];
            Uint64 budget = now + freq / 80; /* stop stepping after ~12 ms */
            if (rate) {
                a.acc += dt * rate;
                while (a.acc >= 1 && SDL_GetPerformanceCounter() < budget) {
                    world_time(&a.w);
                    a.acc -= 1;
                }
                if (a.acc > 4)
                    a.acc = 1; /* behind: drop the debt rather than spiral */
                alpha = a.acc < 1 ? a.acc : 1;
            } else {
                while (SDL_GetPerformanceCounter() < budget)
                    world_time(&a.w);
            }
        }

        /* the aim drifts after what it follows */
        Organism *sel = a.selected >= 0 ? find_org(&a.w, a.selected) : NULL;
        if (a.selected >= 0 && !sel) {
            a.selected = -1; /* gone: decayed away */
            if (a.follow == FOLLOW_SELECTED)
                a.follow = FOLLOW_NONE;
        }
        V3 target = a.aim;
        if (a.follow == FOLLOW_SELECTED && sel) {
            Quat q;
            org_draw_pose(sel, alpha, &target, &q);
        } else if (a.follow == FOLLOW_LIFE) {
            target = world_life_centre_or_box(&a.w);
        }
        double k = 1 - exp(-dt * (a.follow == FOLLOW_SELECTED ? 8.0 : 2.0));
        a.aim = v3_add(a.aim, v3_scale(v3_sub(target, a.aim), k));

        platform_window_refresh_size(&a.win);
        draw(&a, alpha, (double)(now - start) / freq);
        platform_window_swap(&a.win);

        frames_since++;
        frames_total++;
        double elapsed = (double)(SDL_GetPerformanceCounter() - start) / freq;
        if (run_for > 0 && elapsed >= run_for) {
            fprintf(stderr, "%ld pictures in %.1f s: %.1f fps, %d segments, %d organisms, time %lld\n",
                    frames_total, elapsed, frames_total / elapsed, a.ninst, a.w.norgs,
                    (long long)(a.w.frame / 256));
            running = false;
        }
        Uint32 ms = SDL_GetTicks();
        if (ms - last_title > 500) {
            update_title(&a, frames_since * 1000.0 / (ms - last_title));
            last_title = ms;
            frames_since = 0;
        }
    }
    lines_destroy();
    capsules_destroy();
    lighting_destroy();
    platform_window_destroy(&a.win);
    free(a.inst);
    free(a.inst_org);
    free(a.lines);
    world_free(&a.w);
    return 0;
}
