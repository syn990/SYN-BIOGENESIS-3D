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

#include "render/water.h"

#include <epoxy/gl.h>
#include <stddef.h>
#include <string.h>

#include "render/glsl.h"
#include "render/lighting.h"
#include "render/shader.h"

/* ---- shaders ---- */

static const char *FULL_VS = "#version 330 core\n"
                             "void main() {\n"
                             "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                             "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                             "}\n";

/* darker below, the surface's light above, brightest toward the sun */
static const char *BACK_FS = "#version 330 core\n" GLSL_COMMON
                             "uniform mat4 uInvViewProj;\n"
                             "uniform vec2 uInvSize;\n"
                             "uniform vec3 uLightDir;\n"
                             "out vec4 FragColor;\n"
                             "void main() {\n"
                             "    vec2 ndc = gl_FragCoord.xy * uInvSize * 2.0 - 1.0;\n"
                             "    vec4 w = uInvViewProj * vec4(ndc, 1.0, 1.0);\n"
                             "    vec3 dir = normalize(w.xyz / w.w - uEye);\n"
                             "    vec3 top = uFog * 1.3 + vec3(0.01, 0.02, 0.02);\n"
                             "    vec3 c = mix(uDeep * 0.6, top, smoothstep(-0.7, 0.9, dir.z));\n"
                             "    c += uFog * 0.8 * pow(max(dot(dir, uLightDir), 0.0), 10.0);\n"
                             "    FragColor = vec4(finish(c), 1.0);\n"
                             "}\n";

/* the box's floor or its surface: two triangles at height uZ */
static const char *PLANE_VS = "#version 330 core\n"
                              "uniform mat4 uViewProj;\n"
                              "uniform vec3 uBox;\n"
                              "uniform float uZ;\n"
                              "out vec3 vWorld;\n"
                              "const vec2 k[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),\n"
                              "                         vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));\n"
                              "void main() {\n"
                              "    vWorld = vec3(k[gl_VertexID] * uBox.xy, uZ);\n"
                              "    gl_Position = uViewProj * vec4(vWorld, 1.0);\n"
                              "}\n";

/* the sediment's tile under the light that reaches the floor, the
 * caustics' web moving on it, a broad tint so the tile does not show */
static const char *FLOOR_FS = "#version 330 core\n"
                              "in vec3 vWorld;\n" GLSL_COMMON
                              "uniform sampler2D uSed;\n"
                              "uniform sampler2D uCaust;\n"
                              "uniform float uSedTile;\n"
                              "uniform float uCaustTile;\n"
                              "uniform float uLight;\n"
                              "uniform float uCaustics;\n"
                              "uniform float uAmbient;\n"
                              "out vec4 FragColor;\n"
                              "void main() {\n"
                              "    vec2 p = vWorld.xy;\n"
                              "    vec3 sed = texture(uSed, p / uSedTile).rgb * (0.88 + 0.24 * vnoise(p * 0.004));\n"
                              "    float ca = texture(uCaust, p / uCaustTile).r * uCaustics;\n"
                              "    vec3 col = sed * (uAmbient + uLight) + sed * water_at(0.0) * 0.8\n"
                              "               + mix(uFog * 3.0, vec3(0.9, 1.0, 0.95), 0.4) * ca * uLight * 0.45;\n"
                              "    FragColor = vec4(finish(through_water(col, vWorld)), 1.0);\n"
                              "}\n";

/* What repeats, drawn into tiles: noise and cells that wrap at the tile's
 * edge, the caustics' web (each frame) and the sediment (once). */
