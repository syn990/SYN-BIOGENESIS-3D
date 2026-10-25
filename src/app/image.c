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

/* Save its image (0.9's): the organism alone, as the camera sees it now,
 * as flesh (render/bodies.h) on a clear ground, fitted to the picture; and
 * a code's body as the screens show it, on black. Drawn by GL off the
 * screen at a few times the size and averaged down, so edges are smooth. */

#include <cglm/cglm.h>
#include <epoxy/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_internal.h"
#include "app/flesh.h"
#include "app/looks.h"
#include "io/png.h"
#include "ui/gl2d.h"

/* inst drawn as v sees them into size x size RGBA (rows from the top);
 * the ground clear, or black when ground */
static bool snapshot(const BodyInstance *inst, int n, const SceneView *v, int size, bool ground,
                     unsigned char *px)
{
    int ss = size >= 512 ? 2 : size >= 256 ? 3 : 4, big = size * ss;
    unsigned char *raw = malloc((size_t)big * big * 4);
    if (!raw)
        return false;
    gl2d_flush();
    GLuint fbo, col, dep;
    glGenTextures(1, &col);
    glBindTexture(GL_TEXTURE_2D, col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, big, big, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenRenderbuffers(1, &dep);
    glBindRenderbuffer(GL_RENDERBUFFER, dep);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, big, big);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, dep);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, big, big);
        glClearColor(0, 0, 0, ground ? 1.0f : 0.0f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SceneView sv = *v;
        sv.px_scale = v->px_scale * (float)ss;
        bodies_draw(inst, n, &sv);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, big, big, GL_RGBA, GL_UNSIGNED_BYTE, raw);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &dep);
    glDeleteTextures(1, &col);
    gl2d_resume();
    if (ok) {
        /* each pixel the mean of its ss x ss, the colour weighted by how
         * much of it is body, the rows turned top first */
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                double sum[4] = {0};
                for (int j = 0; j < ss; j++)
                    for (int i = 0; i < ss; i++) {
                        const unsigned char *q = raw + ((size_t)(big - 1 - (y * ss + j)) * big + x * ss + i) * 4;
                        double a = q[3] / 255.0;
                        for (int k = 0; k < 3; k++)
                            sum[k] += q[k] * a;
                        sum[3] += a;
                    }
                unsigned char *o = px + ((size_t)y * size + x) * 4;
                for (int k = 0; k < 3; k++)
                    o[k] = (unsigned char)(sum[3] > 0 ? fmin(255, sum[k] / sum[3] + 0.5) : 0);
                o[3] = (unsigned char)fmin(255, sum[3] / (ss * ss) * 255 + 0.5);
            }
    }
    free(raw);
    return ok;
}

bool organism_image(const App *a, const Organism *o, int size, const char *path, char *err, int errlen)
{
    int n = o->segments;
    if (n <= 0) {
        snprintf(err, (size_t)errlen, "it has no body to draw");
        return false;
    }
    BodyInstance *inst = malloc((size_t)flesh_count(n) * sizeof *inst);
    unsigned char *px = malloc((size_t)size * size * 4);
    if (!inst || !px) {
        free(inst);
        free(px);
        snprintf(err, (size_t)errlen, "out of memory");
        return false;
    }
    /* its flesh where it is, in the look's colours, the cell paler */
    Look look = look_current();
    LookScene ls;
    look_scene(look, skin_theme(), &ls);
    M3 R = m3_from_quat(o->q);
    int main_col;
    int m = flesh_build(inst, o->ws, o->we, n, o->gc->ngenes, o->seg_color, m3_mul_v(&R, v3(0, 0, 1)), 0.3f,
                        P.segment_radius, &main_col);
    for (int k = 0; k < m; k++) {
        bool core = k == m - 1;
        float rgb[3];
        look_rgb(look, core ? main_col : o->seg_color[k], rgb);
        for (int q = 0; q < 3; q++)
            inst[k].rgb[q] = (core && o->alive ? rgb[q] * 0.7f + 0.3f : rgb[q]) * 0.85f;
        inst[k].glow = o->alive ? ls.glow : 0;
    }

    /* the camera's view of it, near enough that it fills the picture */
    double rad = o->radius + 3, fovy = 30 * D_PI / 180, dist = rad / sin(fovy / 2) * 1.04;
    V3 eye = v3_sub(o->pos, v3_scale(a->cam_fwd, dist));
    SceneView v;
    memset(&v, 0, sizeof v);
    mat4 proj, view, vp;
    vec3 e = {(float)eye.x, (float)eye.y, (float)eye.z}, at = {(float)o->pos.x, (float)o->pos.y, (float)o->pos.z};
    vec3 u = {(float)a->cam_up.x, (float)a->cam_up.y, (float)a->cam_up.z};
    glm_perspective((float)fovy, 1.0f, (float)fmax(0.1, dist - rad * 1.6), (float)(dist + rad * 1.6), proj);
    glm_lookat(e, at, u, view);
    glm_mat4_mul(proj, view, vp);
    memcpy(v.view_proj, vp, sizeof v.view_proj);
    memcpy(v.eye, e, sizeof v.eye);
    memcpy(v.light_dir, a->view.light_dir, sizeof v.light_dir);
    for (int q = 0; q < 3; q++) {
        v.fog_rgb[q] = ls.water[q] * 1.6f + 0.04f;
        v.deep_rgb[q] = ls.water[q] * 0.8f;
    }
    v.fog_near = v.fog_far = 1e9f;
    memcpy(v.box, a->view.box, sizeof v.box);
    v.time = a->view.time;
    v.px_scale = (float)(size / (2 * tan(fovy / 2)));
    v.tissue = ls.tissue;
    v.sss = ls.sss;
    v.lines = ls.lines;
    v.line_radius = (float)P.segment_radius;
    v.direct = 1;
    bool ok = snapshot(inst, m, &v, size, false, px);
    if (!ok)
        snprintf(err, (size_t)errlen, "the picture could not be drawn");
    else if (!(ok = png_write_rgba(path, size, size, px)))
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
    free(inst);
    free(px);
    return ok;
}

bool code_image(const GeneticCode *gc, int size, const char *path)
{
    SceneView v;
    const BodyInstance *inst;
    int m = screen_code_scene(gc, 1.0, size, 0, 0.55, &v, &inst);
    unsigned char *px = malloc((size_t)size * size * 4);
    bool ok = m > 0 && px && snapshot(inst, m, &v, size, true, px) && png_write_rgba(path, size, size, px);
    free(px);
    return ok;
}
