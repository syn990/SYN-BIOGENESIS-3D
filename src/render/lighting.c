/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Taken from SYN-TREK's src/render/lighting.c, cut down to what a world
 * needs (no warp smear, no panels).
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

#include "render/lighting.h"
#include "render/shader.h"

#include <epoxy/gl.h>
#include <stdio.h>

Graphics g_graphics = {
    .ambient = 0.13f,
    .tone_map = 2,
    .gamma = 1.0f,
    .exposure = 1.05f,
    .bloom_strength = 0.55f,
    .bloom_threshold = 1.0f,
    .fxaa = 1,
};

/* --- targets ----------------------------------------------------------
 *
 * scene:  full-res RGBA16F + depth. The world draws here.
 * bloomA/B: half-res RGBA16F ping-pong for the blur.
 * ldr:    full-res RGBA8, the tonemapped frame FXAA reads from.
 *
 * All lazily (re)built when the window size changes; 0x0 until first use. */

typedef struct {
    GLuint fbo, color, depth;   /* depth is a renderbuffer, 0 where none */
    int w, h;
} Target;

static Target g_scene, g_bloom_a, g_bloom_b, g_ldr;
static GLuint g_prog_bright, g_prog_blur, g_prog_composite, g_prog_fxaa;
static GLuint g_tri_vao;        /* attribute-less fullscreen triangle */
static int g_in_scene;          /* diagnostic: begin without end */

static void target_destroy(Target *t) {
    if (t->color) glDeleteTextures(1, &t->color);
    if (t->depth) glDeleteTextures(1, &t->depth);
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    t->fbo = t->color = t->depth = 0;
    t->w = t->h = 0;
}

static int target_ensure(Target *t, int w, int h, GLenum fmt, int want_depth) {
    if (t->fbo && t->w == w && t->h == h) return 1;
    target_destroy(t);
    if (w <= 0 || h <= 0) return 0;

    glGenTextures(1, &t->color);
    glBindTexture(GL_TEXTURE_2D, t->color);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, GL_RGBA,
                 fmt == GL_RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
    if (want_depth) {
        glGenTextures(1, &t->depth);
        glBindTexture(GL_TEXTURE_2D, t->depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->depth, 0);
    }
    GLenum ok = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (ok != GL_FRAMEBUFFER_COMPLETE) {
        /* Never a dead end: a machine whose driver refuses the target just
           keeps rendering straight to the backbuffer, exactly as before
           this module existed. */
        fprintf(stderr, "lighting: framebuffer incomplete (0x%x), HDR disabled\n", ok);
        target_destroy(t);
        return 0;
    }
    t->w = w; t->h = h;
    return 1;
}

/* --- post shaders ----------------------------------------------------- */

/* Attribute-less fullscreen triangle: three vertices conjured from
   gl_VertexID, one triangle overhanging the screen so there is no seam
   down the middle like a two-triangle quad has. */
static const char *POST_VS =
"#version 330 core\n"
"void main() {\n"
"    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
"}\n";

/* Bright pass: keep only what is brighter than the threshold, softly, so
   a texel at 1.01 contributes a whisper and a star at 8 contributes a
   flood. Runs into the half-res buffer, which is also the first blur. */
static const char *BRIGHT_FS =
"#version 330 core\n"
"uniform sampler2D uScene;\n"
"uniform vec2 uInvSize;\n"
"uniform float uThreshold;\n"
"out vec4 FragColor;\n"
"void main() {\n"
"    vec2 uv = gl_FragCoord.xy * uInvSize;\n"
"    vec3 c = texture(uScene, uv).rgb;\n"
"    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
"    float keep = smoothstep(uThreshold, uThreshold + 0.6, luma);\n"
"    FragColor = vec4(c * keep, 1.0);\n"
"}\n";

/* Separable 9-tap gaussian; uDir carries both the axis and the radius
   scale, so successive passes widen the halo without more taps. */