#define TILE_GLSL                                                                                  \
    "float hp(vec2 p, vec2 per) {\n"                                                               \
    "    p = mod(p, per);\n"                                                                       \
    "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);\n"                         \
    "}\n"                                                                                          \
    "vec2 hp2(vec2 p, vec2 per) {\n"                                                               \
    "    p = mod(p, per);\n"                                                                       \
    "    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);\n" \
    "}\n"                                                                                          \
    "float pnoise(vec2 p, vec2 per) {\n"                                                           \
    "    vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);\n"                          \
    "    return mix(mix(hp(i, per), hp(i + vec2(1.0, 0.0), per), u.x),\n"                         \
    "               mix(hp(i + vec2(0.0, 1.0), per), hp(i + vec2(1.0, 1.0), per), u.x), u.y);\n"   \
    "}\n"                                                                                          \
    "vec3 pcells(vec2 x, vec2 per, float t) {\n"                                                   \
    "    vec2 n = floor(x), f = fract(x);\n"                                                       \
    "    float d1 = 8.0, d2 = 8.0, id = 0.0;\n"                                                    \
    "    for (int j = -1; j <= 1; j++)\n"                                                          \
    "        for (int i = -1; i <= 1; i++) {\n"                                                    \
    "            vec2 g = vec2(float(i), float(j));\n"                                             \
    "            vec2 h = hp2(n + g, per);\n"                                                      \
    "            vec2 r = g + 0.5 + 0.42 * sin(t + 6.2831 * h) - f;\n"                             \
    "            float d = dot(r, r);\n"                                                           \
    "            if (d < d1) { d2 = d1; d1 = d; id = h.x; } else if (d < d2) d2 = d;\n"            \
    "        }\n"                                                                                  \
    "    d1 = sqrt(d1);\n"                                                                         \
    "    return vec3(d1, sqrt(d2) - d1, id);\n"                                                    \
    "}\n"

/* the surface's light, focused by its ripples into a moving web: two
 * layers of cell walls, the space warped so the walls curve and flow; a
 * tile CAUSTIC_TILE units across, 20 cells of the first layer and 29 of
 * the second, so both wrap */
static const char *CAUSTIC_FS = "#version 330 core\n" TILE_GLSL
                                "uniform vec2 uSize;\n"
                                "uniform float uTime;\n"
                                "out vec4 FragColor;\n"
                                "void main() {\n"
                                "    vec2 q = gl_FragCoord.xy / uSize * 20.0;\n"
                                "    q += 0.55 * vec2(pnoise(q * 0.6 + uTime * 0.07, vec2(12.0)),\n"
                                "                     pnoise(q * 0.6 - uTime * 0.06 + 5.2, vec2(12.0))) - 0.27;\n"
                                "    float a = pcells(q + vec2(uTime * 0.020, uTime * 0.011), vec2(20.0), uTime * 0.55).y;\n"
                                "    float b = pcells(q * 1.45 + vec2(-uTime * 0.013, uTime * 0.017) + 3.7, vec2(29.0),\n"
                                "                     uTime * 0.7 + 1.3).y;\n"
                                "    float w = (1.0 - smoothstep(0.0, 0.28, a)) * 0.75 + (1.0 - smoothstep(0.0, 0.24, b)) * 0.55;\n"
                                "    FragColor = vec4(w * w, 0.0, 0.0, 1.0);\n"
                                "}\n";

/* sediment: patches, ripples, grain and pebbles, all wrapping at the
 * tile's edge (SEDIMENT_TILE units) */
static const char *SEDIMENT_FS =
    "#version 330 core\n" TILE_GLSL
    "uniform vec2 uSize;\n"
    "uniform vec3 uSedA;\n"
    "uniform vec3 uSedB;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy / uSize;\n"
    "    float n = 0.0, a = 0.5;\n"
    "    vec2 x = uv * 9.0, per = vec2(9.0);\n"
    "    for (int i = 0; i < 4; i++) { n += a * pnoise(x, per); x = x * 2.0 + 17.0; per *= 2.0; a *= 0.5; }\n"
    "    float ripple = 0.5 + 0.5 * sin(6.2831853 * (11.0 * uv.x + 7.0 * uv.y) + n * 6.0);\n"
    "    vec3 sed = mix(uSedA, uSedB, clamp(n * 1.3 - 0.15 + 0.15 * ripple, 0.0, 1.0));\n"
    "    sed *= 0.85 + 0.25 * pnoise(uv * 435.0, vec2(435.0));\n"
    "    vec3 c = pcells(uv * 56.0, vec2(56.0), 0.0);\n"
    "    sed = mix(sed, sed * 1.35, smoothstep(0.28, 0.12, c.x) * step(0.72, c.z) * 0.6);\n"
    "    FragColor = vec4(sed, 1.0);\n"
    "}\n";

