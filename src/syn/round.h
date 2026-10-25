/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/round.h, the SYN edition of its C port
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

/* The world an arena round (syn/arena.h) begins with, made the same way
 * wherever it is made: in the arena's workers, and again in the window
 * when a round is watched.
 *
 * A round's parameters are the window's with the arena's map applied: a
 * world of the map's size, with as many first organisms to its surface as
 * the default world has (they start in the light under it). Its world is
 * world_genesis's random organisms from the round's seed, fewer by the
 * fighters' share, then each fighter's copies placed as founders are, in
 * the light. */
#ifndef SYN_ROUND_H
#define SYN_ROUND_H

#include <stdint.h>

#include "sim/params.h"
#include "sim/world.h"

/* The map applied to parameters (MAP_PARAMS: none). */
void round_apply_map(Params *p, int map);
/* The parameters a round gets now: P, the map applied. */
void round_params(Params *q, int map);
/* Those parameters hashed, leaving out what only the window reads: rounds
 * are only compared, and beings only meet, under the same hash. */
uint64_t round_fingerprint(int map);

/* The round's world from P and the seed: genesis with the newcomers, then
 * after_genesis (NULL: nothing; the worker resets its history there), then
 * per copies of each of the ns codes. first[k] is fighter k's first
 * organism's id (-1: none placed), placed[k] how many were. Returns per. */
int round_begin(World *w, int64_t seed, GeneticCode *const *code, int ns, int random_pct,
                void (*after_genesis)(World *), int *first, int *placed);

#endif
