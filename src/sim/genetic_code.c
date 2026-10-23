/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from GeneticCode.java and Gene.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/genetic_code.c.
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

#include "sim/genetic_code.h"

#include <stdlib.h>
#include <string.h>

#include "sim/dmath.h"
#include "sim/params.h"

void gene_randomize_color(Gene *g)
{
    int max_prob = P.red_prob + P.green_prob + P.blue_prob + P.cyan_prob + P.white_prob +
                   P.gray_prob + P.yellow_prob;
    int prob = jr_next_int(&g_rng, max_prob);
    const int probs[7] = {P.red_prob,   P.green_prob, P.blue_prob,  P.cyan_prob,
                          P.white_prob, P.gray_prob,  P.yellow_prob};
    const uint8_t colors[7] = {COL_RED,   COL_GREEN, COL_BLUE,  COL_CYAN,
                               COL_WHITE, COL_GRAY,  COL_YELLOW};
    int ac = 0;
    for (int i = 0; i < 6; i++) {
        ac += probs[i];
        if (prob < ac) {
            g->color = colors[i];
            return;
        }
    }
    g->color = COL_YELLOW;
}

void gene_randomize_length(Gene *g)
{
    g->length = 2.0 + jr_next_double(&g_rng) * 16.0;
}

void gene_randomize_theta(Gene *g)
{
    g->theta = jr_next_double(&g_rng) * 2.0 * D_PI;
}

/* A new gene bends out of its plane with probability pitch_prob percent,
 * by an angle anywhere from straight down to straight up. */
void gene_randomize_pitch(Gene *g)
{
    if (jr_next_int(&g_rng, 100) < P.pitch_prob)
        g->pitch = (jr_next_double(&g_rng) - 0.5) * D_PI;
    else
        g->pitch = 0;
}

uint32_t gene_new_id(void)
{
    static uint32_t next;
    return ++next;
}

void gene_randomize(Gene *g)
{
    g->id = gene_new_id();
    gene_randomize_length(g);
    gene_randomize_theta(g);
    gene_randomize_pitch(g);
    gene_randomize_color(g);
}

static GeneticCode *gc_alloc(int ngenes)
{
    GeneticCode *gc = calloc(1, sizeof *gc + (size_t)ngenes * sizeof(Gene));
    if (!gc)
        abort();
    gc->refs = 1;
    gc->ngenes = ngenes;
    return gc;
}

static void gc_calculate_reproduce_energy(GeneticCode *gc)
{
    gc->reproduce_energy = 40 + 3 * gc->ngenes * gc->symmetry;
}

static int random_mirror(void)
{
    return jr_next_int(&g_rng, 2);
}

static int random_symmetry(void)
{
    return jr_next_int(&g_rng, 8) + 1;
}

GeneticCode *gc_new_random(void)
{
    int mirror = random_mirror();
    int symmetry = random_symmetry();
    int ngenes = 1 + jr_next_int(&g_rng, 8);
    GeneticCode *gc = gc_alloc(ngenes);
    gc->mirror = mirror;
    gc->symmetry = symmetry;
    for (int i = 0; i < ngenes; i++)
        gene_randomize(&gc->genes[i]);
    gc->disperse_children = jr_next_boolean(&g_rng);
    gc_calculate_reproduce_energy(gc);
    gc->max_age = P.max_age;
    return gc;
}

/* 0.9's mutation, gene for gene. 0.9 drew a fourth chance per gene
 * ("back") and never used it; here that draw is the pitch's mutation, so a
 * copied gene still takes exactly the four draws it took in 0.9. */
