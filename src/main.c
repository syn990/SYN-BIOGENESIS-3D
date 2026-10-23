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

/* The headless world: run a seed for so many frames and report it.
 *
 * There is no window yet. This is the simulation on its own, as the
 * arena, sweeps and tests drive it; the renderer comes later and will
 * only ever read the World. */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "sim/params.h"
#include "sim/world.h"

#define FRAMES_PER_UNIT 256

static void usage(FILE *f)
{
    fputs("usage: syn-biogenesis-3d [options]\n"
          "  --seed N          the world's seed (default 1)\n"
          "  --frames N        frames to run (default 25600: 100 time units)\n"
          "  --size W H D      the world box (default 1000 1000 32)\n"
          "  --organisms N     random founders (default 15)\n"
          "  --co2 X           carbon in the air at genesis (default 5000)\n"
          "  --pitch P         percent of new genes that bend out of their plane\n"
          "                    (default 50; 0 keeps every new body flat, as in 0.9)\n"
          "  --radius R        segment thickness: capsule radius (default 1)\n"
          "  --import FILE     add copies of a Biogenesis 0.9 being (.bgg), flat; the\n"
          "                    'marked' column counts its living descendants\n"
          "  --copies K        copies of the imported being (default 1)\n"
          "  --every N         a CSV row every N time units (default 0: the end only)\n"
          "  --check           check the carbon and energy books every frame\n"
          "  --hash            print the final state's fingerprint\n",
          f);
}

static void csv_header(void)
{
    puts("time,frame,population,corpses,o2,co2,births,deaths,kills,infections,bites,"
         "touches,eaten,green,red,cyan,blue,white,gray,yellow,flat,level,mean_segments,marked");
}

static void csv_row(const World *w, const WorldCounts *last)
{
    double len[COL_COUNT] = {0};
    int flat = 0, marked = 0;
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
        marked += o->gc->lineage == 1;
    }
    int pop = w->population > 0 ? w->population : 1;
    const WorldCounts *c = &w->counts;
    printf("%" PRId64 ",%" PRId64 ",%d,%d,%.4f,%.4f,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
           ",%" PRId64 ",%" PRId64 ",%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%d\n",
           w->frame / FRAMES_PER_UNIT, w->frame, w->population, world_n_corpses(w), w->o2, w->co2,
           c->births - last->births, c->deaths - last->deaths, c->kills - last->kills,
           c->infections - last->infections, c->bites - last->bites, c->touches - last->touches,
           c->eaten - last->eaten, len[COL_GREEN], len[COL_RED], len[COL_CYAN], len[COL_BLUE],
           len[COL_WHITE], len[COL_GRAY], len[COL_YELLOW], (double)flat / pop, level / pop,
           genes / pop, marked);
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

int main(int argc, char **argv)
{
    params_set_defaults(&P);
    int64_t seed = 1, frames = 25600;
    int every = 0, copies = 1;
    bool check = false, hash = false;
    const char *import = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (strcmp(a, "--seed") == 0 && more)
            seed = strtoll(argv[++i], NULL, 10);
        else if (strcmp(a, "--frames") == 0 && more)
            frames = strtoll(argv[++i], NULL, 10);
        else if (strcmp(a, "--size") == 0 && i + 3 < argc) {
            P.world_width = atoi(argv[++i]);
            P.world_height = atoi(argv[++i]);
            P.world_depth = atoi(argv[++i]);
        } else if (strcmp(a, "--organisms") == 0 && more)
            P.initial_organisms = atoi(argv[++i]);
        else if (strcmp(a, "--co2") == 0 && more)
            P.initial_co2 = strtod(argv[++i], NULL);
        else if (strcmp(a, "--pitch") == 0 && more)
            P.pitch_prob = atoi(argv[++i]);
        else if (strcmp(a, "--radius") == 0 && more)
            P.segment_radius = strtod(argv[++i], NULL);
        else if (strcmp(a, "--import") == 0 && more)
            import = argv[++i];
        else if (strcmp(a, "--copies") == 0 && more)
            copies = atoi(argv[++i]);
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
    /* a flat body is 2 radii thick and is placed a unit clear of the sides */
    double thinnest = 2 * P.segment_radius + 1;
    if (!(P.segment_radius > 0 && P.segment_radius <= 100) || P.world_width <= thinnest ||
        P.world_height <= thinnest || P.world_depth <= thinnest || P.pitch_prob < 0 ||
        P.pitch_prob > 100 || !(P.initial_co2 >= 0 && P.initial_co2 <= 1e12) ||
        P.initial_organisms < 0) {
        fputs("syn-biogenesis-3d: the radius must be above 0 (at most 100), every side of the "
              "world more than 2 radii + 1, the pitch 0 to 100, the CO2 0 or more and the "
              "organisms 0 or more\n",
              stderr);
        return 2;
    }

    jr_set_seed(&g_rng, seed);
    World w;
    world_init(&w);
    world_genesis(&w);
    int founders = w.population;
    int imported = 0;
    if (import) {
        char err[512];
        GeneticCode *gc = bgg_load(import, err, sizeof err);
        if (!gc) {
            fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
            return 1;
        }
        gc->lineage = 1;
        imported = world_add_founders(&w, gc, copies);
        gc_unref(gc);
    }
    fprintf(stderr, "seed %" PRId64 ", world %d x %d x %d, %d random founders, %d imported\n",
            seed, w.width, w.height, w.depth, founders, imported);

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
