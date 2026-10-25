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

#include "render/bodies.h"

#include <epoxy/gl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "render/glsl.h"
#include "render/lighting.h"
#include "render/shader.h"

/* The mesh: rings round the axis, each with its first vertex again at the
 * end (so the angle round runs 0 to 2 pi without a jump): a cap's worth
 * at the start (t = 0, bulging back), the rings along it (the cap's
 * latitude 0), a cap's worth at the end. Each vertex is (t, the angle
 * round, the cap's latitude); the shader makes the rest. Two of them: a
 * fine one, and a coarse one for what is small on the screen. */
typedef struct {
    int around, along, caps;
} MeshSize;
static const MeshSize k_mesh[2] = {{14, 8, 3}, {6, 2, 1}};
/* how many pixels a segment spans, its length and thickness, decides how
 * it is drawn: the fine mesh, the coarse one, or (under STROKE_PX) a line */
#define FINE_PX 28.0f
#define STROKE_PX 7.0f

static const char *VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aP;\n"
    "layout(location = 2) in vec3 iA;\n"
    "layout(location = 3) in vec3 iB;\n"
    "layout(location = 4) in vec2 iR;\n"
    "layout(location = 5) in vec3 iRgb;\n"
    "layout(location = 6) in float iGlow;\n"
    "layout(location = 7) in vec3 iUp;\n"
    "layout(location = 8) in vec3 iKind;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uEye;\n"
    "uniform float uDepthStep;\n"
    "uniform float uTime;\n"
    "out vec3 vWorld;\n"
    "out vec3 vNormal;\n"
    "out vec3 vRgb;\n"
    "out float vGlow;\n"
    "out vec3 vSurf;\n"
    "out vec3 vT;\n"
    "flat out vec3 vKind;\n"
    "flat out float vLen;\n"
    "const float PI = 3.14159265;\n"
    "float K, PH, TIP, LEN;\n"
    "vec3 W, U, V;\n"
    "/* the cross-section's half-widths at t: across the blade, and through it */\n"
    "vec2 profile(float t) {\n"
    "    float r = mix(iR.x, iR.y, t);\n"
    "    float s = sin(PI * clamp(t, 0.0, 1.0));\n"
    "    if (K < 0.5) {\n"
    "        float w = clamp(LEN * 0.24, 1.2, 6.5) * pow(s, 0.7);\n"
    "        return vec2(max(r * 0.75, w), max(r * 0.55, 0.30 + 0.30 * s));\n"
    "    }\n"
    "    if (K < 1.5) {\n"
    "        float c = mix(iR.x * 1.05, TIP > 0.5 ? 0.10 : iR.y * 0.8, pow(t, 0.85));\n"
    "        return vec2(c, c * 0.88);\n"
    "    }\n"
    "    if (K < 2.5) return vec2(r * 0.72);\n"
    "    if (K < 3.5) {\n"
    "        float n = max(2.0, floor(LEN / 3.5));\n"
    "        float c = r * 1.15 * (1.0 + 0.12 * pow(abs(sin(PI * n * t)), 0.35));\n"
    "        return vec2(c, c * 0.92);\n"
    "    }\n"
    "    if (K < 4.5) return vec2(r * (0.95 + 0.25 * s));\n"
    "    if (K < 5.5) {\n"
    "        float n = max(2.0, floor(LEN / 2.5));\n"
    "        return vec2(r * 0.82 * (1.0 + 0.16 * smoothstep(0.6, 1.0, abs(sin(PI * n * t)))\n"
    "                                + 0.3 * (pow(1.0 - t, 8.0) + pow(t, 8.0))));\n"
    "    }\n"
    "    if (K < 6.5) return vec2(r * (1.0 + 0.65 * s));\n"
    "    if (K < 7.5) return vec2(r * (1.0 + 0.9 * pow(s, 0.8)));\n"
    "    if (K < 8.5) {\n"
    "        float c = r * 0.72 * (1.0 + 0.14 * sin(t * LEN * 1.3 + PH * 6.0));\n"
    "        return vec2(c * 1.08, c * 0.8);\n"
    "    }\n"
    "    return vec2(iR.x);\n"
    "}\n"
    "/* the axis's own bend: cilia beat, a claw at an arm's end hooks */\n"
    "vec2 bend(float t) {\n"
    "    if (K > 1.5 && K < 2.5) {\n"
    "        float a = clamp(LEN * 0.07, 0.3, 1.6) * t;\n"
    "        float w = 2.0 * PI * 1.2 * t - uTime * 7.0 + PH * 6.2831;\n"
    "        return vec2(a * sin(w), 0.5 * a * cos(w * 0.9));\n"
    "    }\n"
    "    if (K > 0.5 && K < 1.5 && TIP > 0.5) return vec2(0.0, 0.10 * LEN * t * t);\n"
    "    return vec2(0.0);\n"
    "}\n"
    "vec3 centre(float t) {\n"
    "    vec2 o = bend(t);\n"
    "    return iA + W * (LEN * t) + U * o.x + V * o.y;\n"
    "}\n"
    "vec3 at(float t, float th, float ph) {\n"
    "    vec2 r = profile(t);\n"
    "    float cp = cos(ph);\n"
    "    return centre(t) + W * (sin(ph) * min(r.x, r.y)) + U * (cos(th) * r.x * cp)\n"
    "           + V * (sin(th) * r.y * cp);\n"
    "}\n"
    "void main() {\n"
    "    K = iKind.x;\n"
    "    PH = iKind.y;\n"
    "    TIP = iKind.z;\n"
    "    vec3 axis = iB - iA;\n"
    "    LEN = length(axis);\n"
    "    W = LEN > 1e-5 ? axis / LEN : vec3(0.0, 0.0, 1.0);\n"
    "    vec3 up = iUp - W * dot(iUp, W);\n"
    "    if (dot(up, up) < 1e-6)\n"
    "        up = abs(W.z) < 0.9 ? vec3(0.0, 0.0, 1.0) - W * W.z : vec3(1.0, 0.0, 0.0) - W * W.x;\n"
    "    V = normalize(up);\n"
    "    U = cross(V, W);\n"
    "    float t = aP.x, th = aP.y, ph = aP.z;\n"
    "    vec3 p = at(t, th, ph);\n"
    "    vec2 r = profile(t);\n"
    "    vec3 n;\n"
    "    if (ph != 0.0 || LEN < 1e-4) {\n"
    "        /* a cap: an ellipsoid's own normal */\n"
    "        float cp = cos(ph);\n"
    "        n = U * (cos(th) * cp / r.x) + V * (sin(th) * cp / r.y) + W * (sin(ph) / min(r.x, r.y));\n"
    "    } else {\n"
    "        /* along the body: the cross-section's own normal, tipped back\n"
    "           as the body widens (the bend's own slope left out) */\n"
    "        float t0 = max(t - 0.03, 0.0), t1 = min(t + 0.03, 1.0);\n"
    "        vec2 dr = (profile(t1) - profile(t0)) / (t1 - t0);\n"
    "        vec3 ncs = U * (cos(th) / r.x) + V * (sin(th) / r.y);\n"
    "        vec3 T = W * LEN + U * (cos(th) * dr.x) + V * (sin(th) * dr.y);\n"
    "        n = ncs - T * (dot(ncs, T) / max(dot(T, T), 1e-6));\n"
    "    }\n"
    "    /* Bodies that meet have surfaces in the same place, which would\n"
    "       fight for the same pixels. Each is nudged toward the eye by its\n"
    "       own few steps of depth precision, so one always wins. */\n"
    "    p += normalize(uEye - p) * uDepthStep * float(gl_InstanceID % 13);\n"
    "    vNormal = n;\n"
    "    vWorld = p;\n"
    "    vRgb = iRgb;\n"
    "    vGlow = iGlow;\n"
    "    vSurf = vec3(LEN * t + sin(ph) * min(r.x, r.y), cos(th) * r.x, th);\n"
    "    vT = vec3(t, r);\n"
    "    vKind = iKind;\n"
    "    vLen = LEN;\n"
    "    gl_Position = uViewProj * vec4(p, 1.0);\n"
    "}\n";

