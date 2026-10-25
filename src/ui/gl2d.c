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

#include "ui/gl2d.h"

#include <math.h>

#include <epoxy/gl.h>
#include <stdlib.h>

#include "render/shader.h"

typedef struct {
    float x, y, u, v;
    unsigned char r, g, b, a;
} Vert;

static const char *VS = "#version 330 core\n"
                        "layout(location = 0) in vec2 aPos;\n"
                        "layout(location = 1) in vec2 aUV;\n"
                        "layout(location = 2) in vec4 aRgba;\n"
                        "uniform vec2 uScreen;\n"
                        "out vec2 vUV;\n"
                        "out vec4 vRgba;\n"
                        "void main() {\n"
                        "    vUV = aUV;\n"
                        "    vRgba = aRgba;\n"
                        "    vec2 p = aPos / uScreen * 2.0 - 1.0;\n"
                        "    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
                        "}\n";

static const char *FS = "#version 330 core\n"
                        "in vec2 vUV;\n"
                        "in vec4 vRgba;\n"
                        "uniform sampler2D uTex;\n"
                        "out vec4 FragColor;\n"
                        "void main() {\n"
                        "    FragColor = texture(uTex, vUV) * vRgba;\n"
                        "}\n";

static GLuint g_prog, g_vao, g_vbo, g_white;
static GLint u_screen, u_tex;
static Vert *g_v;
static int g_n, g_cap;
static GLuint g_bound; /* the texture the batch is drawn with */
static size_t g_vbo_cap;
static float g_w, g_h;
/* the clipping rectangles, innermost last: what is drawn is cut to all */
#define CLIPS 16
static struct {
    float x0, y0, x1, y1;
} g_clip[CLIPS];
static int g_nclip;
/* What has been drawn this frame, kept until it is sent: every vertex in
 * one array, uploaded once, and the runs of it to draw, each with its
 * texture and its clip as they were. (Sending each run as it ended made
 * the driver copy the buffer every time, which cost more than the
 * drawing.) */
typedef struct {
    int first, count;
    GLuint tex;
    bool clipped;
    float x0, y0, x1, y1;
} Run;
static Run *g_run;
static int g_nrun, g_run_cap;
static int g_start; /* the first vertex not yet in a run */

