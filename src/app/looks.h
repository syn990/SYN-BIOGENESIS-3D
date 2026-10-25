/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/looks.h, the SYN edition of its C
 * port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* The looks, SYN-BIOGENESIS's four drawn in 3D: Classic (0.9's colours on
 * the theme's water, every body plain lines, the fastest), Biological
 * (living colours and living tissue in
 * green-blue water: a sediment floor under rippling caustics, shafts of
 * light, motes drifting down), Galaxy (glowing bodies among stars) and
 * Digital (SYN-OS's colours in a box of grids). The last three draw bodies
 * as flesh, each colour its tissue, lines when far off. Every look keeps
 * the colours' meaning: green still photosynthesises. The choice is kept
 * with the theme. */
#ifndef APP_LOOKS_H
#define APP_LOOKS_H

#include <stdbool.h>

#include "ui/theme.h"

typedef enum { LOOK_CLASSIC, LOOK_BIOLOGICAL, LOOK_GALAXY, LOOK_DIGITAL, LOOK_COUNT } Look;

Look look_current(void);
void look_set(Look l);
/* The choice as last kept (at start). */
void look_load(void);
const char *look_name(Look l);

/* A segment of this colour, 0..1 (0.9's own in Classic). */
void look_rgb(Look l, int bio_color, float out[3]);

typedef struct {
    float water[3];   /* what is behind, and the fog: the water at the surface */
    float deep[3];    /* the water at the floor */
    float glow;       /* every segment's own, before flashes */
    float bloom;      /* times the lighting's bloom */
    float grid, edge; /* the floor grid's and the box edges' opacity */
    bool stars;       /* a starfield around the box */
    bool specks;      /* motes drifting down through the water */
    bool walls;       /* grids on the walls too */
    float line[3];    /* the grids' colour, and the edges' */
    float edge_rgb[3];
    /* how bodies are drawn (render/bodies.h) */
    bool lines;   /* as plain lines, nothing moving (Classic): fast */
    float tissue; /* 0 plain flesh .. 1 every tissue's pattern */
    float sss;    /* light through thin flesh */
    /* the water drawn (render/water.h) */
    bool gradient;  /* the water all round, darker below, bright toward the sun */
    bool floor;     /* sediment on the floor, and shadows on it */
    bool surface;   /* the surface, rippling */
    float caustics; /* the surface's light on bodies and the floor */
    float shafts;   /* shafts of light from the surface */
    float sediment[2][3];
} LookScene;

void look_scene(Look l, const Theme *t, LookScene *s);

#endif