/* a soft dark disc on the floor, multiplied in */
static const char *SHADOW_VS = "#version 330 core\n"
                               "layout(location = 0) in vec4 iS;\n"
                               "uniform mat4 uViewProj;\n"
                               "out vec2 vOff;\n"
                               "out float vDark;\n"
                               "out vec2 vXY;\n"
                               "const vec2 k[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),\n"
                               "                         vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
                               "void main() {\n"
                               "    vOff = k[gl_VertexID];\n"
                               "    vXY = iS.xy + vOff * iS.z;\n"
                               "    vDark = iS.w;\n"
                               "    gl_Position = uViewProj * vec4(vXY, 0.12, 1.0);\n"
                               "}\n";
static const char *SHADOW_FS = "#version 330 core\n"
                               "in vec2 vOff;\n"
                               "in float vDark;\n"
                               "in vec2 vXY;\n"
                               "uniform vec3 uBox;\n"
                               "out vec4 FragColor;\n"
                               "void main() {\n"
                               "    if (any(lessThan(vXY, vec2(0.0))) || any(greaterThan(vXY, uBox.xy))) discard;\n"
                               "    float a = vDark * (1.0 - smoothstep(0.15, 1.0, length(vOff)));\n"
                               "    FragColor = vec4(0.0, 0.0, 0.0, a);\n"
                               "}\n";

/* a shaft of light: a strip from the surface along the light's way down,
 * widening, turned to face the eye, each its own place, width and flicker */
static const char *SHAFT_VS =
    "#version 330 core\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uBox;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uEye;\n"
    "uniform float uTime;\n"
    "out vec2 vUV;\n"
    "out vec3 vWorld;\n"
    "out float vSeed;\n"
    "const vec2 k[6] = vec2[](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),\n"
    "                         vec2(-1.0, 0.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
    "float h(float n) { return fract(sin(n * 12.9898) * 43758.5453); }\n"
    "void main() {\n"
    "    float f = float(gl_InstanceID) + 1.0;\n"
    "    vec3 down = -uLightDir;\n"
    "    float dz = max(-down.z, 0.2);\n"
    "    vec2 travel = down.xy / dz * uBox.z * 0.9;\n"
    "    vec2 lo = max(vec2(0.0), -travel), hi = max(lo, uBox.xy - max(vec2(0.0), travel));\n"
    "    vec2 top = mix(lo, hi, vec2(h(f * 1.3), h(f * 7.7)))\n"
    "               + vec2(sin(uTime * 0.05 + f), cos(uTime * 0.04 + f * 1.7)) * 20.0;\n"
    "    vec3 t = vec3(top, uBox.z);\n"
    "    vec3 axis = down * (uBox.z * (0.55 + 0.35 * h(f * 3.1)) / dz);\n"
    "    vec3 side = normalize(cross(axis, uEye - (t + axis * 0.5)));\n"
    "    vec2 c = k[gl_VertexID];\n"
    "    float width = (8.0 + 16.0 * h(f * 5.3)) * uBox.z / 300.0;\n"
    "    vWorld = t + axis * c.y + side * c.x * width * (1.0 + 0.8 * c.y);\n"
    "    vUV = c;\n"
    "    vSeed = f;\n"
    "    gl_Position = uViewProj * vec4(vWorld, 1.0);\n"
    "}\n";
static const char *SHAFT_FS = "#version 330 core\n"
                              "in vec2 vUV;\n"
                              "in vec3 vWorld;\n"
                              "in float vSeed;\n" GLSL_COMMON
                              "uniform float uStrength;\n"
                              "out vec4 FragColor;\n"
                              "void main() {\n"
                              "    if (any(lessThan(vWorld.xy, vec2(0.0))) || any(greaterThan(vWorld.xy, uBox.xy))) discard;\n"
                              "    float edge = 1.0 - abs(vUV.x);\n"
                              "    float a = edge * edge * pow(1.0 - vUV.y, 1.3) * smoothstep(0.0, 0.06, vUV.y);\n"
                              "    a *= 0.6 + 0.4 * sin(uTime * 0.4 + vSeed * 2.1);\n"
                              "    vec3 c = mix(uFog * 3.0, vec3(1.0, 0.97, 0.85), 0.5) * a * uStrength * 0.09;\n"
                              "    FragColor = vec4(finish(c), 1.0);\n"
                              "}\n";

