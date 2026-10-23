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

#include "raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/params.h"

struct Stroke {
    float depth;
    float ax, ay, bx, by, r;
    float c[3];
};

static void camera_look(Camera *c, V3 eye, V3 target, double fov_deg, int height)
{
    c->eye = eye;
    c->fwd = v3_unit_or(v3_sub(target, eye), v3(0, 1, 0));
    c->right = v3_unit_or(v3_cross(c->fwd, v3(0, 0, 1)), v3(1, 0, 0));
    c->up = v3_cross(c->right, c->fwd);
    c->focal = (height / 2.0) / tan(fov_deg * D_PI / 360.0);
}

/* Screen position and depth of p; false behind the camera. */
static bool project(const Camera *c, const Frame *f, V3 p, float *sx, float *sy, float *depth)
{
    V3 d = v3_sub(p, c->eye);
    double z = v3_dot(d, c->fwd);
    if (z < 1)
        return false;
    *sx = (float)(f->width / 2.0 + c->focal * v3_dot(d, c->right) / z);
    *sy = (float)(f->height / 2.0 - c->focal * v3_dot(d, c->up) / z);
    *depth = (float)z;
    return true;
}

static void clear(Frame *f)
{
    for (int y = 0; y < f->height; y++) {
        float t = (float)y / f->height;
        float c[3] = {0.035f + 0.02f * t, 0.045f + 0.025f * t, 0.06f + 0.03f * t};
        for (int x = 0; x < f->width; x++)
            memcpy(&f->rgb[3 * (y * f->width + x)], c, sizeof c);
    }
}

/* An anti-aliased capsule from a to b, radius r pixels. tube shades it
 * brighter along its middle, like a lit cylinder. */
static void stroke(Frame *f, const Stroke *s, float alpha, bool tube)
{
    float minx = fminf(s->ax, s->bx) - s->r - 1, maxx = fmaxf(s->ax, s->bx) + s->r + 1;
    float miny = fminf(s->ay, s->by) - s->r - 1, maxy = fmaxf(s->ay, s->by) + s->r + 1;
    int x0 = minx < 0 ? 0 : (int)minx, x1 = maxx >= f->width ? f->width - 1 : (int)maxx;
    int y0 = miny < 0 ? 0 : (int)miny, y1 = maxy >= f->height ? f->height - 1 : (int)maxy;
    float dx = s->bx - s->ax, dy = s->by - s->ay;
    float len2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f - s->ax, py = y + 0.5f - s->ay;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ex = px - t * dx, ey = py - t * dy;
            float d = sqrtf(ex * ex + ey * ey);
            float cov = s->r + 0.5f - d;
            if (cov <= 0)
                continue;
            cov = (cov > 1 ? 1 : cov) * alpha;
            float shade = tube ? 0.62f + 0.38f * (1 - fminf(d / s->r, 1)) : 1;
            float *p = &f->rgb[3 * (y * f->width + x)];
            for (int k = 0; k < 3; k++)
                p[k] = p[k] * (1 - cov) + s->c[k] * shade * cov;
        }
}

static int cmp_stroke(const void *a, const void *b)
{
    float da = ((const Stroke *)a)->depth, db = ((const Stroke *)b)->depth;
    return (da < db) - (da > db); /* far first */
}

/* A thin guide line of the box or floor, fogged like everything else. */
static void guide(Frame *f, const Camera *c, V3 a, V3 b, float alpha, float near, float far)
{
    Stroke s;
    float da, db;
    if (!project(c, f, a, &s.ax, &s.ay, &da) || !project(c, f, b, &s.bx, &s.by, &db))
        return;
    float fog = 1 - 0.6f * fminf(fmaxf(((da + db) / 2 - near) / (far - near), 0), 1);
    s.r = 0.7f;
    s.c[0] = 0.55f * fog;
    s.c[1] = 0.62f * fog;
    s.c[2] = 0.72f * fog;
    stroke(f, &s, alpha, false);
}

void draw_world(Frame *f, const Camera *c, const World *w, Stroke **buf, int *cap)
{
    clear(f);
    V3 corner[8];
    for (int i = 0; i < 8; i++)
        corner[i] = v3(i & 1 ? w->width : 0, i & 2 ? w->height : 0, i & 4 ? w->depth : 0);
    float near = 1e30f, far = 0;
    for (int i = 0; i < 8; i++) {
        float x, y, d;
        if (project(c, f, corner[i], &x, &y, &d)) {
            near = fminf(near, d);
            far = fmaxf(far, d);
        }
    }
    if (far <= near)
        far = near + 1;
    /* the floor's grid, then the box's edges */
    for (int k = 0; k <= 10; k++) {
        double gx = w->width * k / 10.0, gy = w->height * k / 10.0;
        guide(f, c, v3(gx, 0, 0), v3(gx, w->height, 0), 0.18f, near, far);
        guide(f, c, v3(0, gy, 0), v3(w->width, gy, 0), 0.18f, near, far);
    }
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int e = 0; e < 12; e++)
        guide(f, c, corner[edges[e][0]], corner[edges[e][1]], 0.45f, near, far);

    int n = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        for (int s = 0; s < o->segments; s++) {
            if (n == *cap) {
                *cap = *cap ? *cap * 2 : 4096;
                *buf = realloc(*buf, (size_t)*cap * sizeof **buf);
                if (!*buf)
                    abort();
            }
            Stroke *st = &(*buf)[n];
            float da, db;
            if (!project(c, f, o->ws[s], &st->ax, &st->ay, &da) ||
                !project(c, f, o->we[s], &st->bx, &st->by, &db))
                continue;
            st->depth = (da + db) / 2;
            st->r = fmaxf(1.3f, (float)(1.35 * P.segment_radius * c->focal / st->depth));
            int col = o->frames_color > 0 && o->flash_color != FLASH_NONE ? o->flash_color
                                                                         : o->seg_color[s];
            uint8_t r, g, b;
            bio_color_rgb(col, &r, &g, &b);
            float fog = 1 - 0.55f * fminf(fmaxf((st->depth - near) / (far - near), 0), 1);
            st->c[0] = r / 255.0f * fog;
            st->c[1] = g / 255.0f * fog;
            st->c[2] = b / 255.0f * fog;
            n++;
        }
    }
    qsort(*buf, (size_t)n, sizeof **buf, cmp_stroke);
    for (int i = 0; i < n; i++)
        stroke(f, &(*buf)[i], 1, true);
}


bool frame_init(Frame *f, int width, int height)
{
    f->width = width;
    f->height = height;
    f->rgb = malloc((size_t)width * height * 3 * sizeof(float));
    f->out = malloc((size_t)width * height * 3);
    return f->rgb && f->out;
}

void frame_free(Frame *f)
{
    free(f->rgb);
    free(f->out);
}

void frame_to_bytes(Frame *f)
{
    size_t n = (size_t)f->width * f->height * 3;
    for (size_t i = 0; i < n; i++) {
        float v = f->rgb[i];
        f->out[i] = (unsigned char)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f);
    }
}

void camera_orbit(Camera *c, V3 aim, double yaw_deg, double elevation_deg, double distance,
                  int height)
{
    double yaw = yaw_deg * D_PI / 180, el = elevation_deg * D_PI / 180;
    V3 eye = v3_add(aim, v3_scale(v3(cos(el) * cos(yaw), cos(el) * sin(yaw), sin(el)), distance));
    camera_look(c, eye, aim, 40, height);
}

