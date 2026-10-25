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

/* What every 3D pass draws from: the camera, the light, the water, and the
 * look's way of drawing (app/looks.c). */
#ifndef RENDER_SCENE_H
#define RENDER_SCENE_H

typedef struct {
    float view_proj[16];     /* column-major */
    float inv_view_proj[16]; /* clip space back to the world */
    float eye[3];
    float right[3], up[3]; /* the camera's, for what faces it */
    float light_dir[3];    /* toward the light, unit */
    float fog_rgb[3];      /* the water at the surface: the fog up there */
    float deep_rgb[3];     /* the water at the floor */
    float fog_near, fog_far;
    float depth_step; /* world units a body may be nudged toward the eye */
    float box[3];     /* the world: width, height, depth */
    float time;       /* seconds, for what moves on its own */
    float light_depth; /* the world's: light falls to 1/e over this */
    float px_scale;    /* pixels a unit spans one unit from the eye (0: unknown) */
    /* the look's */
    float tissue;   /* 0 plain flesh .. 1 every tissue's pattern */
    float sss;      /* light through thin flesh */
    float caustics; /* the surface's light, rippling on bodies and the floor */
    unsigned caustic_tex; /* its pattern this frame (render/water.c), 0: none */
    float caustic_tile;   /* world units the pattern repeats over */
    int lines;      /* every body as plain lines, still (Classic) */
    float line_radius; /* their thickness then (0: the flesh's) */
    int direct;     /* drawn straight to the screen, not into the HDR target */
} SceneView;

#endif
