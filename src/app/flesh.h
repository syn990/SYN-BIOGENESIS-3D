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

/* A body as the renderer's flesh (render/bodies.h), for the world and for
 * a code shown on its own (app/flesh.c). Only how it looks: the world's
 * segments still touch as capsules segment_radius thick. */
#ifndef APP_FLESH_H
#define APP_FLESH_H

#include <stdint.h>

#include "render/bodies.h"
#include "sim/vec3.h"

/* Instances a body of n segments takes: one each, and its cell. */
int flesh_count(int segments);

/* A body's segments, ends bs and be in the world, ngenes to an arm (arm
 * after arm), each its colour: thickest at the hub and tapering to each
 * arm's end, as thick as its segments are long (thick: the world's segment
 * radius, or 1); each a tissue; then a cell at the hub, its tissue
 * TISSUE_CORE. Colours (rgb, glow) are the caller's. Returns how many it
 * wrote (flesh_count), and in *main the colour most of the body's length
 * is. */
int flesh_build(BodyInstance *out, const V3 *bs, const V3 *be, int n, int ngenes, const uint8_t *colours,
                V3 up, float phase, double thick, int *main);

#endif
