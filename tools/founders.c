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

/* tools/founders FROM TO UNITS: for each seed FROM..TO-1 (the default
 * parameters), its founders' make-up, and which founder's bloodline leads
 * the world at time UNITS (the most living), or is left alone first. The
 * data the telescope's fitness is fitted on (tools/crack-fit.py,
 * docs/crack). CSV on stdout, a row per founder:
 * seed,founder,lead,alone,t_alone,its_living,living,founders,t_end,
 * ngenes,arms,mirror,bent,green,red,cyan,blue,white,gray,yellow (each
 * colour's length in all arms),mass,reproduce_energy,z,light,
 * drive_light,drive_carbon,drive_plants,drive_hunters,drive_dead,drive_crowd,
 * purple,height,plan (0 disk, 1 cone, 2 orb),cone
 * (alone, t_alone: -1 for none) */
#include <stdio.h>
#include <stdlib.h>
#include "sim/genetic_code.h"
#include "sim/jrandom.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/world.h"
int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: tools/founders FROM TO UNITS\n");
        return 2;
    }
    long from = atol(argv[1]), to = atol(argv[2]);
    int units = atoi(argv[3]);
    params_set_defaults(&P);
    for (long seed = from; seed < to; seed++) {
        World w; world_init(&w);
        jr_set_seed(&g_rng, seed);
        world_genesis(&w);
        int nf = w.norgs;
        double feat[64][32];
        for (int i = 0; i < nf && i < 64; i++) {
            Organism *o = w.orgs[i];
            o->gc->lineage = (uint32_t)i + 1;
            double len[COL_COUNT] = {0}; int bent = 0;
            for (int j = 0; j < o->gc->ngenes; j++) {
                len[o->gc->genes[j].color] += o->gc->genes[j].length * o->gc->symmetry;
                bent += o->gc->genes[j].pitch != 0;
            }
            double *f = feat[i]; int k = 0;
            f[k++] = o->gc->ngenes; f[k++] = o->gc->symmetry; f[k++] = o->gc->mirror; f[k++] = bent;
            f[k++] = len[COL_GREEN]; f[k++] = len[COL_RED]; f[k++] = len[COL_CYAN]; f[k++] = len[COL_BLUE];
            f[k++] = len[COL_WHITE]; f[k++] = len[COL_GRAY]; f[k++] = len[COL_YELLOW];
            f[k++] = o->mass; f[k++] = o->gc->reproduce_energy; f[k++] = o->pos.z;
            f[k++] = world_light_at(&w, o->pos);
            for (int d = 0; d < DRIVE_COUNT; d++) f[k++] = o->gc->drive[d];
            f[k++] = len[COL_PURPLE]; f[k++] = o->gc->height; f[k++] = o->gc->plan; f[k++] = o->gc->cone;
        }
        int winner = -1, t_lone = -1, lead = -1, alive_at_end[64] = {0}, pop_end = 0;
        for (long fr = 0; fr < (long)units * 256; fr++) {
            world_time(&w);
            if (w.frame % 256) continue;
            uint32_t line = 0; int more = 0;
            for (int i = 0; i < w.norgs && !more; i++) {
                Organism *q = w.orgs[i];
                if (q->removed || !q->alive) continue;
                if (!line) line = q->gc->lineage; else more = q->gc->lineage != line;
            }
            if (!line) { winner = -2; t_lone = (int)(w.frame / 256); break; }
            if (!more && t_lone < 0) { winner = (int)line - 1; t_lone = (int)(w.frame / 256); break; }
        }
        for (int i = 0; i < w.norgs; i++) {
            Organism *q = w.orgs[i];
            if (q->removed || !q->alive) continue;
            pop_end++;
            if (q->gc->lineage >= 1 && q->gc->lineage <= 64) alive_at_end[q->gc->lineage - 1]++;
        }
        for (int i = 0; i < nf && i < 64; i++)
            if (alive_at_end[i] && (lead < 0 || alive_at_end[i] > alive_at_end[lead])) lead = i;
        for (int i = 0; i < nf && i < 64; i++) {
            printf("%ld,%d,%d,%d,%d,%d,%d,%d,%ld", seed, i, lead, winner, t_lone, alive_at_end[i], pop_end, nf, (long)(w.frame/256));
            for (int k = 0; k < 25; k++) printf(",%.6g", feat[i][k]);
            printf("\n");
        }
        fflush(stdout);
        world_free(&w);
    }
    return 0;
}
