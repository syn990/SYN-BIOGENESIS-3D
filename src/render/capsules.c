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

#include "render/capsules.h"

#include <epoxy/gl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "render/lighting.h"
#include "render/shader.h"

/* The mesh: rings around the axis, a hemisphere's worth at the start
 * (t = 0, bulging back) and at the end (t = 1, bulging forward). Each
 * vertex is its t and its offset on the unit sphere, which is also its
 * normal; the instance's radius scales it. */
#define AROUND 12
#define CAP_RINGS 3

static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in float aT;\n"
    "layout(location = 1) in vec3 aOff;\n"
    "layout(location = 2) in vec3 iA;\n"
    "layout(location = 3) in vec3 iB;\n"
    "layout(location = 4) in vec3 iRgb;\n"
    "layout(location = 5) in float iRadius;\n"
    "layout(location = 6) in float iGlow;\n"
    "uniform mat4 uViewProj;\n"
    "out vec3 vNormal;\n"
    "out vec3 vWorld;\n"
    "out vec3 vRgb;\n"
    "out float vGlow;\n"
    "void main() {\n"
    "    vec3 axis = iB - iA;\n"
    "    float len = length(axis);\n"
    "    vec3 w = len > 1e-6 ? axis / len : vec3(0.0, 0.0, 1.0);\n"
    "    vec3 h = abs(w.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
    "    vec3 u = normalize(cross(h, w));\n"
    "    vec3 v = cross(w, u);\n"
    "    vec3 n = u * aOff.x + v * aOff.y + w * aOff.z;\n"
    "    vec3 p = iA + axis * aT + n * iRadius;\n"
    "    vNormal = n;\n"
    "    vWorld = p;\n"
    "    vRgb = iRgb;\n"
    "    vGlow = iGlow;\n"
    "    gl_Position = uViewProj * vec4(p, 1.0);\n"
    "}\n";

/* Lambert from one key light, a fresnel rim so a body's outline reads
 * against the dark, the glow added on top, and fog toward the background
 * with distance so depth reads too. */
static const char *FS =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec3 vWorld;\n"
    "in vec3 vRgb;\n"
    "in float vGlow;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"
    "uniform float uAmbient;\n"
    "uniform vec3 uFog;\n"
    "uniform float uFogNear;\n"
    "uniform float uFogFar;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(vNormal);\n"
    "    vec3 toEye = normalize(uEye - vWorld);\n"
    "    float diff = max(dot(n, uLightDir), 0.0);\n"
    "    float back = max(dot(n, -uLightDir), 0.0) * 0.18;\n"
    "    float rim = pow(1.0 - max(dot(n, toEye), 0.0), 3.0);\n"
    "    vec3 c = vRgb * (uAmbient + 0.9 * diff + back) + vRgb * rim * 0.45;\n"
    "    c += vRgb * vGlow * 2.2;\n"
    "    float d = length(uEye - vWorld);\n"
    "    float fog = clamp((d - uFogNear) / max(uFogFar - uFogNear, 1.0), 0.0, 1.0);\n"
    "    FragColor = vec4(mix(c, uFog, fog * 0.75), 1.0);\n"
    "}\n";

static GLuint g_prog, g_vao, g_mesh_vbo, g_mesh_ibo, g_inst_vbo;
static GLsizei g_index_count;
static GLint u_view_proj, u_eye, u_light, u_ambient, u_fog, u_fog_near, u_fog_far;
static size_t g_inst_cap;