/* Each tissue's pattern and finish, then the light: wrapped round from the
 * key light (rippled by the caustics where it faces up), the water's own
 * from above and below, light through thin flesh from behind and at the
 * edges, a membrane's rim, a wet shine; then the water to the eye. The
 * patterns fade where a pixel spans more than they can show. */
static const char *FS =
    "#version 330 core\n"
    "in vec3 vWorld;\n"
    "in vec3 vNormal;\n"
    "in vec3 vRgb;\n"
    "in float vGlow;\n"
    "in vec3 vSurf;\n"
    "in vec3 vT;\n"
    "flat in vec3 vKind;\n"
    "flat in float vLen;\n" GLSL_COMMON
    "uniform vec3 uLightDir;\n"
    "uniform float uAmbient;\n"
    "uniform float uTissue;\n"
    "uniform float uSss;\n"
    "uniform float uCaustics;\n"
    "uniform sampler2D uCaustTex;\n"
    "uniform float uCaustTile;\n"
    "out vec4 FragColor;\n"
    "/* cells round the body, whole ones (the angle wraps), about size across */\n"
    "vec3 skin(float x, float th, float r, float size, float t) {\n"
    "    float per = max(3.0, floor(2.0 * PI * r / size));\n"
    "    return cells(vec2(x / size, th / (2.0 * PI) * per), per, t);\n"
    "}\n"
    "void main() {\n"
    "    float K = vKind.x, PH = vKind.y, TIP = vKind.z;\n"
    "    vec3 N = normalize(vNormal);\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 V = normalize(uEye - vWorld);\n"
    "    vec3 L = uLightDir;\n"
    "    float x = vSurf.x, s = vSurf.y, th = vSurf.z, t = vT.x;\n"
    "    float r = max(0.5 * (vT.y + vT.z), 0.2);\n"
    "    float tex = uTissue * (1.0 - smoothstep(0.15, 0.6, fwidth(x)));\n"
    "    vec3 alb = vRgb;\n"
    "    float gloss = 0.25, shine = 24.0, sss = 0.35, rimk = 0.35, emit = 0.0;\n"
    "    if (K > 8.5) {\n"
    "        /* the cell at the hub: a pale membrane, a dark nucleus seen\n"
    "           through it, granules drifting, a slow pulse */\n"
    "        float nucleus = smoothstep(0.55, 0.92, max(dot(N, V), 0.0));\n"
    "        alb = mix(alb * 1.15, alb * 0.45, nucleus * uTissue);\n"
    "        if (tex > 0.01)\n"
    "            alb *= 1.0 - 0.15 * tex\n"
    "                   * smoothstep(0.35, 0.1, cells(N.xy * 3.0 + N.z * 1.7, 0.0, uTime * 0.3 + PH * 6.0).x);\n"
    "        rimk = 0.9; sss = 0.9; gloss = 0.6; shine = 40.0;\n"
    "        emit = 0.08 * (0.5 + 0.5 * sin(uTime * 2.4 + PH * 6.2831)) * uTissue;\n"
    "    } else if (tex < 0.01) {\n"
    "        /* too small to show a pattern: the finish only */\n"
    "        if (K < 0.5) sss = 1.0;\n"
    "        else if (K < 1.5) { gloss = 0.8; shine = 70.0; }\n"
    "        else if (K > 7.5) { gloss = 0.05; sss = 0.05; rimk = 0.1; }\n"
    "    } else if (K < 0.5) {\n"
    "        /* a leaf: a midrib, veins swept toward the tip, chloroplasts */\n"
    "        float midrib = 1.0 - smoothstep(0.10, 0.32, abs(s));\n"
    "        float vv = fract((x - abs(s) * 1.2) / 2.4);\n"
    "        float side = (1.0 - smoothstep(0.03, 0.10, min(vv, 1.0 - vv)))\n"
    "                     * (1.0 - smoothstep(0.6, 1.0, abs(s) / max(vT.y, 0.5)));\n"
    "        float vein = max(midrib, side * 0.75);\n"
    "        float chloro = smoothstep(0.42, 0.12, cells(vec2(x, s) * 1.6, 0.0, 0.0).x);\n"
    "        alb *= 1.0 - 0.22 * chloro * tex;\n"
    "        alb = mix(alb, alb * 1.45 + vec3(0.06, 0.08, 0.0), vein * 0.55 * tex);\n"
    "        gloss = 0.35; shine = 40.0; sss = 1.0;\n"
    "    } else if (K < 1.5) {\n"
    "        /* a claw: muscle fibres, darkening to horn at an arm's end */\n"
    "        alb *= mix(1.0, 0.82 + 0.18 * (0.5 + 0.5 * sin(th * 9.0 + x * 0.6)), tex);\n"
    "        float horn = TIP > 0.5 ? smoothstep(0.35, 1.0, t) : 0.0;\n"
    "        alb = mix(alb, alb * 0.35 + vec3(0.10, 0.04, 0.03), horn * tex);\n"
    "        gloss = 0.8; shine = 70.0; sss = 0.25;\n"
    "    } else if (K < 2.5) {\n"
    "        /* cilia: bands of beating hairs running down the whip */\n"
    "        float band = smoothstep(0.55, 1.0, sin(x * 3.2 - uTime * 9.0 + PH * 6.2831));\n"
    "        alb = mix(alb, alb * 1.35, band * tex * 0.6);\n"
    "        rimk = 0.9; sss = 0.8; gloss = 0.4; shine = 30.0;\n"
    "    } else if (K < 3.5) {\n"
    "        /* a shield: hard plates with dark seams */\n"
    "        vec3 c = skin(x, th, r, 1.6, 0.0);\n"
    "        float seam = 1.0 - smoothstep(0.0, 0.10, c.y);\n"
    "        alb *= mix(1.0, 0.75 + 0.35 * c.z - 0.45 * seam, tex);\n"
    "        gloss = 1.0; shine = 90.0; sss = 0.1;\n"
    "    } else if (K < 4.5) {\n"
    "        /* a spore: glowing spots that come and go */\n"
    "        vec3 c = skin(x, th, r, 1.1, 0.0);\n"
    "        alb = mix(alb, alb * 0.75, tex * 0.5);\n"
    "        emit = smoothstep(0.30, 0.12, c.x) * tex * (0.35 + 0.25 * sin(uTime * 2.0 + c.z * 6.28));\n"
    "        sss = 0.7; gloss = 0.3;\n"
    "    } else if (K < 5.5) {\n"
    "        /* bone: rings, and grain */\n"
    "        float ring = smoothstep(0.75, 1.0, abs(sin(PI * max(2.0, floor(vLen / 2.5)) * t)));\n"
    "        float grain = fbm(vec2(x * 1.5, th * 3.0));\n"
    "        alb *= mix(1.0, (0.85 - 0.3 * ring) * (0.8 + 0.4 * grain), tex);\n"
    "        gloss = 0.15; shine = 12.0; sss = 0.1;\n"
    "    } else if (K < 6.5) {\n"
    "        /* eggs in a sac */\n"
    "        float egg = smoothstep(0.42, 0.18, skin(x, th, r, 1.3, 0.0).x);\n"
    "        alb = mix(alb, mix(alb * 0.55, alb * 1.35 + 0.05, egg), tex);\n"
    "        sss = 0.9; gloss = 0.6; shine = 50.0;\n"
    "    } else if (K < 7.5) {\n"
    "        /* a bladder: bubbles rising through it */\n"
    "        vec3 c = skin(x - uTime * 0.35, th, r, 1.2, PH * 6.0);\n"
    "        float bub = smoothstep(0.34, 0.24, c.x) * step(0.45, c.z);\n"
    "        alb = mix(alb * 0.7, alb * 1.2 + 0.08, bub * tex);\n"
    "        emit = bub * smoothstep(0.16, 0.0, c.x) * tex * 0.8;\n"
    "        sss = 1.0; gloss = 0.7; shine = 60.0; rimk = 0.6;\n"
    "    } else {\n"
    "        /* dead: wrinkled, dull */\n"
    "        alb *= mix(1.0, 0.55 + 0.6 * fbm(vec2(x * 0.9 + PH * 13.0, th * 2.5)), tex);\n"
    "        gloss = 0.05; sss = 0.05; rimk = 0.1;\n"
    "    }\n"
    "    float ndl = dot(N, L);\n"
    "    float diff = max((ndl + 0.35) / 1.35, 0.0);\n"
    "    float ca = 1.0;\n"
    "    if (uCaustics > 0.0 && N.z > 0.05)\n"
    "        ca += uCaustics * 1.2 * N.z\n"
    "              * texture(uCaustTex, (vWorld.xy - L.xy / max(L.z, 0.2) * (uBox.z - vWorld.z)) / uCaustTile).r;\n"
    "    vec3 hemi = mix(water_at(0.0), water_at(uBox.z), N.z * 0.5 + 0.5);\n"
    "    vec3 col = alb * (uAmbient + 0.85 * diff * ca) + alb * hemi * 0.6;\n"
    "    float through = pow(clamp(dot(-L, V) * 0.5 + 0.5, 0.0, 1.0), 3.0) * 0.6 + max(-ndl, 0.0) * 0.5;\n"
    "    col += alb * vec3(1.0, 0.95, 0.75) * through * sss * uSss;\n"
    "    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);\n"
    "    col += mix(alb, vec3(1.0), 0.35) * rim * rimk * (0.35 + 0.65 * uSss);\n"
    "    col += vec3(pow(max(dot(N, normalize(L + V)), 0.0), shine) * gloss * 0.6);\n"
    "    col += alb * (emit * 2.0 + vGlow * 2.2);\n"
    "    FragColor = vec4(finish(through_water(col, vWorld)), 1.0);\n"
    "}\n";

