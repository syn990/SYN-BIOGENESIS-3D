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
    g_v = NULL;
    g_n = g_cap = 0;
    glDeleteTextures(1, &g_white);
    glDeleteBuffers(1, &g_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteProgram(g_prog);
}

static void flush(void)
{
    if (!g_n)
        return;
    size_t bytes = (size_t)g_n * sizeof *g_v;
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (bytes > g_vbo_cap) {
        g_vbo_cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_vbo_cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, g_v);
    glUseProgram(g_prog);
    glUniform2f(u_screen, g_w, g_h);
    glUniform1i(u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_bound ? g_bound : g_white);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, g_n);
    glBindVertexArray(0);
    g_n = 0;
}

void gl2d_begin(int w, int h)
{
    g_w = (float)w;
    g_h = (float)h;
    g_n = 0;
    g_bound = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gl2d_end(void)
{
    flush();
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
        if (t->id == g_bound)
            flush();
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