GeneticCode *gc_new_mutated(const GeneticCode *parent)
{
    int added = -1, removed = -1;
    int ngenes, symmetry, mirror;

    if (bio_random_mutation())
        mirror = random_mirror();
    else
        mirror = parent->mirror;
    if (bio_random_mutation()) {
        symmetry = random_symmetry();
        ngenes = parent->ngenes;
    } else {
        symmetry = parent->symmetry;
        if (bio_random_mutation()) {
            if (jr_next_boolean(&g_rng)) {
                if (parent->ngenes >= 100) {
                    ngenes = parent->ngenes;
                } else {
                    ngenes = parent->ngenes + 1;
                    added = jr_next_int(&g_rng, ngenes);
                }
            } else {
                if (parent->ngenes <= 1) {
                    ngenes = parent->ngenes;
                } else {
                    ngenes = parent->ngenes - 1;
                    removed = jr_next_int(&g_rng, parent->ngenes);
                }
            }
        } else {
            ngenes = parent->ngenes;
        }
    }

    GeneticCode *gc = gc_alloc(ngenes);
    gc->mirror = mirror;
    gc->symmetry = symmetry;
    for (int i = 0, j = 0; i < ngenes; i++, j++) {
        if (removed == j) {
            i--;
            continue;
        }
        if (added == i) {
            gene_randomize(&gc->genes[i]);
            j--;
            continue;
        }
        bool rlen = bio_random_mutation();
        bool rtheta = bio_random_mutation();
        bool rcolor = bio_random_mutation();
        bool rpitch = bio_random_mutation();
        const Gene *src = &parent->genes[j];
        Gene *g = &gc->genes[i];
        *g = *src;
        if (rlen || rtheta || rcolor || rpitch) {
            g->id = gene_new_id();
            if (rlen)
                gene_randomize_length(g);
            if (rtheta)
                gene_randomize_theta(g);
            if (rcolor)
                gene_randomize_color(g);
            if (rpitch)
                gene_randomize_pitch(g);
        }
    }
    if (bio_random_mutation())
        gc->disperse_children = jr_next_boolean(&g_rng);
    else
        gc->disperse_children = parent->disperse_children;
    gc_calculate_reproduce_energy(gc);
    gc->max_age = P.max_age;
    gc->lineage = parent->lineage;
    return gc;
}

GeneticCode *gc_new_from_genes(const Gene *genes, int n, int symmetry, int mirror, bool disperse)
{
    GeneticCode *gc = gc_alloc(n);
    if (n > 0)
        memcpy(gc->genes, genes, (size_t)n * sizeof(Gene));
    for (int i = 0; i < n; i++)
        gc->genes[i].id = gene_new_id();
    gc->max_age = P.max_age;
    gc->mirror = mirror;
    gc->symmetry = symmetry;
    gc->disperse_children = disperse;
    gc_calculate_reproduce_energy(gc);
    return gc;
}

GeneticCode *gc_ref(GeneticCode *gc)
{
    if (gc)
        gc->refs++;
    return gc;
}

void gc_unref(GeneticCode *gc)
{
    if (gc && --gc->refs == 0)
        free(gc);
}

void gc_assign(GeneticCode **slot, GeneticCode *gc)
{
    if (*slot == gc)
        return;
    gc_ref(gc);
    gc_unref(*slot);
    *slot = gc;
}

bool gc_is_flat(const GeneticCode *gc)
{
    for (int i = 0; i < gc->ngenes; i++)
        if (gc->genes[i].pitch != 0)
            return false;
    return true;
}

static const char *k_color_names[COL_COUNT] = {"green", "red",  "cyan",   "blue",
                                               "white", "gray", "yellow", "brown"};

const char *bio_color_name(int c)
{
    return (c >= 0 && c < COL_COUNT) ? k_color_names[c] : "";
}

int bio_color_from_name(const char *name)
{
    for (int c = 0; c < COL_COUNT; c++)
        if (strcmp(name, k_color_names[c]) == 0)
            return c;
    return -1;
}

void bio_color_rgb(int c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    static const uint8_t rgb[COL_COUNT][3] = {
        {0, 255, 0},     {255, 0, 0},     {0, 255, 255}, {0, 0, 255},
        {255, 255, 255}, {128, 128, 128}, {255, 255, 0}, {150, 75, 0},
    };
    if (c < 0 || c >= COL_COUNT)
        c = COL_WHITE;
    *r = rgb[c][0];
    *g = rgb[c][1];
    *b = rgb[c][2];
}