/* A segment as a line: a strip from end to end turned to face the eye, as
 * wide as the segment is thick or a pixel and a half, whichever is more,
 * brighter along its middle as a tube is; flat, still, cheap. Classic's
 * way of drawing every body, and every look's for what is far off. */
static const char *STROKE_VS =
    "#version 330 core\n"
    "layout(location = 2) in vec3 iA;\n"
    "layout(location = 3) in vec3 iB;\n"
    "layout(location = 4) in vec2 iR;\n"
    "layout(location = 5) in vec3 iRgb;\n"
    "layout(location = 6) in float iGlow;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec2 uViewport;\n"
    "uniform float uPxScale;\n"
    "uniform float uMinPx;\n"
    "uniform float uRadius;\n"
    "out vec3 vRgb;\n"
    "out float vGlow;\n"
    "out float vAcross;\n"
    "out vec3 vWorld;\n"
    "const vec2 k[6] = vec2[](vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),\n"
    "                         vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0));\n"
    "void main() {\n"
    "    vec2 c = k[gl_VertexID];\n"
    "    vec4 pa = uViewProj * vec4(iA, 1.0), pb = uViewProj * vec4(iB, 1.0);\n"
    "    if (pa.w < 1e-3 || pb.w < 1e-3) { gl_Position = vec4(0.0); return; }\n"
    "    vec2 sa = pa.xy / pa.w * 0.5 * uViewport, sb = pb.xy / pb.w * 0.5 * uViewport;\n"
    "    vec2 d = sb - sa;\n"
    "    float l = length(d);\n"
    "    vec2 along = l > 1e-3 ? d / l : vec2(1.0, 0.0), side = vec2(-along.y, along.x);\n"
    "    vec4 p = c.x < 0.5 ? pa : pb;\n"
    "    float r = uRadius > 0.0 ? uRadius : (c.x < 0.5 ? iR.x : iR.y);\n"
    "    float half_px = max(r * uPxScale / p.w, uMinPx * 0.5);\n"
    "    vec2 off = side * c.y * half_px + along * (c.x < 0.5 ? -half_px : half_px) * 0.5;\n"
    "    p.xy += off / (0.5 * uViewport) * p.w;\n"
    "    gl_Position = p;\n"
    "    vRgb = iRgb;\n"
    "    vGlow = iGlow;\n"
    "    vAcross = c.y;\n"
    "    vWorld = c.x < 0.5 ? iA : iB;\n"
    "}\n";
