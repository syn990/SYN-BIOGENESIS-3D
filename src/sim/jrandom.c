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

#include "sim/jrandom.h"

#include <time.h>

#define JR_MULT 0x5DEECE66DULL
#define JR_MASK ((1ULL << 48) - 1)

void jr_set_seed(JRandom *r, int64_t seed)
{
    r->seed = ((uint64_t)seed ^ JR_MULT) & JR_MASK;
}

void jr_seed_from_time(JRandom *r)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t s = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    /* Same uniquifier constant Java mixes into its default seed. */
    jr_set_seed(r, (int64_t)(s ^ 8682522807148012ULL * 1181783497276652981ULL));
}

static int32_t jr_next(JRandom *r, int bits)
{
    r->seed = (r->seed * JR_MULT + 0xBULL) & JR_MASK;
    return (int32_t)(uint32_t)(r->seed >> (48 - bits));
}

int32_t jr_next_int(JRandom *r, int32_t bound)
{
    int32_t rr = jr_next(r, 31);
    int32_t m = bound - 1;
    if ((bound & m) == 0)
        return (int32_t)(((int64_t)bound * (int64_t)rr) >> 31);
    /* Java's rejection loop relies on int overflow going negative. */
    for (int32_t u = rr; (int32_t)((uint32_t)u - (uint32_t)(rr = u % bound) + (uint32_t)m) < 0;
         u = jr_next(r, 31))
        ;
    return rr;
}

double jr_next_double(JRandom *r)
{
    /* Java evaluates next(26) before next(27); C would not promise that order
     * inside one expression, so it is spelled out. */
    int64_t hi = (int64_t)jr_next(r, 26) << 27;
    int64_t lo = jr_next(r, 27);
    return (double)(hi + lo) * 0x1.0p-53;
}

bool jr_next_boolean(JRandom *r)
{
    return jr_next(r, 1) != 0;
}
