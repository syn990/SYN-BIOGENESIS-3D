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

/* An arena round made again from the record is the very world its worker
 * ran. Run after the arena without a window with SYN3D_ARENA_WORLDS=DIR
 * (tests/arena.sh): each recorded round is made again as the window makes
 * a round to be watched, without the family tree, run to where the worker
 * stopped, and its hash compared with the worker's world as it saved it.
 *
 *   tests/arena_check DIR RECORD */

#include <inttypes.h>
#include <stdio.h>

#include "io/worldfile.h"
#include "sim/world.h"
#include "syn/arena.h"
#include "syn/record.h"
#include "syn/round.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: arena_check DIR RECORD\n");
        return 2;
    }
    syn_record_set_file(argv[2]);
    int n = syn_record_count(), fails = 0;
    for (int i = 0; i < n; i++) {
        const RecordRound *r = syn_record_at(i);
        ArenaRoundSpec s;
        if (!syn_arena_record_round(i, &s)) {
            printf("FAIL round %d: the record can't make it again\n", r->cycle);
            fails++;
            continue;
        }
        P = s.params;
        World w;
        world_init(&w);
        int first[ARENA_SLOTS], placed[ARENA_SLOTS];
        round_begin(&w, s.seed, s.code, s.nf, s.wild, NULL, first, placed);
        for (int64_t f = 0; f < (int64_t)r->t_end * 256; f++)
            world_time(&w);
        uint64_t again = world_hash(&w);

        World saved;
        world_init(&saved);
        char path[1200], err[256];
        snprintf(path, sizeof path, "%s/cycle-%d.b3w", argv[1], r->cycle);
        if (!worldfile_load(&saved, path, err, (int)sizeof err)) {
            printf("FAIL round %d: %s\n", r->cycle, err);
            fails++;
        } else if (world_hash(&saved) != again) {
            printf("FAIL round %d: made again %016" PRIx64 ", the worker's %016" PRIx64 "\n", r->cycle,
                   again, world_hash(&saved));
            fails++;
        } else {
            printf("  round %d (%s, %d fighters): the same world at time %d, %d living\n", r->cycle,
                   r->level ? "a ladder round" : "a qualifier", s.nf, r->t_end, w.population);
        }
        world_free(&saved);
        world_free(&w);
        syn_arena_round_spec_free(&s);
    }
    printf(fails ? "%d of %d rounds differ\n" : "all %d rounds made again\n", fails ? fails : n, n);
    return fails ? 1 : 0;
}