/* a soft round point turned to the eye */
static const char *POINT_VS = "#version 330 core\n"
                              "layout(location = 0) in vec4 iP;\n"
                              "layout(location = 1) in vec4 iC;\n"
                              "uniform mat4 uViewProj;\n"
                              "uniform vec3 uRight;\n"
                              "uniform vec3 uUp;\n"
                              "out vec2 vOff;\n"
                              "out vec4 vC;\n"
                              "out vec3 vWorld;\n"
                              "const vec2 k[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),\n"
                              "                         vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));\n"
                              "void main() {\n"
                              "    vOff = k[gl_VertexID];\n"
                              "    vC = iC;\n"
                              "    vWorld = iP.xyz + (uRight * vOff.x + uUp * vOff.y) * iP.w;\n"
                              "    gl_Position = uViewProj * vec4(vWorld, 1.0);\n"
                              "}\n";
static const char *POINT_FS = "#version 330 core\n"
                              "in vec2 vOff;\n"
                              "in vec4 vC;\n"
                              "in vec3 vWorld;\n" GLSL_COMMON
                              "uniform int uFogged;\n"
                              "out vec4 FragColor;\n"
                              "void main() {\n"
                              "    float a = vC.a * pow(max(1.0 - length(vOff), 0.0), 1.6);\n"
                              "    vec3 c = vC.rgb;\n"
                              "    if (uFogged == 1) {\n"
                              "        float d = length(uEye - vWorld);\n"
                              "        a *= 1.0 - 0.8 * clamp((d - uFogNear) / max(uFogFar - uFogNear, 1.0), 0.0, 1.0);\n"
                              "    }\n"
                              "    FragColor = vec4(finish(c), a);\n"
                              "}\n";

/* the surface: ripples, from above a glint and the sky's sheen at a
 * glance, from below a bright ceiling with the caustics' web in it */
static const char *SURFACE_FS =
    "#version 330 core\n"
    "in vec3 vWorld;\n" GLSL_COMMON
    "uniform vec3 uLightDir;\n"
    "uniform sampler2D uCaust;\n"
    "uniform float uCaustTile;\n"
    "out vec4 FragColor;\n"
    "/* the ripples' slope: long swells and short chop, each its own way */\n"
    "vec2 slope(vec2 p) {\n"
    "    const vec2 d[6] = vec2[](vec2(0.21, 0.12), vec2(-0.16, 0.27), vec2(0.37, -0.23),\n"
    "                             vec2(0.52, 0.45), vec2(-0.71, 0.38), vec2(0.58, -0.84));\n"
    "    const float a[6] = float[](0.50, 0.35, 0.25, 0.15, 0.10, 0.07);\n"
    "    const float w[6] = float[](1.1, 1.5, 1.9, 2.6, 3.2, 3.9);\n"
    "    vec2 g = vec2(0.0);\n"
    "    for (int i = 0; i < 6; i++) g += a[i] * cos(dot(p, d[i]) + uTime * w[i]) * d[i];\n"
    "    return g;\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = vWorld.xy;\n"
    "    vec3 n = normalize(vec3(-slope(p) * 0.9, 1.0));\n"
    "    vec3 V = normalize(uEye - vWorld);\n"
    "    vec3 c;\n"
    "    float a;\n"
    "    if (uEye.z > uBox.z) {\n"
    "        float fres = pow(1.0 - max(dot(n, V), 0.0), 4.0);\n"
    "        float glint = pow(max(dot(reflect(-V, n), uLightDir), 0.0), 180.0);\n"
    "        c = uFog * 1.6 + vec3(0.25, 0.30, 0.32) * fres + vec3(1.0, 0.97, 0.9) * glint * 4.0;\n"
    "        a = 0.03 + 0.22 * fres + min(glint * 4.0, 0.6);\n"
    "    } else {\n"
    "        c = uFog * 2.4 + vec3(0.35, 0.45, 0.45) * texture(uCaust, p / uCaustTile).r;\n"
    "        a = 0.6;\n"
    "    }\n"
    "    FragColor = vec4(finish(through_water(c, vWorld)), clamp(a, 0.0, 1.0));\n"
    "}\n";

/* ---- passes ---- */

#define CAUSTIC_TILE 200.0f
#define CAUSTIC_PX 256
#define SEDIMENT_TILE 256.0f
#define SEDIMENT_PX 1024

static GLuint g_back, g_floor, g_shadow, g_shaft, g_point, g_surface, g_caustic, g_sediment;
static GLuint g_tile_fbo, g_caust_tex, g_sed_tex;
static float g_sed_rgb[6] = {-1};
static GLuint g_empty_vao, g_shadow_vao, g_shadow_vbo, g_point_vao, g_point_vbo;
static size_t g_shadow_cap, g_point_cap;

