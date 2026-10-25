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

/* GLSL the 3D passes share, pasted into their fragment shaders: noise,
 * cells, the water's colour with depth and the fog through it, and the
 * finish for what is drawn straight to the screen. Wants the uniforms
 * below. */
#ifndef RENDER_GLSL_H
#define RENDER_GLSL_H

#define GLSL_COMMON                                                                                \
    "uniform vec3 uEye;\n"                                                                         \
    "uniform vec3 uFog;\n"                                                                         \
    "uniform vec3 uDeep;\n"                                                                        \
    "uniform vec3 uBox;\n"                                                                         \
    "uniform float uFogNear;\n"                                                                    \
    "uniform float uFogFar;\n"                                                                     \
    "uniform float uTime;\n"                                                                       \
    "uniform int uDirect;\n"                                                                       \
    "uniform float uExposure;\n"                                                                   \
    "const float PI = 3.14159265;\n"                                                               \
    "vec2 hash2(vec2 p) {\n"                                                                       \
    "    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));\n"                      \
    "    return fract(sin(p) * 43758.5453);\n"                                                     \
    "}\n"                                                                                          \
    "float hash1(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"     \
    "float vnoise(vec2 p) {\n"                                                                     \
    "    vec2 i = floor(p), f = fract(p);\n"                                                       \
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"                                                      \
    "    return mix(mix(hash1(i), hash1(i + vec2(1.0, 0.0)), u.x),\n"                              \
    "               mix(hash1(i + vec2(0.0, 1.0)), hash1(i + vec2(1.0, 1.0)), u.x), u.y);\n"       \
    "}\n"                                                                                          \
    "float fbm(vec2 p) {\n"                                                                        \
    "    float s = 0.0, a = 0.5;\n"                                                                \
    "    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; }\n"     \
    "    return s;\n"                                                                              \
    "}\n"                                                                                          \
    "/* cells round points that wander with t: x the distance to the nearest,\n"                  \
    "   y the gap to the next (0 on a wall), z the nearest one's own number;\n"                    \
    "   the rows wrap after period (0: never) */\n"                                                \
    "vec3 cells(vec2 x, float period, float t) {\n"                                                \
    "    vec2 n = floor(x), f = fract(x);\n"                                                       \
    "    float d1 = 8.0, d2 = 8.0, id = 0.0;\n"                                                    \
    "    for (int j = -1; j <= 1; j++)\n"                                                          \
    "        for (int i = -1; i <= 1; i++) {\n"                                                    \
    "            vec2 g = vec2(float(i), float(j));\n"                                             \
    "            vec2 c = n + g;\n"                                                                \
    "            if (period > 0.0) c.y = mod(c.y, period);\n"                                      \
    "            vec2 h = hash2(c);\n"                                                             \
    "            vec2 r = g + 0.5 + 0.42 * sin(t + 6.2831 * h) - f;\n"                             \
    "            float d = dot(r, r);\n"                                                           \
    "            if (d < d1) { d2 = d1; d1 = d; id = h.x; } else if (d < d2) d2 = d;\n"            \
    "        }\n"                                                                                  \
    "    d1 = sqrt(d1);\n"                                                                         \
    "    return vec3(d1, sqrt(d2) - d1, id);\n"                                                    \
    "}\n"                                                                                          \
    "/* the water's own colour at height z: the floor's to the surface's */\n"                     \
    "vec3 water_at(float z) {\n"                                                                   \
    "    return mix(uDeep, uFog, smoothstep(0.0, 1.0, clamp(z / max(uBox.z, 1.0), 0.0, 1.0)));\n"  \
    "}\n"                                                                                          \
    "/* seen through the water from the eye */\n"                                                 \
    "vec3 through_water(vec3 c, vec3 p) {\n"                                                       \
    "    float d = length(uEye - p);\n"                                                            \
    "    float f = clamp((d - uFogNear) / max(uFogFar - uFogNear, 1.0), 0.0, 1.0);\n"              \
    "    return mix(c, water_at(p.z), f * 0.8);\n"                                                 \
    "}\n"                                                                                          \
    "/* the lighting's tonemap, for what skips it (drawn straight to the screen) */\n"             \
    "vec3 finish(vec3 c) {\n"                                                                      \
    "    if (uDirect == 0) return c;\n"                                                            \
    "    c *= uExposure;\n"                                                                        \
    "    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);\n"      \
    "}\n"

#endif
