/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of a C port of Biogenesis (https://biogenesis.sourceforge.net/).
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

/* java.util.Random, bit for bit.
 *
 * Biogenesis draws every random decision (gene colours, mutations, cilia
 * kicks, birth positions) from one shared java.util.Random, and so does the
 * 3D world: the same 48-bit LCG and the same nextInt/nextDouble algorithms
 * as SYN-BIOGENESIS (src/sim/jrandom.c there), so a seed names one world
 * and every gene is a place on that seed's stream, as in 2D.
 */
#ifndef SIM_JRANDOM_H
#define SIM_JRANDOM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t seed;
} JRandom;

void jr_set_seed(JRandom *r, int64_t seed);
/* Seeds from the clock, like `new Random()`. */
void jr_seed_from_time(JRandom *r);
int32_t jr_next_int(JRandom *r, int32_t bound);
double jr_next_double(JRandom *r);
bool jr_next_boolean(JRandom *r);

#endif
