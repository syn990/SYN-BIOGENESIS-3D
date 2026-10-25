/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/round.c, the SYN edition of its C port
 * of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "syn/round.h"

#include <math.h>
#include <string.h>

#include "sim/jrandom.h"
#include "sim/organism.h"
#include "syn/arena.h"

/* The maps' water: the default box, four and nine times its surface at
 * its depth, and two of docs/ecology's shapes (a wide shallow pond, a
 * cube that is mostly dark). */
static const struct {
    int w, h, d;
} k_map[MAP_COUNT] = {
    {0, 0, 0}, {600, 600, 300}, {1200, 1200, 300}, {1800, 1800, 300}, {1000, 1000, 64}, {400, 400, 400},
};

void round_apply_map(Params *p, int map)
{
    if (map <= MAP_PARAMS || map >= MAP_COUNT)
        return;
    Params d;
    memset(&d, 0, sizeof d);
    params_set_defaults(&d);
    p->world_width = k_map[map].w;
    p->world_height = k_map[map].h;
    p->world_depth = k_map[map].d;
    p->initial_organisms = (int)lround((double)d.initial_organisms * k_map[map].w * k_map[map].h /
                                       ((double)d.world_width * d.world_height));
    if (p->initial_organisms < 1)
        p->initial_organisms = 1;
}

void round_params(Params *q, int map)
{
    memcpy(q, &P, sizeof *q);
    round_apply_map(q, map);
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t round_fingerprint(int map)
{
    Params q;
    round_params(&q, map);
    /* by value, field by field: the struct's padding is no parameter */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < k_n_param_fields; i++) {
        double v = params_get(&q, &k_param_fields[i]);
        h = fnv(h, &v, sizeof v);
    }
    return h ? h : 1;
}

int round_begin(World *w, int64_t seed, GeneticCode *const *code, int ns, int random_pct,
                void (*after_genesis)(World *), int *first, int *placed)
{
    jr_set_seed(&g_rng, seed);
    /* the random organisms as world_genesis makes them, fewer by the
     * fighters' share */
    int total = P.initial_organisms, per = 0;
    if (ns > 0) {
        int nc = (int)lround(total * (100.0 - random_pct) / 100.0);
        per = nc / ns;
        if (per < 1)
            per = 1;
    }
    /* none at all for winners only, not the few left over */
    int nrandom = ns > 0 && random_pct == 0 ? 0 : total - per * ns;
    if (nrandom < 0)
        nrandom = 0;
    P.initial_organisms = nrandom;
    world_genesis(w);
    P.initial_organisms = total;
    if (after_genesis)
        after_genesis(w);

    /* then the fighters, each its own bloodline from its first copy (ids
     * are given only to those placed, in turn) */
    for (int k = 0; k < ns; k++) {
        int id = w->next_id;
        placed[k] = world_add_founders(w, code[k], per);
        first[k] = placed[k] > 0 ? id : -1;
    }
    return per;
}