static const char *STROKE_FS = "#version 330 core\n"
                               "in vec3 vRgb;\n"
                               "in float vGlow;\n"
                               "in float vAcross;\n"
                               "in vec3 vWorld;\n" GLSL_COMMON
                               "uniform float uAmbient;\n"
                               "out vec4 FragColor;\n"
                               "void main() {\n"
                               "    float shade = 0.7 + 0.3 * (1.0 - vAcross * vAcross);\n"
                               "    vec3 col = vRgb * shade * (uAmbient + 0.8) + vRgb * vGlow * 2.2;\n"
                               "    FragColor = vec4(finish(through_water(col, vWorld)), 1.0);\n"
                               "}\n";

static GLuint g_prog, g_vao, g_mesh_vbo, g_mesh_ibo, g_inst_vbo, g_stroke;
static unsigned char *g_class;
static int g_first[2], g_count[2];
static size_t g_inst_cap;
static BodyInstance *g_stage;
static int g_stage_cap;

/* the per-instance attributes, from the first'th instance in the buffer
 * on (the VAO bound) */
static void point_instances(int first)
{
    glBindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    const GLsizei stride = sizeof(BodyInstance);
    const struct {
        GLuint loc;
        GLint n;
        size_t off;
    } attrs[] = {
        {2, 3, offsetof(BodyInstance, a)},   {3, 3, offsetof(BodyInstance, b)},
        {4, 2, offsetof(BodyInstance, r0)},  {5, 3, offsetof(BodyInstance, rgb)},
        {6, 1, offsetof(BodyInstance, glow)}, {7, 3, offsetof(BodyInstance, up)},
        {8, 3, offsetof(BodyInstance, tissue)},
    };
    for (size_t i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
        glEnableVertexAttribArray(attrs[i].loc);
        glVertexAttribPointer(attrs[i].loc, attrs[i].n, GL_FLOAT, GL_FALSE, stride,
                              (void *)(attrs[i].off + (size_t)first * sizeof(BodyInstance)));
        glVertexAttribDivisor(attrs[i].loc, 1);
    }
}
static struct {
    GLint view_proj, eye, depth_step, time, light, ambient, tissue, sss, caustics, caust_tex, caust_tile, fog,
        deep, box, fog_near, fog_far, direct, exposure;
} U;

