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

/* Every segment of every organism as flesh, all in one instanced draw: one
 * small mesh on the GPU (rings along an axis, a round cap at each end), and
 * per segment its two ends, its thickness at each, its tissue and colour.
 * The vertex shader shapes the mesh by tissue: a leaf is a veined blade
 * that faces the body's up, a claw tapers to a hooked point at an arm's
 * end, cilia are a slender beating whip, a shield is plated, a killer's
 * segment is ridged bone, the fertile segment a sac of eggs, purple a
 * bladder of bubbles; a corpse's are shrivelled; and each body has a cell
 * at its hub, a membrane round a nucleus. The fragment shader gives each
 * its pattern, light through thin flesh, a wet shine, the caustics from
 * the surface and the water between it and the eye. What spans few
 * pixels is drawn with a coarse mesh, and what spans fewer as a line; a
 * look that wants it (Classic) has every body as lines, nothing moving.
 * Output is linear radiance for render/lighting.c (or finished, for a
 * preview drawn straight to the screen); glow above 0 pushes past white
 * so it blooms. */
#ifndef RENDER_BODIES_H
#define RENDER_BODIES_H

#include "render/scene.h"

/* The first nine in BioColor's order: a segment's tissue is its colour. */
typedef enum {
    TISSUE_LEAF,
    TISSUE_CLAW,
    TISSUE_CILIA,
    TISSUE_SHELL,
    TISSUE_SPORE,
    TISSUE_BONE,
    TISSUE_EGG,
    TISSUE_BLADDER,
    TISSUE_DEAD,
    TISSUE_CORE, /* the cell at a body's hub */
    TISSUE_COUNT
} Tissue;

typedef struct {
    float a[3], b[3]; /* the ends of the axis, world units (equal for a core) */
    float r0, r1;     /* thickness at a and at b */
    float rgb[3];     /* linear colour */
    float glow;
    float up[3];   /* the body's up: the way a leaf faces */
    float tissue;  /* Tissue */
    float phase;   /* the body's own, 0..1: where its beating is */
    float tip;     /* 1 where an arm ends */
} BodyInstance;

void bodies_init(void);
void bodies_destroy(void);
/* Each instance as fine flesh, coarse flesh or a line by how many pixels
 * it spans (all lines, no cells, when view->lines). */
void bodies_draw(const BodyInstance *inst, int n, const SceneView *view);
/* Already in order: count[0] fine, then count[1] coarse, then count[2]
 * lines. */
void bodies_draw_tiers(const BodyInstance *inst, const int count[3], const SceneView *view);

#endif
