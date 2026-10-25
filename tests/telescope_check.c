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

/* The dice telescope's checks (syn/telescope.h, syn/crack.h), on worlds
 * whose seeds are known:
 *   - jumping n draws and back is no jump, and tel_draws counts them;
 *   - every gene length and turn a founder has cracks back to the state
 *     that drew it;
 *   - world_genesis made a founder at a time (crack_genesis_*) is the same
 *     world;
 *   - every founder of every seed tried is found as its own genesis
 *     ancestor, begun where genesis began it, and that seed is in its
 *     basin with the founder's slot;
 *   - a mutated child's ancestor is still found, with its changes. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/genetic_code.h"
#include "sim/jrandom.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/world.h"
#include "syn/crack.h"
#include "syn/telescope.h"

static int failures;

#define CHECK(c, ...)                                                                             \
    do {                                                                                          \
        if (!(c)) {                                                                               \
            printf("FAIL: " __VA_ARGS__);                                                         \
            printf("\n");                                                                         \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

int main(void)
{
    params_set_defaults(&P);
    /* a smaller world than the default, for speed: 12 founders */
    P.initial_organisms = 12;

    /* the algebra */
    uint64_t s = 0x123456789abcULL;
    for (int64_t n = -5000; n <= 5000; n += 997) {
        uint64_t t = tel_jump(s, n);
        CHECK(tel_jump(t, -n) == s, "jump %" PRId64 " and back", n);
        if (n >= 0)
            CHECK(tel_draws(s, t) == (uint64_t)n, "draws over %" PRId64, n);
    }
    JRandom r;
    r.seed = s;
    for (int i = 0; i < 100; i++)
        jr_next_int(&r, 8);
    CHECK(r.seed == tel_jump(s, 100), "the generator's own steps");
    CHECK(tel_state_seed(tel_seed_state(12345)) == 12345, "seed and state");

    int64_t seeds[] = {7, 2026, 99991};
    int found_all = 0, tried = 0;
    for (size_t si = 0; si < sizeof seeds / sizeof *seeds; si++) {
        /* genesis a founder at a time, with each founder's start */
        World a, b;
        world_init(&a);
        world_init(&b);
        jr_set_seed(&g_rng, seeds[si]);
        uint64_t s0 = g_rng.seed;
        uint64_t start[64];
        int nstart = 0, made = 0;
        crack_genesis_begin(&a);
        do {
            start[nstart++] = g_rng.seed;
        } while (crack_genesis_next(&a, &made) && nstart < 64);
        uint64_t ha = world_hash(&a);
        jr_set_seed(&g_rng, seeds[si]);
        world_genesis(&b);
        CHECK(ha == world_hash(&b), "seed %" PRId64 ": genesis a founder at a time differs", seeds[si]);
        CHECK(a.norgs == b.norgs, "seed %" PRId64 ": founders", seeds[si]);

        /* each founder that found a place, found again from its genes */
        for (int i = 0; i < b.norgs; i++) {
            const GeneticCode *gc = b.orgs[i]->gc;
            for (int j = 0; j < gc->ngenes; j++) {
                uint64_t st[4];
                int k = tel_crack_length(gc->genes[j].length, st, 4);
                CHECK(k >= 1, "seed %" PRId64 " founder %d gene %d: its length cracks", seeds[si], i, j);
                k = tel_crack_theta(gc->genes[j].theta, st, 4);
                CHECK(k >= 1, "seed %" PRId64 " founder %d gene %d: its turn cracks", seeds[si], i, j);
            }
            CrackAncestor anc[8];
            int na = crack_ancestors(gc, anc, 8);
            tried++;
            /* which of genesis's founders it was: its slot */
            int slot = -1;
            for (int k = 0; k < nstart && slot < 0; k++) {
                GeneticCode *f = crack_founder_at(start[k]);
                if (crack_same(f, gc))
                    slot = k;
                gc_unref(f);
            }
            CHECK(slot >= 0, "seed %" PRId64 " founder %d: drawn again from its start", seeds[si], i);
            bool own = na > 0 && slot >= 0 && anc[0].start == start[slot] && anc[0].changes == 0;
            /* a one-gene founder with no turn or drive to prove it can't
             * be told from a stranger; every founder has six drives */
            CHECK(own, "seed %" PRId64 " founder %d: its own genesis ancestor (%d found)", seeds[si], i, na);
            if (own) {
                CrackSeed bs[256];
                int nb = crack_basin(anc[0].start, 0, bs, 256);
                bool in = false;
                for (int c = 0; c < nb && c < 256; c++)
                    if (bs[c].state == s0) {
                        in = true;
                        CHECK(bs[c].slot == slot, "seed %" PRId64 " founder %d: slot %d, not %d", seeds[si], i,
                              bs[c].slot, slot);
                        CHECK(bs[c].org == i, "seed %" PRId64 " founder %d: organism %d", seeds[si], i, bs[c].org);
                    }
                CHECK(in, "seed %" PRId64 " founder %d: the seed is in its basin (%d seeds)", seeds[si], i, nb);
                found_all += in;
            }
            crack_ancestors_free(anc, na);
        }

        /* a child a few mutations on still points back to the founder */
        const GeneticCode *f0 = b.orgs[0]->gc;
        double keep = P.mutation_rate;
        P.mutation_rate = 0.3;
        GeneticCode *child = gc_new_mutated(f0);
        P.mutation_rate = keep;
        CrackAncestor anc[8];
        int na = crack_ancestors(child, anc, 8);
        int expect = crack_changes(f0, child);
        bool back = false;
        for (int k = 0; k < na; k++)
            back |= crack_same(anc[k].gc, f0) && anc[k].changes == expect;
        /* unless every gene and drive it had was drawn again */
        int dist = crack_changes(f0, child);
        CHECK(back || dist >= child->ngenes * 4 + DRIVE_COUNT,
              "seed %" PRId64 ": a mutated child (%d changes) finds its founder", seeds[si], expect);
        crack_ancestors_free(anc, na);
        gc_unref(child);
        world_free(&a);
        world_free(&b);
    }
    printf("%d of %d founders found as their own ancestors with their seed in the basin\n", found_all, tried);
    if (failures) {
        printf("%d telescope check%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("telescope checks passed\n");
    return 0;
}