void bodies_init(void)
{
    g_prog = shader_program(VS, FS);
    U.view_proj = glGetUniformLocation(g_prog, "uViewProj");
    U.eye = glGetUniformLocation(g_prog, "uEye");
    U.depth_step = glGetUniformLocation(g_prog, "uDepthStep");
    U.time = glGetUniformLocation(g_prog, "uTime");
    U.light = glGetUniformLocation(g_prog, "uLightDir");
    U.ambient = glGetUniformLocation(g_prog, "uAmbient");
    U.tissue = glGetUniformLocation(g_prog, "uTissue");
    U.sss = glGetUniformLocation(g_prog, "uSss");
    U.caustics = glGetUniformLocation(g_prog, "uCaustics");
    U.caust_tex = glGetUniformLocation(g_prog, "uCaustTex");
    U.caust_tile = glGetUniformLocation(g_prog, "uCaustTile");
    g_stroke = shader_program(STROKE_VS, STROKE_FS);
    U.fog = glGetUniformLocation(g_prog, "uFog");
    U.deep = glGetUniformLocation(g_prog, "uDeep");
    U.box = glGetUniformLocation(g_prog, "uBox");
    U.fog_near = glGetUniformLocation(g_prog, "uFogNear");
    U.fog_far = glGetUniformLocation(g_prog, "uFogFar");
    U.direct = glGetUniformLocation(g_prog, "uDirect");
    U.exposure = glGetUniformLocation(g_prog, "uExposure");

    float verts[4096];
    unsigned short idx[8192];
    int nv = 0, ni = 0;
    for (int m = 0; m < 2; m++) {
        int around = k_mesh[m].around, along = k_mesh[m].along, caps = k_mesh[m].caps;
        int rings = caps + along + 1 + caps, round = around + 1, base = nv / 3;
        for (int r = 0; r < rings; r++) {
            double t, ph;
            if (r < caps) {
                t = 0;
                ph = -(M_PI / 2) + (M_PI / 2) * r / caps;
            } else if (r <= caps + along) {
                t = (double)(r - caps) / along;
                ph = 0;
            } else {
                t = 1;
                ph = (M_PI / 2) * (r - caps - along) / caps;
            }
            for (int j = 0; j < round; j++) {
                verts[nv++] = (float)t;
                verts[nv++] = (float)(2 * M_PI * j / around);
                verts[nv++] = (float)ph;
            }
        }
        g_first[m] = ni;
        for (int r = 0; r + 1 < rings; r++)
            for (int j = 0; j < around; j++) {
                unsigned short a = (unsigned short)(base + r * round + j), b = (unsigned short)(a + 1);
                unsigned short c = (unsigned short)(a + round), d = (unsigned short)(c + 1);
                /* counter-clockwise seen from outside */
                idx[ni++] = a;
                idx[ni++] = b;
                idx[ni++] = c;
                idx[ni++] = b;
                idx[ni++] = d;
                idx[ni++] = c;
            }
        g_count[m] = ni - g_first[m];
    }

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_mesh_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof *verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glGenBuffers(1, &g_mesh_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof *idx), idx, GL_STATIC_DRAW);

    glGenBuffers(1, &g_inst_vbo);
    point_instances(0);
    glBindVertexArray(0);
}

