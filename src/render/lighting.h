/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Taken from SYN-TREK's src/render/lighting.h and graphics.h, cut down to
 * what a world needs (no warp smear, no panels).
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

/* The lighting engine's frame spine: HDR in, light out (SYN-TREK's).
 *
 * Everything 3D renders into a floating-point scene buffer where 1.0 is
 * just a number, not a ceiling, so a flash of colour (a bite, an infection,
 * a kill) can be brighter than white and the difference survives to the
 * end of the frame. Then one post chain spends it: what is brighter than
 * white spills into a bloom halo, an exposure + filmic tonemap folds the
 * range onto the screen, and FXAA files the aliased edges off. Scene
 * shaders output linear radiance and no tonemap of their own. */
#ifndef RENDER_LIGHTING_H
#define RENDER_LIGHTING_H

/* The knobs, with SYN-TREK's defaults. */
typedef struct {
    float ambient;
    int tone_map;          /* 2 = ACES filmic, 1 = Reinhard, 0 = clamp */
    float gamma;
    float exposure;
    float bloom_strength;
    float bloom_threshold;
    int fxaa;
} Graphics;

extern Graphics g_graphics;

void lighting_init(void);
void lighting_destroy(void);

/* Send all rendering into the HDR scene target (made or resized to w x h)
 * and clear it to the given colour. */
void lighting_scene_begin(int w, int h, float r, float g, float b);

/* Bloom, exposure, tonemap, gamma, FXAA, onto the backbuffer. */
void lighting_scene_end(void);

#endif
