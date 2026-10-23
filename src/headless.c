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

/* The headless world (--headless): run a seed for so many frames and
 * report it as CSV. This is the simulation on its own, as the sweeps and
 * tests drive it; the window (src/app) only ever reads the same World. */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "headless.h"
#include "options.h"
#include "sim/params.h"
#include "sim/world.h"

#define FRAMES_PER_UNIT 256

static void usage(FILE *f)
{
    fputs("usage: syn-biogenesis-3d --headless [options]\n" WORLD_OPTIONS_HELP
          "  --frames N        frames to run (default 25600: 100 time units)\n"
          "  --every N         a CSV row every N time units (default 0: the end only)\n"
          "  --check           check the carbon and energy books every frame\n"
          "  --hash            print the final state's fingerprint\n",
          f);
}

static void csv_header(void)
{
    puts("time,frame,population,corpses,o2,co2,births,deaths,kills,infections,bites,"
         "touches,eaten,green,red,cyan,blue,white,gray,yellow,flat,level,mean_segments,marked,"
         "line2,line3,line4");
}

static void csv_row(const World *w, const WorldCounts *last)
{
    double len[COL_COUNT] = {0};
    int flat = 0, line[5] = {0};
    double level = 0, genes = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (!o->alive)
            continue;
        for (int s = 0; s < o->segments; s++)
            len[o->seg_color[s]] += o->m[s];
        flat += gc_is_flat(o->gc);
        level += org_levelness(o);
        genes += o->gc->ngenes * o->gc->symmetry;
        if (o->gc->lineage >= 1 && o->gc->lineage <= 4)
            line[o->gc->lineage]++;
    }
    int pop = w->population > 0 ? w->population : 1;
    const WorldCounts *c = &w->counts;
    printf("%" PRId64 ",%" PRId64 ",%d,%d,%.4f,%.4f,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
           ",%" PRId64 ",%" PRId64 ",%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%d,%d,%d,%d\n",
           w->frame / FRAMES_PER_UNIT, w->frame, w->population, world_n_corpses(w), w->o2, w->co2,
           c->births - last->births, c->deaths - last->deaths, c->kills - last->kills,
           c->infections - last->infections, c->bites - last->bites, c->touches - last->touches,
           c->eaten - last->eaten, len[COL_GREEN], len[COL_RED], len[COL_CYAN], len[COL_BLUE],
           len[COL_WHITE], len[COL_GRAY], len[COL_YELLOW], (double)flat / pop, level / pop,
           genes / pop, line[1], line[2], line[3], line[4]);
}

static bool check_books(const World *w, double carbon)
{
    double bodies = world_energy_in_bodies(w);
    double tol = 1e-6 * (1 + carbon);
    if (fabs(bodies + w->lost - w->o2) > tol || fabs(w->o2 + w->co2 - carbon) > tol) {
        fprintf(stderr,
                "frame %" PRId64 ": the books do not add up: bodies %.9f + lost %.9f vs O2 %.9f; "
                "O2 + CO2 = %.9f vs %.9f\n",
                w->frame, bodies, w->lost, w->o2, w->o2 + w->co2, carbon);
        return false;
    }
    return true;
}

int headless_main(int argc, char **argv)
{
    WorldOptions wo;
    world_options_init(&wo);
    int64_t frames = 25600;
    int every = 0;
    bool check = false, hash = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (world_option(&wo, argc, argv, &i))
            continue;
        if (strcmp(a, "--frames") == 0 && more)
            frames = strtoll(argv[++i], NULL, 10);
        else if (strcmp(a, "--every") == 0 && more)
            every = atoi(argv[++i]);
        else if (strcmp(a, "--check") == 0)
            check = true;
        else if (strcmp(a, "--hash") == 0)
            hash = true;
        else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "syn-biogenesis-3d: unknown option %s\n", a);
            usage(stderr);
            return 2;
        }
    }
    char err[512];
    if (!world_options_check(err, sizeof err)) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return 2;
    }
    World w;
    memset(&w, 0, sizeof w);
    int imported = world_options_genesis(&wo, &w, err, sizeof err);
    if (imported < 0) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        return 1;
    }
    fprintf(stderr, "seed %" PRId64 ", world %d x %d x %d, %d random founders, %d imported\n",
            wo.seed, w.width, w.height, w.depth, w.population - imported, imported);

    double carbon = w.o2 + w.co2;
    WorldCounts last = w.counts;
    csv_header();
    if (every > 0) {
        csv_row(&w, &last);
        last = w.counts;
    }
    for (int64_t f = 0; f < frames; f++) {
        world_time(&w);
        if (check && !check_books(&w, carbon))
            return 1;
        if (every > 0 && w.frame % ((int64_t)every * FRAMES_PER_UNIT) == 0) {
            csv_row(&w, &last);
            last = w.counts;
        }
    }
    if (every == 0 || w.frame % ((int64_t)every * FRAMES_PER_UNIT) != 0)
        csv_row(&w, &last);
    if (hash)
        fprintf(stderr, "hash %016" PRIx64 "\n", world_hash(&w));
    world_free(&w);
    return 0;
}