void bodies_destroy(void)
{
    glDeleteBuffers(1, &g_inst_vbo);
    glDeleteBuffers(1, &g_mesh_ibo);
    glDeleteBuffers(1, &g_mesh_vbo);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteProgram(g_prog);
    glDeleteProgram(g_stroke);
    free(g_stage);
    free(g_class);
    g_stage = NULL;
    g_class = NULL;
    g_stage_cap = 0;
}

/* the lines: the same instances, from the first'th on */
static void draw_strokes(int first, int count, const SceneView *view)
{
    glUseProgram(g_stroke);
    glUniformMatrix4fv(glGetUniformLocation(g_stroke, "uViewProj"), 1, GL_FALSE, view->view_proj);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glUniform2f(glGetUniformLocation(g_stroke, "uViewport"), (float)vp[2], (float)vp[3]);
    glUniform1f(glGetUniformLocation(g_stroke, "uPxScale"), view->px_scale);
    glUniform1f(glGetUniformLocation(g_stroke, "uMinPx"), 1.5f);
    glUniform1f(glGetUniformLocation(g_stroke, "uRadius"), view->lines ? view->line_radius : 0.0f);
    glUniform1f(glGetUniformLocation(g_stroke, "uAmbient"), g_graphics.ambient);
    glUniform3fv(glGetUniformLocation(g_stroke, "uEye"), 1, view->eye);
    glUniform3fv(glGetUniformLocation(g_stroke, "uFog"), 1, view->fog_rgb);
    glUniform3fv(glGetUniformLocation(g_stroke, "uDeep"), 1, view->deep_rgb);
    glUniform3fv(glGetUniformLocation(g_stroke, "uBox"), 1, view->box);
    glUniform1f(glGetUniformLocation(g_stroke, "uFogNear"), view->fog_near);
    glUniform1f(glGetUniformLocation(g_stroke, "uFogFar"), view->fog_far);
    glUniform1i(glGetUniformLocation(g_stroke, "uDirect"), view->direct);
    glUniform1f(glGetUniformLocation(g_stroke, "uExposure"), g_graphics.exposure);
    glDisable(GL_CULL_FACE);
    point_instances(first);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
}

