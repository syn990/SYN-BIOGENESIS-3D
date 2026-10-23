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

#include "render/lines.h"

#include <epoxy/gl.h>
#include <stddef.h>

#include "render/shader.h"

static const char *VS = "#version 330 core\n"
                        "layout(location = 0) in vec3 aPos;\n"
                        "layout(location = 1) in vec4 aRgba;\n"
                        "uniform mat4 uViewProj;\n"
                        "out vec4 vRgba;\n"
                        "out vec3 vWorld;\n"
                        "void main() {\n"
                        "    vRgba = aRgba;\n"
                        "    vWorld = aPos;\n"
                        "    gl_Position = uViewProj * vec4(aPos, 1.0);\n"
                        "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec4 vRgba;\n"
    "in vec3 vWorld;\n"
    "uniform vec3 uEye;\n"
    "uniform float uFogNear;\n"
    "uniform float uFogFar;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    float d = length(uEye - vWorld);\n"
    "    float fog = clamp((d - uFogNear) / max(uFogFar - uFogNear, 1.0), 0.0, 1.0);\n"
    "    FragColor = vec4(vRgba.rgb, vRgba.a * (1.0 - 0.7 * fog));\n"
    "}\n";

static GLuint g_prog, g_vao, g_vbo;
static GLint u_view_proj, u_eye, u_fog_near, u_fog_far;
static size_t g_cap;

void lines_init(void)
{
    g_prog = shader_program(VS, FS);
    u_view_proj = glGetUniformLocation(g_prog, "uViewProj");
    u_eye = glGetUniformLocation(g_prog, "uEye");
    u_fog_near = glGetUniformLocation(g_prog, "uFogNear");
    u_fog_far = glGetUniformLocation(g_prog, "uFogFar");
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          (void *)offsetof(LineVertex, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          (void *)offsetof(LineVertex, rgba));
    glBindVertexArray(0);
}

void lines_destroy(void)
{
    glDeleteBuffers(1, &g_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteProgram(g_prog);
}

void lines_draw(const LineVertex *v, int n, const SceneView *view)
{
    if (n <= 0 || !g_prog)
        return;
    size_t bytes = (size_t)n * sizeof *v;
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (bytes > g_cap) {
        g_cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, v);
    glUseProgram(g_prog);
    glUniformMatrix4fv(u_view_proj, 1, GL_FALSE, view->view_proj);
    glUniform3fv(u_eye, 1, view->eye);
    glUniform1f(u_fog_near, view->fog_near);
    glUniform1f(u_fog_far, view->fog_far);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_LINES, 0, n);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}
