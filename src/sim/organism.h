/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from Organism.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/organism.h.
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

/* An organism: a body of coloured segments that moves, eats, breathes,
 * grows, collides and reproduces, grown from 0.9's rules into a box of
 * water.
 *
 * What changed from 0.9 and why is in docs/DESIGN.md. In short: segments
 * are capsules (lines of thickness 2 * segment_radius) because two lines in
 * space practically never cross; the body is a rigid body with an
 * orientation, a velocity, a spin vector and an inertia tensor; a leaf
 * catches the light it lies across, at its depth and in the shade above it;
 * every breath goes into the water where it is taken; the dead sink; and an
 * organism senses what is around it and its cilia push it along its drives.
 * Eating, infection, killing, ageing and reproduction are 0.9's.
 */
#ifndef SIM_ORGANISM_H
#define SIM_ORGANISM_H

#include <stdbool.h>
#include <stdint.h>

#include "sim/genetic_code.h"
#include "sim/vec3.h"

struct World;

#define FLASH_NONE 0xff

typedef struct Organism {
    GeneticCode *gc;
    GeneticCode *infected_gc; /* the code its next child will carry, if infected */
    int n_children;
    struct World *world;
    int parent_id;
    int id;
    int founder;          /* the id of the founder whose line this is */
    int infected_founder; /* the line of infected_gc */
    int generation;
    int infected_generation;
    int total_children;
    int total_kills;
    int total_infected;

    int segments;
    V3 *bs, *be;        /* each segment's ends in the body frame, about the centre of mass */
    V3 *ws, *we;        /* the same ends in the world */
    double *m;          /* segment lengths */
    uint8_t *seg_color; /* BioColor; all brown once dead */

    V3 pos; /* centre of mass */
    Quat q; /* orientation: body frame to world */
    V3 v;   /* velocity, per frame */
    V3 w;   /* spin: axis times angle per frame, world frame */
    V3 prev_pos; /* where this frame's move started */
    Quat prev_q;
    double mass;
    M3 inertia, inv_inertia; /* body frame */
    double inertia_trace;
    double radius; /* centre of mass to the farthest capsule surface */
    double green_frac, hunter_frac; /* shares of the body's length: green; red, gray and white */
    V3 aim; /* the way its drives pull it, and how hard: at most 1 */
    double lit; /* the light on its centre this frame */
    V3 lo, hi;     /* world bounding box of the capsules */
    int growth_ratio; /* 16 = newborn ... 1 = adult */
    double energy;
    int age;
    uint8_t flash_color;
    int frames_color;
    int time_to_reproduce;
    int has_grown; /* this frame: 1 grew, -1 shrank, 0 neither */
    bool has_moved;
    bool alive;
    bool removed; /* out of energy this frame; freed when the frame ends */
} Organism;

/* new Organism(): no body yet. Draws one random turn, as 0.9 drew theta. */
Organism *org_new(struct World *w);
/* An organism carrying gc, its segments' arrays made and coloured, without
 * a draw from the dice: what a loaded world is rebuilt from. */
Organism *org_new_bare(struct World *w, GeneticCode *gc);
void org_free(Organism *o);

/* A random founder (World.genesis): code from the dice, energy from CO2. */
bool org_random_create(Organism *o);
/* A founder with a given code, placed at random (an imported being). */
bool org_place_with_code(Organism *o, GeneticCode *gc);
/* A founder with a given code with its centre of mass at p (a code pasted
 * where it was asked for), its energy from the water's CO2; false if it
 * doesn't fit there or something is in the way. No dice are drawn but
 * org_new's turn. */
bool org_place_at(Organism *o, GeneticCode *gc, V3 p);
/* A child of parent: mutated code, placed beside it. */
bool org_inherit(Organism *o, Organism *parent, bool first);
/* Its children now, as when it has the energy for them (Force
 * reproduction). */
void org_reproduce(Organism *o);
/* A corpse alive again, with its genes' colours, young (0.9's Revive). */
void org_revive(Organism *o);

/* The body a code grows, segment by segment (ngenes x symmetry of them),
 * from the arms' shared root, with its gene lengths divided by scale (1 for
 * an adult). Pure: no dice, no organism; what a preview draws. */
void gc_body_shape(const GeneticCode *gc, double scale, V3 *bs, V3 *be);
/* Build the body from the code at the current growth stage. */
void org_build_body(Organism *o);
/* Move the body's segments and bounding box to pos and q. */
void org_update_geometry(Organism *o);

/* One frame of this organism's life. False when it has run out of energy. */
bool org_move(Organism *o);
bool org_use_energy(Organism *o, double q);
void org_die(Organism *o, Organism *killer);
/* Touch test with another organism; on contact applies the colours'
 * effects and the collision impulse and returns true. */
bool org_contact(Organism *o, Organism *other);

/* How level the body lies: |the body's up axis . world up|, 1 = flat on
 * the floor's plane, 0 = standing on its edge. */
double org_levelness(const Organism *o);

/* An organism looks around every this many frames. */
#define THINK_EVERY 8
/* Where its drives pull it now, from what it senses: aim. */
void org_sense(Organism *o);


#endif