bool gl2d_init(void)
{
    g_prog = shader_program(VS, FS);
    if (!g_prog)
        return false;
    u_screen = glGetUniformLocation(g_prog, "uScreen");
    u_tex = glGetUniformLocation(g_prog, "uTex");
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vert),
                          (void *)(4 * sizeof(float)));
    glBindVertexArray(0);
    unsigned char white[4] = {255, 255, 255, 255};
    glGenTextures(1, &g_white);
    glBindTexture(GL_TEXTURE_2D, g_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void gl2d_shutdown(void)
{
    free(g_v);
    free(g_run);
    g_v = NULL;
    g_run = NULL;
    g_n = g_cap = g_start = g_nrun = g_run_cap = 0;
    glDeleteTextures(1, &g_white);
    glDeleteBuffers(1, &g_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteProgram(g_prog);
}

static void clip_apply(void)
{
    if (!g_nclip) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    int k = (g_nclip < CLIPS ? g_nclip : CLIPS) - 1;
    float x0 = g_clip[k].x0, y0 = g_clip[k].y0, x1 = g_clip[k].x1, y1 = g_clip[k].y1;
    int ix = (int)floorf(x0), iy = (int)floorf(y0);
    int iw = (int)ceilf(x1) - ix, ih = (int)ceilf(y1) - iy;
    glEnable(GL_SCISSOR_TEST);
    /* GL counts rows from the bottom */
    glScissor(ix, (int)g_h - iy - (ih > 0 ? ih : 0), iw > 0 ? iw : 0, ih > 0 ? ih : 0);
}

/* the vertices since the last run, as a run */
static void flush(void)
{
    if (g_n <= g_start)
        return;
    if (g_nrun == g_run_cap) {
        g_run_cap = g_run_cap ? g_run_cap * 2 : 128;
        g_run = realloc(g_run, (size_t)g_run_cap * sizeof *g_run);
        if (!g_run)
            abort();
    }
    Run *r = &g_run[g_nrun++];
    r->first = g_start;
    r->count = g_n - g_start;
    r->tex = g_bound ? g_bound : g_white;
    r->clipped = g_nclip > 0;
    if (r->clipped) {
        int k = (g_nclip < CLIPS ? g_nclip : CLIPS) - 1;
        r->x0 = g_clip[k].x0;
        r->y0 = g_clip[k].y0;
        r->x1 = g_clip[k].x1;
        r->y1 = g_clip[k].y1;
    }
    g_start = g_n;
}

static void scissor(bool on, float x0, float y0, float x1, float y1)
{
    if (!on) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    int ix = (int)floorf(x0), iy = (int)floorf(y0);
    int iw = (int)ceilf(x1) - ix, ih = (int)ceilf(y1) - iy;
    glEnable(GL_SCISSOR_TEST);
    /* GL counts rows from the bottom */
    glScissor(ix, (int)g_h - iy - (ih > 0 ? ih : 0), iw > 0 ? iw : 0, ih > 0 ? ih : 0);
}

/* everything kept, sent: one upload, then each run */
static void submit(void)
{
    flush();
    if (g_nrun) {
        size_t bytes = (size_t)g_n * sizeof *g_v;
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        if (bytes > g_vbo_cap)
            g_vbo_cap = bytes * 2;
        /* a fresh store each time, so nothing waits on the last one */
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_vbo_cap, NULL, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, g_v);
        glUseProgram(g_prog);
        glUniform2f(u_screen, g_w, g_h);
        glUniform1i(u_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(g_vao);
        for (int i = 0; i < g_nrun; i++) {
            const Run *r = &g_run[i];
            scissor(r->clipped, r->x0, r->y0, r->x1, r->y1);
            glBindTexture(GL_TEXTURE_2D, r->tex);
            glDrawArrays(GL_TRIANGLES, r->first, r->count);
        }
        glBindVertexArray(0);
    }
    g_nrun = 0;
    g_n = g_start = 0;
    clip_apply();
}

void gl2d_clip_push(float x, float y, float w, float h)
{
    flush();
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (g_nclip) {
        x0 = fmaxf(x0, g_clip[g_nclip - 1].x0);
        y0 = fmaxf(y0, g_clip[g_nclip - 1].y0);
        x1 = fminf(x1, g_clip[g_nclip - 1].x1);
        y1 = fminf(y1, g_clip[g_nclip - 1].y1);
    }
    if (g_nclip < CLIPS) {
        g_clip[g_nclip].x0 = x0;
        g_clip[g_nclip].y0 = y0;
        g_clip[g_nclip].x1 = fmaxf(x0, x1);
        g_clip[g_nclip].y1 = fmaxf(y0, y1);
    }
    g_nclip++;
    clip_apply();
}

void gl2d_clip_pop(void)
{
    flush();
    if (g_nclip > 0)
        g_nclip--;
    clip_apply();
}

void gl2d_flush(void)
{
    submit();
}

void gl2d_resume(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei)g_w, (GLsizei)g_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(0);
    clip_apply();
}

bool gl2d_clip_current(float *x0, float *y0, float *x1, float *y1)
{
    if (!g_nclip)
        return false;
    int i = (g_nclip < CLIPS ? g_nclip : CLIPS) - 1;
    *x0 = g_clip[i].x0;
    *y0 = g_clip[i].y0;
    *x1 = g_clip[i].x1;
    *y1 = g_clip[i].y1;
    return true;
}

float gl2d_height(void)
{
    return g_h;
}

void gl2d_begin(int w, int h)
{
    g_w = (float)w;
    g_h = (float)h;
    g_n = g_start = 0;
    g_nrun = 0;
    g_bound = 0;
    g_nclip = 0;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gl2d_end(void)
{
    submit();
    g_nclip = 0;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}

/* Room for n more vertices drawn with texture tex. */
static Vert *reserve(GLuint tex, int n)
{
    if (tex != g_bound) {
        flush();
        g_bound = tex;
    }
    if (g_n + n > g_cap) {
        g_cap = (g_n + n) * 2 + 1024;
        g_v = realloc(g_v, (size_t)g_cap * sizeof *g_v);
        if (!g_v)
            abort();
    }
    Vert *v = &g_v[g_n];
    g_n += n;
    return v;
}

static void quad(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1,
                 float v1, SDL_Color c)
{
    Vert *v = reserve(tex, 6);
    Vert a = {x, y, u0, v0, c.r, c.g, c.b, c.a}, b = {x + w, y, u1, v0, c.r, c.g, c.b, c.a};
    Vert d = {x, y + h, u0, v1, c.r, c.g, c.b, c.a}, e = {x + w, y + h, u1, v1, c.r, c.g, c.b, c.a};
    v[0] = a;
    v[1] = b;
    v[2] = e;
    v[3] = a;
    v[4] = e;
    v[5] = d;
}

void gl2d_fill(float x, float y, float w, float h, SDL_Color c)
{
    if (w <= 0 || h <= 0 || !c.a)
        return;
    quad(0, x, y, w, h, 0, 0, 1, 1, c);
}

void gl2d_geometry(const SDL_Vertex *sv, int nv, const int *idx, int ni)
{
    Vert *v = reserve(0, ni);
    for (int i = 0; i < ni; i++) {
        int k = idx[i];
        if (k < 0 || k >= nv)
            k = 0;
        const SDL_Vertex *s = &sv[k];
        v[i] = (Vert){s->position.x, s->position.y, 0, 0, s->color.r, s->color.g, s->color.b,
                      s->color.a};
    }
}

bool gl2d_tex_from_surface(SDL_Surface *s, Gl2dTex *out)
{
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0);
    if (!rgba)
        return false;
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    out->id = id;
    out->w = rgba->w;
    out->h = rgba->h;
    SDL_FreeSurface(rgba);
    return true;
}

void gl2d_tex_destroy(Gl2dTex *t)
{
    if (t->id) {
        /* what is kept may draw with it */
        if (g_n > 0)
            submit();
        GLuint id = t->id;
        glDeleteTextures(1, &id);
    }
    t->id = 0;
}

void gl2d_tex_draw(const Gl2dTex *t, float x, float y, float w, float h, SDL_Color tint)
{
    if (!t->id || !tint.a)
        return;
    quad(t->id, x, y, w, h, 0, 0, 1, 1, tint);
}
