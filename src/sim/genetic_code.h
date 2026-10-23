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
    COL_BROWN,  /* dead */
    COL_COUNT
} BioColor;

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
/* A code from given genes (ids renewed), e.g. an imported 0.9 being. */
GeneticCode *gc_new_from_genes(const Gene *genes, int n, int symmetry, int mirror, bool disperse);

GeneticCode *gc_ref(GeneticCode *gc);
void gc_unref(GeneticCode *gc);
void gc_assign(GeneticCode **slot, GeneticCode *gc);

/* Whether every gene lies flat (pitch 0): a body 0.9 could have grown. */
bool gc_is_flat(const GeneticCode *gc);

const char *bio_color_name(int c);
int bio_color_from_name(const char *name);
void bio_color_rgb(int c, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
