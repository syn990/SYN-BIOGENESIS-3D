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

/* Thin guide lines: the world's box, the floor's grid, the marker on the
 * selected organism. Blended, depth-tested, fogged like the capsules. */
#ifndef RENDER_LINES_H
#define RENDER_LINES_H

#include "render/capsules.h"

typedef struct {
    float p[3];
    float rgba[4];
} LineVertex;

void lines_init(void);
void lines_destroy(void);
/* n vertices, two per line */
void lines_draw(const LineVertex *v, int n, const SceneView *view);

#endif