static void common(GLuint prog, const SceneView *v)
{
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uViewProj"), 1, GL_FALSE, v->view_proj);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uInvViewProj"), 1, GL_FALSE, v->inv_view_proj);
    glUniform3fv(glGetUniformLocation(prog, "uEye"), 1, v->eye);
    glUniform3fv(glGetUniformLocation(prog, "uRight"), 1, v->right);
    glUniform3fv(glGetUniformLocation(prog, "uUp"), 1, v->up);
    glUniform3fv(glGetUniformLocation(prog, "uLightDir"), 1, v->light_dir);
    glUniform3fv(glGetUniformLocation(prog, "uFog"), 1, v->fog_rgb);
    glUniform3fv(glGetUniformLocation(prog, "uDeep"), 1, v->deep_rgb);
    glUniform3fv(glGetUniformLocation(prog, "uBox"), 1, v->box);
    glUniform1f(glGetUniformLocation(prog, "uFogNear"), v->fog_near);
    glUniform1f(glGetUniformLocation(prog, "uFogFar"), v->fog_far);
    glUniform1f(glGetUniformLocation(prog, "uTime"), v->time);
    glUniform1i(glGetUniformLocation(prog, "uDirect"), v->direct);
    glUniform1f(glGetUniformLocation(prog, "uExposure"), g_graphics.exposure);
}

/* a VAO reading one vec4 per instance from each of n buffers' slots */
static void instanced(GLuint *vao, GLuint *vbo, int vec4s)
{
    glGenVertexArrays(1, vao);
    glBindVertexArray(*vao);
    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    for (int i = 0; i < vec4s; i++) {
        glEnableVertexAttribArray((GLuint)i);
        glVertexAttribPointer((GLuint)i, 4, GL_FLOAT, GL_FALSE, (GLsizei)(vec4s * 4 * sizeof(float)),
                              (void *)(i * 4 * sizeof(float)));
        glVertexAttribDivisor((GLuint)i, 1);
    }
    glBindVertexArray(0);
}

static void upload(GLuint vbo, size_t *cap, const void *data, size_t bytes)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (bytes > *cap) {
        *cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)*cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, data);
}

void water_init(void)
{
    g_back = shader_program(FULL_VS, BACK_FS);
    g_floor = shader_program(PLANE_VS, FLOOR_FS);
    g_shadow = shader_program(SHADOW_VS, SHADOW_FS);
    g_shaft = shader_program(SHAFT_VS, SHAFT_FS);
    g_point = shader_program(POINT_VS, POINT_FS);
    g_surface = shader_program(PLANE_VS, SURFACE_FS);
    g_caustic = shader_program(FULL_VS, CAUSTIC_FS);
    g_sediment = shader_program(FULL_VS, SEDIMENT_FS);
    glGenFramebuffers(1, &g_tile_fbo);
    GLuint *texs[2] = {&g_caust_tex, &g_sed_tex};
    for (int i = 0; i < 2; i++) {
        glGenTextures(1, texs[i]);
        glBindTexture(GL_TEXTURE_2D, *texs[i]);
        if (i == 0)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, CAUSTIC_PX, CAUSTIC_PX, 0, GL_RED, GL_HALF_FLOAT, NULL);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SEDIMENT_PX, SEDIMENT_PX, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenVertexArrays(1, &g_empty_vao);
    instanced(&g_shadow_vao, &g_shadow_vbo, 1);
    instanced(&g_point_vao, &g_point_vbo, 2);
}

void water_destroy(void)
{
    GLuint progs[] = {g_back, g_floor, g_shadow, g_shaft, g_point, g_surface, g_caustic, g_sediment};
    for (size_t i = 0; i < sizeof progs / sizeof *progs; i++)
        glDeleteProgram(progs[i]);
    GLuint bufs[] = {g_shadow_vbo, g_point_vbo};
    glDeleteBuffers(2, bufs);
    GLuint vaos[] = {g_empty_vao, g_shadow_vao, g_point_vao};
    glDeleteVertexArrays(3, vaos);
    GLuint texs[] = {g_caust_tex, g_sed_tex};
    glDeleteTextures(2, texs);
    glDeleteFramebuffers(1, &g_tile_fbo);
}