static const char *BLUR_FS =
"#version 330 core\n"
"uniform sampler2D uSrc;\n"
"uniform vec2 uInvSize;\n"
"uniform vec2 uDir;\n"
"out vec4 FragColor;\n"
"void main() {\n"
"    vec2 uv = gl_FragCoord.xy * uInvSize;\n"
"    vec2 d = uDir * uInvSize;\n"
"    vec3 sum = texture(uSrc, uv).rgb * 0.227027;\n"
"    sum += texture(uSrc, uv + d * 1.3846).rgb * 0.316216;\n"
"    sum += texture(uSrc, uv - d * 1.3846).rgb * 0.316216;\n"
"    sum += texture(uSrc, uv + d * 3.2308).rgb * 0.070270;\n"
"    sum += texture(uSrc, uv - d * 3.2308).rgb * 0.070270;\n"
"    FragColor = vec4(sum, 1.0);\n"
"}\n";

/* The spend: scene + bloom, exposure, tonemap, gamma. uToneMap keeps the
   old modes alive as options -- 0 clamp, 1 Reinhard -- with 2 (ACES
   filmic, Narkowicz fit) as what the game now means by light. */
static const char *COMPOSITE_FS =
"#version 330 core\n"
"uniform sampler2D uScene;\n"
"uniform sampler2D uBloom;\n"
"uniform vec2 uInvSize;\n"
"uniform float uBloomStrength;\n"
"uniform float uExposure;\n"
"uniform int uToneMap;\n"
"uniform float uGamma;\n"
"out vec4 FragColor;\n"
"vec3 aces(vec3 x) {\n"
"    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
"}\n"
"void main() {\n"
"    vec2 uv = gl_FragCoord.xy * uInvSize;\n"
"    vec3 c = texture(uScene, uv).rgb;\n"
"    c += texture(uBloom, uv).rgb * uBloomStrength;\n"
"    c *= uExposure;\n"
"    if (uToneMap == 2)      c = aces(c);\n"
"    else if (uToneMap == 1) c = c / (c + vec3(1.0));\n"
"    else                    c = clamp(c, 0.0, 1.0);\n"
"    c = pow(c, vec3(1.0 / max(uGamma, 0.001)));\n"
"    FragColor = vec4(c, 1.0);\n"
"}\n";

/* FXAA -- the compact console variant. Reads the finished LDR frame,
   finds luma edges, nudges the sample along them. Not a substitute for
   real supersampling, but it takes the crawling staircase off nacelle
   pylons and ring rims for the price of one texture pass. */
static const char *FXAA_FS =
"#version 330 core\n"
"uniform sampler2D uSrc;\n"
"uniform vec2 uInvSize;\n"
"out vec4 FragColor;\n"
"float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
"void main() {\n"
"    vec2 uv = gl_FragCoord.xy * uInvSize;\n"
"    vec3 rgbM  = texture(uSrc, uv).rgb;\n"
"    float lM = luma(rgbM);\n"
"    float lNW = luma(textureOffset(uSrc, uv, ivec2(-1,  1)).rgb);\n"
"    float lNE = luma(textureOffset(uSrc, uv, ivec2( 1,  1)).rgb);\n"
"    float lSW = luma(textureOffset(uSrc, uv, ivec2(-1, -1)).rgb);\n"
"    float lSE = luma(textureOffset(uSrc, uv, ivec2( 1, -1)).rgb);\n"
"    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
"    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
"    if (lMax - lMin < max(0.0312, lMax * 0.125)) { FragColor = vec4(rgbM, 1.0); return; }\n"
"    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));\n"
"    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);\n"
"    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
"    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * uInvSize;\n"
"    vec3 rgbA = 0.5 * (texture(uSrc, uv + dir * (1.0/3.0 - 0.5)).rgb\n"
"                     + texture(uSrc, uv + dir * (2.0/3.0 - 0.5)).rgb);\n"
"    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uSrc, uv + dir * -0.5).rgb\n"
"                                   + texture(uSrc, uv + dir *  0.5).rgb);\n"
"    float lB = luma(rgbB);\n"
"    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);\n"
"}\n";

static GLuint make_prog(const char *fs_src) {
    return shader_program(POST_VS, fs_src);
}

void lighting_init(void) {
    g_prog_bright    = make_prog(BRIGHT_FS);
    g_prog_blur      = make_prog(BLUR_FS);
    g_prog_composite = make_prog(COMPOSITE_FS);
    g_prog_fxaa      = make_prog(FXAA_FS);
    glGenVertexArrays(1, &g_tri_vao);
}

