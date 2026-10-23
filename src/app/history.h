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

/* The world's story, a sample a time unit, kept beside it for the
 * statistics screen (as SYN-BIOGENESIS's syn/history.c is). It only reads
 * the World. When the samples run past HISTORY_MAX, neighbours are merged
 * and each sample then covers twice as many units, so a long run still fits
 * and still reaches back to genesis. */
#ifndef APP_HISTORY_H
#define APP_HISTORY_H

#include <stdint.h>

#include "sim/world.h"

#define HISTORY_MAX 2048

typedef struct {
    double t; /* time units */
    double population, corpses, o2, co2;
    double births, deaths, kills, infections, bites; /* per time unit */
    double colour[COL_COUNT];                        /* living segment length */
    double flat, level;                              /* shares of the living, 0..1 */
} Sample;

typedef struct {
    Sample s[HISTORY_MAX];
    int n;
    int stride;    /* time units a sample covers */
    int pending;   /* units gathered into the next sample */
    Sample acc;    /* what they add up to so far */
    WorldCounts last;
} History;

void history_reset(History *h, const World *w);
/* After every world frame: a sample at every time unit's end. */
void history_frame(History *h, const World *w);

#endif
