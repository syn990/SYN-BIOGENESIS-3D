/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from World.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/world.h.
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

/* The world: a box of width x height x depth, the organisms in it, the air
 * (O2 and CO2, a closed carbon cycle as in 0.9) and the frame loop.
 *
 * Collision candidates come from a uniform grid of the organisms' centres,
 * built when each frame starts, as 0.9 built its STRtree from the
 * frame-start bounds. A child born during a frame is not in the grid until
 * the next one, as in 0.9.
 */
#ifndef SIM_WORLD_H
#define SIM_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "sim/organism.h"

/* Running totals since genesis; reports take differences. */
typedef struct {
    int64_t births, deaths, kills, infections, bites, touches;
    double eaten; /* energy moved by red segments */
} WorldCounts;

typedef struct World {
    int width, height, depth;
    Organism **orgs; /* every organism, dead ones included, in birth order */
    int norgs, cap;
    int population;
    int next_id;
    int64_t frame;
    double o2, co2;
    /* energy that left the world's books: the crumbs (< BIO_TOL) organisms
     * still held when they were removed. bodies + lost == O2, always. */
    double lost;
    WorldCounts counts;

    /* broad phase: the organisms' centres sorted into cells */
    double cell;
    int gx, gy, gz;
    int *cell_start; /* gx*gy*gz + 1 offsets into cell_items */
    int *cell_fill;  /* scratch for the sort */
    int ncells_cap;
    Organism **cell_items;
    int *item_cell;
    int items_cap;
    double reach; /* how far beyond a body's box a candidate's centre can be */
    bool grid_valid;
} World;

void world_init(World *w);
void world_free(World *w);
void world_clear(World *w);
/* World.genesis: a fresh world from the current parameters. */
void world_genesis(World *w);
/* World.time: one frame. */
void world_time(World *w);
/* Place copies of a code at random spots. Returns how many found room. */
int world_add_founders(World *w, GeneticCode *gc, int copies);

int world_new_id(World *w);
int world_n_corpses(const World *w);
/* The energy held by every body, alive and dead. */
double world_energy_in_bodies(const World *w);
/* The middle of the living, or the box's centre when nothing lives. */
V3 world_life_centre_or_box(const World *w);
/* A 64-bit fingerprint of the whole state, for determinism checks. */
uint64_t world_hash(const World *w);

void world_add_o2(World *w, double q);
void world_decrease_co2(World *w, double q);
double world_respiration(World *w, double q);
double world_photosynthesis(World *w, double q);

/* Bounding-box overlap with any other organism (placement test). */
Organism *world_fast_check_hit(World *w, Organism *o);
/* Touch test against the frame's grid; applies the effects. */
Organism *world_check_hit(World *w, Organism *o);

void world_add_organism(World *w, Organism *child, Organism *parent);
/* Append an organism as it is, counting nothing (a loaded world's). */
void world_append_raw(World *w, Organism *o);
void world_organism_has_died(World *w, Organism *dying, Organism *killer);
void world_organism_has_been_infected(World *w, Organism *infected, Organism *infector);
void world_organism_bitten(World *w, Organism *eater, Organism *eaten, double energy);

#endif
