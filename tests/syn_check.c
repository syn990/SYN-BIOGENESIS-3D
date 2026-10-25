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

/* The history and the family tree (src/syn) only read the world, as
 * SYN-BIOGENESIS's tests check of its own:
 *   - a world recorded frame by frame is the world left alone, hash for
 *     hash;
 *   - a world saved with its history beside it and opened again goes on
 *     as the one left running, and so does its history: the same samples,
 *     the same species and names;
 *   - the tree's books balance: every living organism in a species, their
 *     counts the population. */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/worldfile.h"
#include "sim/jrandom.h"
#include "sim/params.h"
#include "sim/world.h"
#include "syn/family.h"
#include "syn/history.h"

static int fails;

static void make(World *w, int64_t seed)
{
    params_set_defaults(&P);
    jr_set_seed(&g_rng, seed);
    world_init(w);
    world_genesis(w);
}

static void check_books(const World *w, const char *when)
{
    int in = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (o->alive && !o->removed && syn_family_of(o))
            in++;
    }
    int counted = 0;
    for (int i = 0; i < syn_family_count(); i++)
        counted += syn_family_at(i)->alive;
    if (in != w->population || counted != w->population) {
        printf("FAIL %s: %d living, %d in species, species count %d\n", when, w->population, in, counted);
        fails++;
    }
}

int main(void)
{
    const int64_t seed = 11;
    const int frames = 256 * 24, more = 256 * 12;
    char dir[] = "/tmp/syn-check-XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }
    char path[256];
    snprintf(path, sizeof path, "%s/w.b3w", dir);

    /* left alone */
    World plain;
    make(&plain, seed);
    for (int f = 0; f < frames + more; f++)
        world_time(&plain);
    uint64_t want = world_hash(&plain);

    /* recorded, saved half way and opened again */
    World rec;
    make(&rec, seed);
    syn_history_reset(&rec, false);
    for (int f = 0; f < frames; f++) {
        world_time(&rec);
        syn_history_frame(&rec);
    }
    check_books(&rec, "recorded");
    char err[256];
    uint8_t *hist = NULL, *fam = NULL;
    size_t hn = syn_history_save(&hist), fn = syn_family_save(&rec, &fam);
    int samples = syn_history()->n, species = syn_family_count(), named = syn_family_named();
    if (!worldfile_save(&rec, path, err, (int)sizeof err) || !hn || !fn) {
        printf("FAIL saving: %s\n", err);
        return 1;
    }
    world_free(&rec);
    syn_history_free();
    World back;
    memset(&back, 0, sizeof back);
    if (!worldfile_load(&back, path, err, (int)sizeof err)) {
        printf("FAIL opening: %s\n", err);
        return 1;
    }
    syn_history_reset(&back, true);
    if (!syn_history_load(&back, hist, hn, fam, fn, false)) {
        printf("FAIL: the history saved doesn't read back\n");
        fails++;
    } else if (syn_history()->n != samples || syn_family_count() != species || syn_family_named() != named) {
        printf("FAIL: the history read back has %d samples, %d species, %d names, not %d, %d, %d\n",
               syn_history()->n, syn_family_count(), syn_family_named(), samples, species, named);
        fails++;
    }
    for (int f = 0; f < more; f++) {
        world_time(&back);
        syn_history_frame(&back);
    }
    check_books(&back, "opened again");
    uint64_t got = world_hash(&back);
    if (got != want) {
        printf("FAIL: recorded and opened again %016" PRIx64 ", left alone %016" PRIx64 "\n", got, want);
        fails++;
    }
    printf("  history and family tree: the world unchanged (%016" PRIx64 "), %d samples and %d species "
           "(%d families named) back as saved\n",
           got, samples, species, named);
    /* bytes cut short are refused, and change nothing */
    int n_before = syn_history()->n;
    if (syn_history_load(&back, hist, hn / 2, fam, fn, false) || syn_history()->n != n_before) {
        printf("FAIL: a history cut short was read\n");
        fails++;
    }
    free(hist);
    free(fam);
    world_free(&back);
    world_free(&plain);
    syn_history_free();
    remove(path);
    rmdir(dir);
    printf(fails ? "%d failed\n" : "history checks passed\n", fails);
    return fails ? 1 : 0;
}
