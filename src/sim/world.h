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

/* The world: a box of water, width x height x depth with the surface on
 * top (z = depth), the organisms in it, the carbon (O2 and CO2, a closed
 * cycle as in 0.9) and the frame loop.
 *
 * 0.9's CO2 was one number for the whole world. Here it is dissolved in the
 * water, cell by cell (cells of about 32 units a side), and spreads between
 * neighbouring cells. Leaves take it from the water around them, and
 * breathing, eating and decay give it back where they happen. How much
 * there is at genesis goes with the volume of the water.
 *
 * Light falls from the surface, as bright as the time of day: days of
 * day_length time units, from dark at midnight to full at noon. The water
 * dims it by e for every light_depth it goes down, and every body casts a
 * shadow on what is below it: each cell holds the shade cast in it this
 * frame, and the light that reaches a cell is what the cells above it let
 * through.
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
#include "sim/stats.h"

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
    double o2, co2; /* co2: all that is dissolved, the sum of co2_cell */
    double ch4;     /* new: the methane dissolved, the sum of ch4_cell; O2 + CO2 + CH4 is the
                       world's carbon, which never changes */
    /* energy that left the world's books: the crumbs (< BIO_TOL) organisms
     * still held when they were removed. bodies + lost == O2, always. */
    double lost;
    WorldCounts counts;
    WorldStats stats; /* 0.9's WorldStatistics: records and averages */

    /* the water, in cells of fsx x fsy x fsz: fx x fy x fz of them */
    int fx, fy, fz;
    double fsx, fsy, fsz;
    double *co2_cell;   /* CO2 dissolved in each cell */
    double *co2_flux;   /* scratch for its spreading (and the methane's) */
    double *ch4_cell;   /* methane dissolved in each cell: the dead rot into it, purple
                           lives on it, and it turns back into CO2 slowly */
    double *shade_cell; /* the shade cast in each cell this frame (optical depth) */
    double *light_cell; /* how much of the surface's light the shade above lets
                           through to each cell's middle */
    double daylight;    /* the surface's light this frame, 0 at midnight to 1 at noon */

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

    /* Called when an organism leaves the world (out of energy, or the world
     * cleared), before it is freed, so watchers can read it last and drop
     * pointers to it. No part of the simulation. */
    void (*on_remove)(void *ud, Organism *o);
    void *ud;
} World;

/* The time in time units of 256 frames, as the window and the CSV count it. */
static inline int64_t world_time_units(const World *w)
{
    return w->frame >> 8;
}

void world_init(World *w);
void world_free(World *w);
void world_clear(World *w);
/* World.genesis: a fresh world from the current parameters. */
void world_genesis(World *w);
/* What a frame starts with: the neighbours' grid, the time of day, the
 * shade and the light. */
void world_prepare(World *w);
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

/* The water's cells for the world's size, all holding density CO2 per
 * million cubic units. */
void world_reset_water(World *w, double density);
int world_n_cells(const World *w);
double world_cell_volume(const World *w);
/* The cell of the water a point is in. */
int world_cell_of(const World *w, V3 p);
/* How much light reaches p, of a noon's at the surface: the time of day,
 * the water's dimming and the shade above. */
double world_light_at(const World *w, V3 p);
/* Which way the dissolved CO2 grows richer around p (a unit vector), or 0. */
V3 world_co2_gradient(const World *w, V3 p);
/* The way to brighter water across, at p's height (out of the shade), a
 * unit vector, or 0 where it is even. */
V3 world_light_gradient(const World *w, V3 p);

void world_add_o2(World *w, double q);
/* Take CO2 from all the water alike (a founder's energy). */
void world_take_co2(World *w, double q);
/* CO2 added to the water, the same in every cell (Increase CO2; Decrease
 * takes it as world_take_co2 does, in proportion). */
void world_add_co2(World *w, double q);
/* Every living organism dies where it is; every corpse breathes out what
 * it holds (0.9's Kill all, Disperse all). */
void world_kill_all(World *w);
void world_disperse_all(World *w);
/* Breathe q of O2 out as CO2 into cell. Returns what was breathed. */
double world_respiration(World *w, int cell, double q);
/* Leaves catching `light` (length times light) in cell: the energy they
 * make from the CO2 there. */
double world_photosynthesis(World *w, int cell, double light);
/* A corpse rotting q of its energy into methane in cell (Color Mod's
 * decomposition). Returns what rotted. */
double world_decomposition(World *w, int cell, double q);
/* Purple segments of total length `length` in cell: the energy they make
 * from the methane there (Color Mod's methanotrophy). */
double world_methanotrophy(World *w, int cell, double length);

/* Bounding-box overlap with any other organism (placement test). */
Organism *world_fast_check_hit(World *w, Organism *o);
/* Touch test against the frame's grid; applies the effects. */
Organism *world_check_hit(World *w, Organism *o);
/* Call fn on every organism in the frame's grid whose centre may be within
 * r of p (the caller measures). */
void world_visit(World *w, V3 p, double r, void (*fn)(Organism *b, void *ud), void *ud);

void world_add_organism(World *w, Organism *child, Organism *parent);
/* Append an organism as it is, counting nothing (a loaded world's). */
void world_append_raw(World *w, Organism *o);
void world_organism_has_died(World *w, Organism *dying, Organism *killer);
void world_organism_has_been_infected(World *w, Organism *infected, Organism *infector);
void world_organism_bitten(World *w, Organism *eater, Organism *eaten, double energy);

#endif