void bodies_draw(const BodyInstance *inst, int n, const SceneView *view)
{
    if (n <= 0 || !g_prog)
        return;
    /* fine, then coarse, then lines: by how many pixels each spans (all
     * lines, and no cells, when the look draws bodies as lines) */
    if (n > g_stage_cap) {
        g_stage_cap = n * 2;
        g_stage = realloc(g_stage, (size_t)g_stage_cap * sizeof *g_stage);
        g_class = realloc(g_class, (size_t)g_stage_cap);
        if (!g_stage || !g_class)
            abort();
    }
    int count[3] = {0};
    for (int i = 0; i < n; i++) {
        const BodyInstance *b = &inst[i];
        if (view->lines) {
            g_class[i] = b->tissue == TISSUE_CORE ? 3 : 2;
        } else if (view->px_scale <= 0) {
            g_class[i] = 0;
        } else {
            float dx = b->b[0] - b->a[0], dy = b->b[1] - b->a[1], dz = b->b[2] - b->a[2];
            float ex = b->a[0] - view->eye[0], ey = b->a[1] - view->eye[1], ez = b->a[2] - view->eye[2];
            float size = sqrtf(dx * dx + dy * dy + dz * dz) + 2 * fmaxf(b->r0, b->r1);
            float px = size * view->px_scale / (sqrtf(ex * ex + ey * ey + ez * ez) + 1e-3f);
            g_class[i] = px >= FINE_PX ? 0 : px >= STROKE_PX ? 1 : 2;
        }
        if (g_class[i] < 3)
            count[g_class[i]]++;
    }
    int at[3] = {0, count[0], count[0] + count[1]};
    for (int i = 0; i < n; i++)
        if (g_class[i] < 3)
            g_stage[at[g_class[i]]++] = inst[i];
    bodies_draw_tiers(g_stage, count, view);
}

