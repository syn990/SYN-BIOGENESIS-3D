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

/* Every segment of every organism as a lit capsule, all in one instanced
 * draw: one small capsule mesh on the GPU, and per segment only its two
 * ends, radius, colour and glow. The vertex shader stretches the mesh from
 * end to end. Output is linear radiance for render/lighting.c; glow above 0
 * pushes a capsule past white so it blooms (a bite, an infection, a kill,
 * the selected organism). */
#ifndef RENDER_CAPSULES_H
#define RENDER_CAPSULES_H

typedef struct {
    float a[3], b[3]; /* the ends of the axis, world units */
    float rgb[3];     /* linear colour */
    float radius;
    float glow;
} CapsuleInstance;

typedef struct {
    float view_proj[16]; /* column-major */
    float eye[3];
    float light_dir[3]; /* toward the light, unit */
    float fog_rgb[3];
    float fog_near, fog_far;
} SceneView;

void capsules_init(void);
void capsules_destroy(void);
void capsules_draw(const CapsuleInstance *inst, int n, const SceneView *view);

#endif