void lighting_destroy(void) {
    target_destroy(&g_scene);
    target_destroy(&g_bloom_a);
    target_destroy(&g_bloom_b);
    target_destroy(&g_ldr);
    if (g_tri_vao) { glDeleteVertexArrays(1, &g_tri_vao); g_tri_vao = 0; }
    if (g_prog_bright)    glDeleteProgram(g_prog_bright);
    if (g_prog_blur)      glDeleteProgram(g_prog_blur);
    if (g_prog_composite) glDeleteProgram(g_prog_composite);
    if (g_prog_fxaa)      glDeleteProgram(g_prog_fxaa);
    g_prog_bright = g_prog_blur = g_prog_composite = g_prog_fxaa = 0;
}

/* Draw the fullscreen triangle with `prog`, sampling `tex` on unit 0 into
   whatever framebuffer/viewport is currently bound. */
static void blit_with(GLuint prog, GLuint tex, int w, int h) {
    glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog, "uInvSize"), 1.0f / (float)w, 1.0f / (float)h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(g_tri_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void lighting_scene_begin(int w, int h, float r, float g, float b) {
    if (!target_ensure(&g_scene, w, h, GL_RGBA16F, 1)) return; /* fallback: backbuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_scene.fbo);
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_in_scene = 1;
}

void lighting_scene_end(void) {
    if (!g_in_scene || !g_scene.fbo) { g_in_scene = 0; return; }
    g_in_scene = 0;
    int w = g_scene.w, h = g_scene.h;
    int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    GLuint scene_color = g_scene.color;

    /* bloom: bright-pass to half res, then three widening blur rounds */
    int have_bloom = g_graphics.bloom_strength > 0.001f
                  && target_ensure(&g_bloom_a, hw, hh, GL_RGBA16F, 0)
                  && target_ensure(&g_bloom_b, hw, hh, GL_RGBA16F, 0);
    if (have_bloom) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_a.fbo);
        glViewport(0, 0, hw, hh);
        glUseProgram(g_prog_bright);
        glUniform1f(glGetUniformLocation(g_prog_bright, "uThreshold"), g_graphics.bloom_threshold);
        glUniform1i(glGetUniformLocation(g_prog_bright, "uScene"), 0);
        blit_with(g_prog_bright, scene_color, hw, hh);

        glUseProgram(g_prog_blur);
        glUniform1i(glGetUniformLocation(g_prog_blur, "uSrc"), 0);
        GLint dir_loc = glGetUniformLocation(g_prog_blur, "uDir");
        static const float widen[3] = { 1.0f, 1.8f, 2.8f };
        for (int i = 0; i < 3; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_b.fbo);
            glUseProgram(g_prog_blur);
            glUniform2f(dir_loc, widen[i], 0.0f);
            blit_with(g_prog_blur, g_bloom_a.color, hw, hh);
            glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_a.fbo);
            glUseProgram(g_prog_blur);
            glUniform2f(dir_loc, 0.0f, widen[i]);
            blit_with(g_prog_blur, g_bloom_b.color, hw, hh);
        }
    }

    /* composite (+FXAA) onto the backbuffer */
    int use_fxaa = g_graphics.fxaa && target_ensure(&g_ldr, w, h, GL_RGBA8, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, use_fxaa ? g_ldr.fbo : 0);
    glViewport(0, 0, w, h);
    glUseProgram(g_prog_composite);
    glUniform1i(glGetUniformLocation(g_prog_composite, "uScene"), 0);
    glUniform1i(glGetUniformLocation(g_prog_composite, "uBloom"), 1);
    glUniform1f(glGetUniformLocation(g_prog_composite, "uBloomStrength"),
                have_bloom ? g_graphics.bloom_strength : 0.0f);
    glUniform1f(glGetUniformLocation(g_prog_composite, "uExposure"), g_graphics.exposure);
    glUniform1i(glGetUniformLocation(g_prog_composite, "uToneMap"), g_graphics.tone_map);
    glUniform1f(glGetUniformLocation(g_prog_composite, "uGamma"), g_graphics.gamma);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, have_bloom ? g_bloom_a.color : scene_color);
    blit_with(g_prog_composite, scene_color, w, h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    if (use_fxaa) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glUseProgram(g_prog_fxaa);
        glUniform1i(glGetUniformLocation(g_prog_fxaa, "uSrc"), 0);
        blit_with(g_prog_fxaa, g_ldr.color, w, h);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

