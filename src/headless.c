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
#include "io/worldfile.h"
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
          "  --load FILE       go on from a saved world (.b3w) instead of a new one\n"
          "  --save FILE       save the world at the end (.b3w)\n"
          "  --census FILE     with each CSV row, a row per organism into FILE: its line,\n"
          "                    place, colours and drives\n"
          "  --profile FILE    with each CSV row, the water layer by layer into FILE:\n"
          "                    its CO2, its light and the life in it\n"
          "  --hash            print the final state's fingerprint\n",
          f);
}

static void csv_header(void)
{
    puts("time,frame,population,corpses,o2,co2,births,deaths,kills,infections,bites,"
         "touches,eaten,green,red,cyan,blue,white,gray,yellow,flat,level,mean_segments,marked,"
         "line2,line3,line4,purple,ch4");
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
           ",%" PRId64 ",%" PRId64 ",%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%d,%d,%d,%d,%.2f,"
           "%.4f\n",
           w->frame / FRAMES_PER_UNIT, w->frame, w->population, world_n_corpses(w), w->o2, w->co2,
           c->births - last->births, c->deaths - last->deaths, c->kills - last->kills,
           c->infections - last->infections, c->bites - last->bites, c->touches - last->touches,
           c->eaten - last->eaten, len[COL_GREEN], len[COL_RED], len[COL_CYAN], len[COL_BLUE],
           len[COL_WHITE], len[COL_GRAY], len[COL_YELLOW], (double)flat / pop, level / pop,
           genes / pop, line[1], line[2], line[3], line[4], len[COL_PURPLE], w->ch4);
}

static void census_header(FILE *f)
{
    fputs("time,id,founder,generation,alive,x,y,z,level,energy,mass,genes,symmetry,flat,"
          "green,red,cyan,blue,white,gray,yellow,light,carbon,plants,hunters,dead,crowd,"
          "children,kills,infected,purple,height,plan\n",
          f);
}

static void census_rows(FILE *f, const World *w)
{
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        double len[COL_COUNT] = {0};
        for (int k = 0; k < o->gc->ngenes; k++)
            len[o->gc->genes[k].color] += o->gc->genes[k].length * o->gc->symmetry;
        const double *d = o->gc->drive;
        fprintf(f,
                "%" PRId64 ",%d,%d,%d,%d,%.1f,%.1f,%.1f,%.3f,%.2f,%.1f,%d,%d,%d,%.1f,%.1f,%.1f,"
                "%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%.1f,%.3f,%s\n",
                w->frame / FRAMES_PER_UNIT, o->id, o->founder, o->generation, o->alive, o->pos.x,
                o->pos.y, o->pos.z, org_levelness(o), o->energy, o->mass, o->gc->ngenes,
                o->gc->symmetry, gc_is_flat(o->gc), len[COL_GREEN], len[COL_RED], len[COL_CYAN],
                len[COL_BLUE], len[COL_WHITE], len[COL_GRAY], len[COL_YELLOW], d[DRIVE_LIGHT],
                d[DRIVE_CARBON], d[DRIVE_PLANTS], d[DRIVE_HUNTERS], d[DRIVE_DEAD], d[DRIVE_CROWD],
                o->total_children, o->total_kills, o->total_infected, len[COL_PURPLE], o->gc->height,
                gc_plan_name(o->gc->plan));
    }
}

/* The water layer by layer from the surface down: its CO2, its light and
 * the living length in it. */
static void profile_header(FILE *f)
{
    fputs("time,layer,depth,co2,light,green,hunters,other,dead\n", f);
}

static void profile_rows(FILE *f, const World *w)
{
    int layer = w->fx * w->fy;
    for (int z = w->fz - 1; z >= 0; z--) {
        double co2 = 0, light = 0, len[4] = {0};
        for (int c = 0; c < layer; c++) {
            co2 += w->co2_cell[z * layer + c];
            light += w->light_cell[z * layer + c];
        }
        double mid = (z + 0.5) * w->fsz;
        double water = P.light_depth > 0 ? exp(-(w->depth - mid) / P.light_depth) : 1;
        for (int i = 0; i < w->norgs; i++) {
            const Organism *o = w->orgs[i];
            for (int s = 0; s < o->segments; s++) {
                double mz = (o->ws[s].z + o->we[s].z) / 2;
                int lz = (int)floor(mz / w->fsz);
                if (lz < 0)
                    lz = 0;
                if (lz >= w->fz)
                    lz = w->fz - 1;
                if (lz != z)
                    continue;
                int c = o->seg_color[s];
                int k = !o->alive ? 3 : c == COL_GREEN ? 0
                        : (c == COL_RED || c == COL_GRAY || c == COL_WHITE) ? 1 : 2;
                len[k] += o->m[s];
            }
        }
        fprintf(f, "%" PRId64 ",%d,%.1f,%.2f,%.4f,%.1f,%.1f,%.1f,%.1f\n", w->frame / FRAMES_PER_UNIT,
                w->fz - 1 - z, w->depth - mid, co2 / (layer * world_cell_volume(w)) * 1e6,
                light / layer * water, len[0], len[1], len[2], len[3]);
    }
}

