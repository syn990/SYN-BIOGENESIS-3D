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

/* The water the bodies live in, for the looks that draw it (app/looks.c):
 * its colour all round, darker below and bright toward the sun; the floor,
 * sediment with the caustics' web moving on it and each body's shadow; the
 * shafts of light falling from the surface; drifting motes (or a look's
 * stars); and the surface itself, rippling, a glint from above and a
 * bright ceiling from below. Linear radiance, as the bodies. */
#ifndef RENDER_WATER_H
#define RENDER_WATER_H

#include <stdbool.h>

#include "render/scene.h"

typedef struct {
    float x, y;   /* under the body */
    float radius; /* how wide the shadow falls */
    float dark;   /* 0..1 */
} WaterShadow;

typedef struct {
    float p[3];
    float size; /* half its width, world units */
    float rgba[4];
} WaterPoint;

void water_init(void);
void water_destroy(void);

/* Before anything is drawn into the scene (it draws into its own
 * targets): the caustics' pattern for this frame, into v (caustic_tex,
 * caustic_tile), when v wants caustics; the sediment's tile, when these
 * colours are new. */
void water_prepare(SceneView *v, const float sed_a[3], const float sed_b[3]);
/* All round, behind everything: w x h the target's size. */
void water_background(const SceneView *v, int w, int h);
/* The floor, the sediment's tile lit by light (0..1) and the caustics. */
void water_floor(const SceneView *v, float light);
void water_shadows(const SceneView *v, const WaterShadow *s, int n);
/* count shafts, strength 0..1 */
void water_shafts(const SceneView *v, int count, float strength);
/* Soft round points; fogged: seen through the water (stars are not). */
void water_points(const SceneView *v, const WaterPoint *p, int n, bool fogged);
void water_surface(const SceneView *v);

#endif