/* a tile drawn by prog into tex, size x size, and its mipmaps */
static void tile(GLuint prog, GLuint tex, int size)
{
    glBindFramebuffer(GL_FRAMEBUFFER, g_tile_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, size, size);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glUseProgram(prog);
        glUniform2f(glGetUniformLocation(prog, "uSize"), (float)size, (float)size);
        glBindVertexArray(g_empty_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
}

void water_prepare(SceneView *v, const float sed_a[3], const float sed_b[3])
{
    v->caustic_tex = 0;
    v->caustic_tile = CAUSTIC_TILE;
    if (!g_caustic)
        return;
    if (v->caustics > 0) {
        glUseProgram(g_caustic);
        glUniform1f(glGetUniformLocation(g_caustic, "uTime"), v->time);
        tile(g_caustic, g_caust_tex, CAUSTIC_PX);
        v->caustic_tex = g_caust_tex;
    }
    if (sed_a && sed_b && (memcmp(g_sed_rgb, sed_a, 3 * sizeof(float)) || memcmp(g_sed_rgb + 3, sed_b, 3 * sizeof(float)))) {
        memcpy(g_sed_rgb, sed_a, 3 * sizeof(float));
        memcpy(g_sed_rgb + 3, sed_b, 3 * sizeof(float));
        glUseProgram(g_sediment);
        glUniform3fv(glGetUniformLocation(g_sediment, "uSedA"), 1, sed_a);
        glUniform3fv(glGetUniformLocation(g_sediment, "uSedB"), 1, sed_b);
        tile(g_sediment, g_sed_tex, SEDIMENT_PX);
    }
    glUseProgram(0);
}

void water_background(const SceneView *v, int w, int h)
{
    if (!g_back || w <= 0 || h <= 0)
        return;
    common(g_back, v);
    glUniform2f(glGetUniformLocation(g_back, "uInvSize"), 1.0f / (float)w, 1.0f / (float)h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glBindVertexArray(g_empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void water_floor(const SceneView *v, float light)
{
    if (!g_floor)
        return;
    common(g_floor, v);
    glUniform1f(glGetUniformLocation(g_floor, "uZ"), 0.0f);
    glUniform1i(glGetUniformLocation(g_floor, "uSed"), 0);
    glUniform1i(glGetUniformLocation(g_floor, "uCaust"), 1);
    glUniform1f(glGetUniformLocation(g_floor, "uSedTile"), SEDIMENT_TILE);
    glUniform1f(glGetUniformLocation(g_floor, "uCaustTile"), CAUSTIC_TILE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_sed_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, v->caustic_tex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(glGetUniformLocation(g_floor, "uLight"), light);
    glUniform1f(glGetUniformLocation(g_floor, "uCaustics"), v->caustic_tex ? v->caustics : 0.0f);
    glUniform1f(glGetUniformLocation(g_floor, "uAmbient"), g_graphics.ambient);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(g_empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void water_shadows(const SceneView *v, const WaterShadow *s, int n)
{
    if (!g_shadow || n <= 0)
        return;
    upload(g_shadow_vbo, &g_shadow_cap, s, (size_t)n * sizeof *s);
    common(g_shadow, v);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_shadow_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void water_shafts(const SceneView *v, int count, float strength)
{
    if (!g_shaft || count <= 0 || strength <= 0)
        return;
    common(g_shaft, v);
    glUniform1f(glGetUniformLocation(g_shaft, "uStrength"), strength);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(g_empty_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void water_points(const SceneView *v, const WaterPoint *p, int n, bool fogged)
{
    if (!g_point || n <= 0)
        return;
    upload(g_point_vbo, &g_point_cap, p, (size_t)n * sizeof *p);
    common(g_point, v);
    glUniform1i(glGetUniformLocation(g_point, "uFogged"), fogged);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(g_point_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void water_surface(const SceneView *v)
{
    if (!g_surface)
        return;
    common(g_surface, v);
    glUniform1f(glGetUniformLocation(g_surface, "uZ"), v->box[2]);
    glUniform1i(glGetUniformLocation(g_surface, "uCaust"), 0);
    glUniform1f(glGetUniformLocation(g_surface, "uCaustTile"), CAUSTIC_TILE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->caustic_tex);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}