void capsules_init(void)
{
    g_prog = shader_program(VS, FS);
    u_view_proj = glGetUniformLocation(g_prog, "uViewProj");
    u_eye = glGetUniformLocation(g_prog, "uEye");
    u_light = glGetUniformLocation(g_prog, "uLightDir");
    u_ambient = glGetUniformLocation(g_prog, "uAmbient");
    u_fog = glGetUniformLocation(g_prog, "uFog");
    u_fog_near = glGetUniformLocation(g_prog, "uFogNear");
    u_fog_far = glGetUniformLocation(g_prog, "uFogFar");

    enum { RINGS = 2 * (CAP_RINGS + 1) };
    float verts[RINGS * AROUND * 4];
    int nv = 0;
    for (int r = 0; r < RINGS; r++) {
        bool end = r > CAP_RINGS;
        int k = end ? r - (CAP_RINGS + 1) : r;
        double phi = end ? (M_PI / 2) * k / CAP_RINGS : -(M_PI / 2) + (M_PI / 2) * k / CAP_RINGS;
        for (int j = 0; j < AROUND; j++) {
            double th = 2 * M_PI * j / AROUND;
            verts[nv++] = end ? 1.0f : 0.0f;
            verts[nv++] = (float)(cos(phi) * cos(th));
            verts[nv++] = (float)(cos(phi) * sin(th));
            verts[nv++] = (float)sin(phi);
        }
    }
    unsigned short idx[(RINGS - 1) * AROUND * 6];
    int ni = 0;
    for (int r = 0; r + 1 < RINGS; r++)
        for (int j = 0; j < AROUND; j++) {
            unsigned short a = (unsigned short)(r * AROUND + j);
            unsigned short b = (unsigned short)(r * AROUND + (j + 1) % AROUND);
            unsigned short c = (unsigned short)((r + 1) * AROUND + j);
            unsigned short d = (unsigned short)((r + 1) * AROUND + (j + 1) % AROUND);
            /* counter-clockwise seen from outside */
            idx[ni++] = a;
            idx[ni++] = b;
            idx[ni++] = c;
            idx[ni++] = b;
            idx[ni++] = d;
            idx[ni++] = c;
        }
    g_index_count = ni;

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_mesh_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(sizeof(float)));
    glGenBuffers(1, &g_mesh_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);

    glGenBuffers(1, &g_inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    const GLsizei stride = sizeof(CapsuleInstance);
    const struct {
        GLuint loc;
        GLint n;
        size_t off;
    } attrs[] = {
        {2, 3, offsetof(CapsuleInstance, a)},      {3, 3, offsetof(CapsuleInstance, b)},
        {4, 3, offsetof(CapsuleInstance, rgb)},    {5, 1, offsetof(CapsuleInstance, radius)},
        {6, 1, offsetof(CapsuleInstance, glow)},
    };
    for (size_t i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
        glEnableVertexAttribArray(attrs[i].loc);
        glVertexAttribPointer(attrs[i].loc, attrs[i].n, GL_FLOAT, GL_FALSE, stride,
                              (void *)attrs[i].off);
        glVertexAttribDivisor(attrs[i].loc, 1);
    }
    glBindVertexArray(0);
}

void capsules_destroy(void)
{
    glDeleteBuffers(1, &g_inst_vbo);
    glDeleteBuffers(1, &g_mesh_ibo);
    glDeleteBuffers(1, &g_mesh_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteProgram(g_prog);
}

void capsules_draw(const CapsuleInstance *inst, int n, const SceneView *view)
{
    if (n <= 0 || !g_prog)
        return;
    size_t bytes = (size_t)n * sizeof *inst;
    glBindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    if (bytes > g_inst_cap) {
        g_inst_cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_inst_cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, inst);

    glUseProgram(g_prog);
    glUniformMatrix4fv(u_view_proj, 1, GL_FALSE, view->view_proj);
    glUniform3fv(u_eye, 1, view->eye);
    glUniform3fv(u_light, 1, view->light_dir);
    glUniform1f(u_ambient, g_graphics.ambient);
    glUniform3fv(u_fog, 1, view->fog_rgb);
    glUniform1f(u_fog_near, view->fog_near);
    glUniform1f(u_fog_far, view->fog_far);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(g_vao);
    glDrawElementsInstanced(GL_TRIANGLES, g_index_count, GL_UNSIGNED_SHORT, NULL, n);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}