static bool check_books(const World *w, double carbon)
{
    double bodies = world_energy_in_bodies(w);
    double tol = 1e-6 * (1 + carbon);
    double water = 0;
    for (int i = 0; i < world_n_cells(w); i++)
        water += w->co2_cell[i];
    if (fabs(bodies + w->lost - w->o2) > tol || fabs(w->o2 + w->co2 - carbon) > tol ||
        fabs(water - w->co2) > tol) {
        fprintf(stderr,
                "frame %" PRId64 ": the books do not add up: bodies %.9f + lost %.9f vs O2 %.9f; "
                "O2 + CO2 = %.9f vs %.9f; the water's cells hold %.9f of CO2 %.9f\n",
                w->frame, bodies, w->lost, w->o2, w->o2 + w->co2, carbon, water, w->co2);
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
    const char *load = NULL, *save = NULL, *census_path = NULL, *profile_path = NULL;
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
        else if (strcmp(a, "--load") == 0 && more)
            load = argv[++i];
        else if (strcmp(a, "--save") == 0 && more)
            save = argv[++i];
        else if (strcmp(a, "--census") == 0 && more)
            census_path = argv[++i];
        else if (strcmp(a, "--profile") == 0 && more)
            profile_path = argv[++i];
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
    if (load) {
        world_init(&w);
        if (!worldfile_load(&w, load, err, sizeof err)) {
            fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
            return 1;
        }
        fprintf(stderr, "%s: world %d x %d x %d at frame %" PRId64 ", %d alive\n", load, w.width,
                w.height, w.depth, w.frame, w.population);
    } else {
        int imported = world_options_genesis(&wo, &w, err, sizeof err);
        if (imported < 0) {
            fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
            return 1;
        }
        fprintf(stderr, "seed %" PRId64 ", world %d x %d x %d, %d random founders, %d imported\n",
                wo.seed, w.width, w.height, w.depth, w.population - imported, imported);
    }

    FILE *census = NULL, *profile = NULL;
    if (census_path) {
        census = fopen(census_path, "w");
        if (!census) {
            fprintf(stderr, "syn-biogenesis-3d: %s: cannot write\n", census_path);
            return 1;
        }
        census_header(census);
    }
    if (profile_path) {
        profile = fopen(profile_path, "w");
        if (!profile) {
            fprintf(stderr, "syn-biogenesis-3d: %s: cannot write\n", profile_path);
            return 1;
        }
        profile_header(profile);
    }
    double carbon = w.o2 + w.co2;
    WorldCounts last = w.counts;
    csv_header();
    if (every > 0) {
        csv_row(&w, &last);
        if (census)
            census_rows(census, &w);
        if (profile)
            profile_rows(profile, &w);
        last = w.counts;
    }
    for (int64_t f = 0; f < frames; f++) {
        world_time(&w);
        if (check && !check_books(&w, carbon))
            return 1;
        if (every > 0 && w.frame % ((int64_t)every * FRAMES_PER_UNIT) == 0) {
            csv_row(&w, &last);
            if (census)
                census_rows(census, &w);
            if (profile)
                profile_rows(profile, &w);
            last = w.counts;
        }
    }
    if (every == 0 || w.frame % ((int64_t)every * FRAMES_PER_UNIT) != 0) {
        csv_row(&w, &last);
        if (census)
            census_rows(census, &w);
        if (profile)
            profile_rows(profile, &w);
    }
    if (census)
        fclose(census);
    if (profile)
        fclose(profile);
    if (hash)
        fprintf(stderr, "hash %016" PRIx64 "\n", world_hash(&w));
    if (save && !worldfile_save(&w, save, err, sizeof err)) {
        fprintf(stderr, "syn-biogenesis-3d: %s\n", err);
        world_free(&w);
        return 1;
    }
    world_free(&w);
    return 0;
}