void bodies_draw_tiers(const BodyInstance *inst, const int count[3], const SceneView *view)
{
    int total = count[0] + count[1] + count[2];
    if (total <= 0 || !g_prog)
        return;
    size_t bytes = (size_t)total * sizeof *inst;
    glBindBuffer(GL_ARRAY_BUFFER, g_inst_vbo);
    if (bytes > g_inst_cap) {
        g_inst_cap = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_inst_cap, NULL, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, inst);
    int nfine = count[0], ncoarse = count[1], nlines = count[2];
    glUseProgram(g_prog);
    glUniformMatrix4fv(U.view_proj, 1, GL_FALSE, view->view_proj);
    glUniform3fv(U.eye, 1, view->eye);
    glUniform1f(U.depth_step, view->depth_step);
    glUniform1f(U.time, view->time);
    glUniform3fv(U.light, 1, view->light_dir);
    glUniform1f(U.ambient, g_graphics.ambient);
    glUniform1f(U.tissue, view->tissue);
    glUniform1f(U.sss, view->sss);
    glUniform1f(U.caustics, view->caustic_tex ? view->caustics : 0.0f);
    glUniform1i(U.caust_tex, 0);
    glUniform1f(U.caust_tile, view->caustic_tile > 0 ? view->caustic_tile : 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, view->caustic_tex);
    glUniform3fv(U.fog, 1, view->fog_rgb);
    glUniform3fv(U.deep, 1, view->deep_rgb);
    glUniform3fv(U.box, 1, view->box);
    glUniform1f(U.fog_near, view->fog_near);
    glUniform1f(U.fog_far, view->fog_far);
    glUniform1i(U.direct, view->direct);
    glUniform1f(U.exposure, g_graphics.exposure);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(g_vao);
    if (nfine) {
        point_instances(0);
        glDrawElementsInstanced(GL_TRIANGLES, g_count[0], GL_UNSIGNED_SHORT,
                                (void *)(g_first[0] * sizeof(unsigned short)), nfine);
    }
    if (ncoarse) {
        point_instances(nfine);
        glDrawElementsInstanced(GL_TRIANGLES, g_count[1], GL_UNSIGNED_SHORT,
                                (void *)(g_first[1] * sizeof(unsigned short)), ncoarse);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (nlines)
        draw_strokes(nfine + ncoarse, nlines, view);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}
