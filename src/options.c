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

#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "sim/params.h"

void world_options_init(WorldOptions *o)
{
    params_set_defaults(&P);
    o->seed = 1;
    o->nimports = 0;
    o->copies = 1;
}

bool world_option(WorldOptions *o, int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    bool more = *i + 1 < argc;
    if (strcmp(a, "--seed") == 0 && more)
        o->seed = strtoll(argv[++*i], NULL, 10);
    else if (strcmp(a, "--size") == 0 && *i + 3 < argc) {
        P.world_width = atoi(argv[++*i]);
        P.world_height = atoi(argv[++*i]);
        P.world_depth = atoi(argv[++*i]);
    } else if (strcmp(a, "--organisms") == 0 && more)
        P.initial_organisms = atoi(argv[++*i]);
    else if (strcmp(a, "--co2") == 0 && more)
        P.initial_co2 = strtod(argv[++*i], NULL);
    else if (strcmp(a, "--pitch") == 0 && more)
        P.pitch_prob = atoi(argv[++*i]);
    else if (strcmp(a, "--radius") == 0 && more)
        P.segment_radius = strtod(argv[++*i], NULL);
    else if (strcmp(a, "--import") == 0 && more) {
        const char *path = argv[++*i];
        if (o->nimports < MAX_IMPORTS)
            snprintf(o->import[o->nimports++], sizeof o->import[0], "%s", path);
    }
    else if (strcmp(a, "--copies") == 0 && more)
        o->copies = atoi(argv[++*i]);
    else
        return false;
    return true;
}

bool world_options_check(char *err, int errlen)
{
    /* a flat body is 2 radii thick and is placed a unit clear of the sides */
    double thinnest = 2 * P.segment_radius + 1;
    if (!(P.segment_radius > 0 && P.segment_radius <= 100) || P.world_width <= thinnest ||
        P.world_height <= thinnest || P.world_depth <= thinnest || P.pitch_prob < 0 ||
        P.pitch_prob > 100 || !(P.initial_co2 >= 0 && P.initial_co2 <= 1e12) ||
        P.initial_organisms < 0) {
        snprintf(err, (size_t)errlen,
                 "the radius must be above 0 (at most 100), every side of the world more than "
                 "2 radii + 1, the pitch 0 to 100, the CO2 0 or more and the organisms 0 or "
                 "more");
        return false;
    }
    return true;
}

int world_options_genesis(const WorldOptions *o, World *w, char *err, int errlen)
{
    jr_set_seed(&g_rng, o->seed);
    world_free(w);
    world_init(w);
    world_genesis(w);
    int placed = 0;
    for (int i = 0; i < o->nimports; i++) {
        GeneticCode *gc = bgg_load(o->import[i], err, errlen);
        if (!gc)
            return -1;
        gc->lineage = (uint32_t)(i + 1);
        placed += world_add_founders(w, gc, o->copies);
        gc_unref(gc);
    }
    return placed;
}

void world_options_import_only(WorldOptions *o, const char *path)
{
    o->nimports = 0;
    if (path)
        snprintf(o->import[o->nimports++], sizeof o->import[0], "%s", path);
}
