/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from GeneticCode.java and Gene.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/genetic_code.h.
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

/* Genes and genetic codes.
 *
 * A 0.9 gene is one segment: a length, a turn (theta) from the previous
 * segment's heading, and a colour. Here a gene has one more field, pitch:
 * how far the segment bends up or down out of the plane the turn happens
 * in. A gene with pitch 0 is exactly a 0.9 gene, so every 0.9 genetic code
 * is a 3D genetic code whose body happens to be flat.
 *
 * A code also carries drives, which 0.9 had none of: how strongly its
 * organism steers toward (or away from) the light, richer water, plants,
 * hunters, the dead and company. Cilia push along them (organism.c). A 0.9
 * code has every drive 0, and its cilia push as 0.9's did.
 *
 * Codes are reference counted and compared by identity, as Java compared
 * them by reference: a white segment refuses to re-infect an organism
 * already carrying this very code.
 */
#ifndef SIM_GENETIC_CODE_H
#define SIM_GENETIC_CODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COL_GREEN,  /* photosynthesis */
    COL_RED,    /* eats other organisms */
    COL_CYAN,   /* cilia: random propulsion */
    COL_BLUE,   /* shield */
    COL_WHITE,  /* infects with its own code */
    COL_GRAY,   /* kills on touch */
    COL_YELLOW, /* extra children per birth */
    COL_PURPLE, /* new, Color Mod's: lives on the methane the dead rot into, light or none */
    COL_BROWN,  /* dead */
    COL_COUNT
} BioColor;

/* How a body's arms stand. A disk is 0.9's: the arms around the body's
 * axis, in the plane the turns happen in. A cone tilts every arm out of
 * that plane by the code's cone angle, an umbrella or a jellyfish. An orb
 * spreads them over a sphere: two, up and down; four, a tetrahedron's
 * corners; six, an octahedron's; eight, a cube's. */
typedef enum { PLAN_DISK, PLAN_CONE, PLAN_ORB, PLAN_COUNT } BodyPlan;
/* "disk", "cone", "orb" */
const char *gc_plan_name(int plan);

/* What an organism senses and steers by; each drive is -1 (away) to 1
 * (toward). */
typedef enum {
    DRIVE_LIGHT,   /* toward brighter water at its height, out of the shade (or into it) */
    DRIVE_CARBON,  /* toward water with more dissolved CO2 */
    DRIVE_PLANTS,  /* toward living bodies, by how green they are */
    DRIVE_HUNTERS, /* toward living bodies, by how much red, gray and white they carry */
    DRIVE_DEAD,    /* toward corpses */
    DRIVE_CROWD,   /* toward any living body */
    DRIVE_COUNT
} Drive;

typedef struct {
    double length;
    double theta; /* turn within the arm's plane, as in 0.9 */
    double pitch; /* new: bend out of that plane, -pi/2..pi/2; 0 is flat */
    uint8_t color; /* BioColor */
    uint32_t id;
} Gene;

typedef struct GeneticCode {
    int refs;
    uint32_t lineage; /* a mark tools can follow down the generations */
    int symmetry;     /* 1..8 arms around the body's axis */
    int mirror;       /* 0 or 1: every other arm mirrored */
    bool disperse_children;
    int reproduce_energy;
    int max_age;
    double drive[DRIVE_COUNT]; /* new */
    double height; /* new: where its line keeps, 0 the floor to 1 the surface */
    uint8_t plan;  /* new: how its arms stand (BodyPlan): 0.9's disk, a cone, an orb */
    double cone;   /* new: a cone's arms' tilt out of the disk, -pi/2 (down) to pi/2 (up) */
    int ngenes;
    Gene genes[];
} GeneticCode;

void gene_randomize_color(Gene *g);
void gene_randomize_length(Gene *g);
void gene_randomize_theta(Gene *g);
void gene_randomize_pitch(Gene *g);
void gene_randomize(Gene *g);
uint32_t gene_new_id(void);

GeneticCode *gc_new_random(void);
GeneticCode *gc_new_mutated(const GeneticCode *parent);
/* A code from given genes (ids renewed), e.g. an imported 0.9 being. Its
 * drives are 0. */
GeneticCode *gc_new_from_genes(const Gene *genes, int n, int symmetry, int mirror, bool disperse);

GeneticCode *gc_ref(GeneticCode *gc);
void gc_unref(GeneticCode *gc);
void gc_assign(GeneticCode **slot, GeneticCode *gc);

/* Whether every gene lies flat (pitch 0): a body 0.9 could have grown. */
bool gc_is_flat(const GeneticCode *gc);

/* Whether any drive is set: an organism that steers. */
bool gc_steers(const GeneticCode *gc);
/* Drives read from the body, for a 0.9 code that has none: 0.9 had no
 * senses, so a being from it is given what its make-up asks for. Leaves
 * seek the light and keep away from hunters; eating, killing and infecting
 * parts seek plants; eating parts seek the dead. */
void gc_instincts(GeneticCode *gc);
/* A height read from the body, for a code that has none (0.9's, Color
 * Mod's): leaves high, methane eaters and eaters with no leaves low, the
 * rest between. */
double gc_body_height(const GeneticCode *gc);
/* The height whose light a light drive of an earlier version of this
 * program liked ((1 + drive) / 2 of the surface's), in this water; for
 * codes saved before the height gene. */
double gc_height_of_light_drive(double drive, double depth, double light_depth);
/* "light", "carbon", "plants", "hunters", "dead", "crowd" */
const char *gc_drive_name(int d);

const char *bio_color_name(int c);
int bio_color_from_name(const char *name);
void bio_color_rgb(int c, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
