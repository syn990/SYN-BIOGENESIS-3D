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

#include "app/flesh.h"

#include <math.h>

#include "sim/genetic_code.h"

int flesh_count(int segments)
{
    return segments + 1;
}

static void put3(float out[3], V3 v)
{
    out[0] = (float)v.x;
    out[1] = (float)v.y;
    out[2] = (float)v.z;
}

int flesh_build(BodyInstance *out, const V3 *bs, const V3 *be, int n, int ngenes, const uint8_t *colours,
                V3 up, float phase, double thick, int *main)
{
    if (ngenes < 1)
        ngenes = 1;
    double total = 0, by[COL_COUNT] = {0};
    for (int s = 0; s < n; s++) {
        double len = v3_len(v3_sub(be[s], bs[s]));
        total += len;
        if (colours[s] < COL_COUNT)
            by[colours[s]] += len;
    }
    int best = n > 0 ? colours[0] : COL_GREEN;
    for (int c = 0; c < COL_COUNT; c++)
        if (by[c] > by[best])
            best = c;
    if (main)
        *main = best;
    /* as thick at the hub as a fifth of a segment's length, at least the
     * capsule the world touches with, thinning to under half at the ends */
    double mean = n > 0 ? total / n : 1;
    double hub = thick * fmin(3.2, fmax(0.9, 0.20 * mean + 0.5));
    for (int s = 0; s < n; s++) {
        BodyInstance *b = &out[s];
        int j = s % ngenes;
        put3(b->a, bs[s]);
        put3(b->b, be[s]);
        b->r0 = (float)(hub * (1 - 0.55 * j / ngenes));
        b->r1 = (float)(hub * (1 - 0.55 * (j + 1) / ngenes));
        put3(b->up, up);
        b->tissue = (float)(colours[s] < COL_COUNT ? colours[s] : TISSUE_DEAD);
        b->phase = phase;
        b->tip = j == ngenes - 1 ? 1.0f : 0.0f;
        b->glow = 0;
    }
    BodyInstance *c = &out[n];
    V3 at = n > 0 ? bs[0] : v3(0, 0, 0);
    put3(c->a, at);
    put3(c->b, at);
    c->r0 = c->r1 = (float)fmin(7 * thick, fmax(1.4 * thick, hub * 1.6 + 0.15 * thick * sqrt((double)n)));
    put3(c->up, up);
    c->tissue = TISSUE_CORE;
    c->phase = phase;
    c->tip = 0;
    c->glow = 0;
    return n + 1;
}
