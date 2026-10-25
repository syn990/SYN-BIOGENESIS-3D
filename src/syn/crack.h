/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/crack.h, the SYN edition of its C
 * port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* The seed-cracking engine, worked out again for this program's genesis:
 * from a being alone, its genesis ancestor, every seed that ancestor was a
 * founder in, and the being's later draws in the order they were made.
 *
 * What carries it here:
 * - A founder is a run of draws from one state: org_new's turn (2), the
 *   mirroring, the arms and the genes' number (1 each), then per gene its
 *   length (2), turn (2), whether it bends (1), its bend if it does (2)
 *   and its colour (1); then the dispersal (1), the six drives (2 each),
 *   its height (2), plan (1) and cone (2), and three a try at a place. So gene j's length is drawn 5 + 6 j + 2 m
 *   draws after the founder was begun, m the genes before it that bend: a
 *   length cracked to its state (telescope.h) steps back to the founder's
 *   start in at most 36 ways, and drawing again from each, the one whose
 *   gene j is that length is the founder. Its other genes and its drives,
 *   where the being still has them, prove it.
 * - Genesis is founders made one after another from one stream, so the
 *   seeds whose genesis begins a founder at a given state are all within a
 *   founder's draws per slot before it; trying each finds them all.
 *
 * Nothing here changes the simulation: it draws codes as gc_new_random
 * does, makes worlds as world_genesis does, and reads them. */
#ifndef SYN_CRACK_H
#define SYN_CRACK_H

#include <stdbool.h>
#include <stdint.h>

struct GeneticCode;
struct World;

/* gc_new_random gives a founder 1 to 8 genes */
#define CRACK_FOUNDER_GENES 8

/* A founder the being comes from, drawn again. */
typedef struct {
    uint64_t start;          /* the generator's state when org_new began it */
    struct GeneticCode *gc;  /* its code, drawn again from start */
    int gene;                /* its gene the cracked one was (from 0) */
    int proof;               /* the being's other draws in it: lengths, turns, bends and drives */
    int kept;                /* the being's genes it has whole (length, turn, bend, colour) */
    int changes;             /* the fewest mutations from it to the being */
} CrackAncestor;

/* Its genesis ancestors, the likeliest first: the founders that some gene
 * of the being points back to, with at least one more of its draws to
 * prove each; the one the being is the fewest mutations from first. */
int crack_ancestors(const struct GeneticCode *being, CrackAncestor *out, int max);
void crack_ancestors_free(CrackAncestor *a, int n);
/* The fewest mutations that make one code from another: genes added,
 * removed, or with their length, turn, bend or colour drawn again (in
 * order, as gc_new_mutated keeps them), and the arms, mirroring, dispersal
 * and drives. */
int crack_changes(const struct GeneticCode *from, const struct GeneticCode *to);
/* The founder org_new and gc_new_random would make from this state, under
 * P's colours and bends. */
struct GeneticCode *crack_founder_at(uint64_t start);
/* Whether two codes are the same draws: genes, arms, mirroring, dispersal
 * and drives, bit for bit. */
bool crack_same(const struct GeneticCode *a, const struct GeneticCode *b);

/* A draw of the being made after its ancestor was: a gene's length (a
 * gene added, or its length drawn again), its turn or its bend alone, or a
 * drive drawn again. */
typedef struct {
    int gene;       /* the being's gene (from 0), or for a drive, the drive */
    char what;      /* 'l' its length, 'a' its turn, 'b' its bend, 'd' a drive, 'h' its
                     * height, 'k' its cone */
    bool whole;     /* its turn drawn straight after its length: a gene added */
    uint64_t state; /* the state that made the draw (0: a number the generator never makes) */
    uint64_t bits;  /* the length or turn drawn, as the double's bits */
    int64_t place;  /* draws after its ancestor was begun, or with none after the being's
                     * earliest draw (-1: not in that stream) */
} CrackDraw;

/* The being's draws its ancestor hasn't, each placed in the ancestor's
 * stream, the soonest first. With no ancestor (NULL), all its genes,
 * placed after the earliest of them. */
int crack_draws(const struct GeneticCode *being, const CrackAncestor *a, CrackDraw *out, int max);

/* A seed whose genesis begins the ancestor. */
typedef struct {
    uint64_t state; /* its first state */
    int64_t seed;   /* in [0, 2^48) */
    int slot;       /* the ancestor's place among its founders (from 0) */
    int org;        /* its index in the world's organisms (some founders find no place) */
    int founders;
} CrackSeed;

/* Every seed whose genesis, under P, begins a founder at `start` that
 * finds a place in its world, in the order of their slots. back: how many
 * draws before it to look (0: as far as P's founders reach). Returns how
 * many, of which at most max are written (none for NULL: only counted). */
int crack_basin(uint64_t start, int64_t back, CrackSeed *out, int max);

/* world_genesis, a founder at a time: the world made ready, then each
 * founder made in turn (false once all are). The generator is g_rng, from
 * where it is. */
void crack_genesis_begin(struct World *w);
bool crack_genesis_next(struct World *w, int *made);

/* How likely a founder's bloodline is to lead its world, from its code
 * alone, on a log scale (fitted on replayed worlds: docs/crack). */
double crack_fitness(const struct GeneticCode *gc);
/* The chance founder i of n leads its world, from their fitness. */
double crack_win_chance(const double *fit, int n, int i);

/* "the being: 3 genes (1 bent), 2 arms: green 4.10 ..." after head */
void crack_say_code(const char *head, const struct GeneticCode *gc);

/* syn-biogenesis-3d --telescope crack|forge|basins ... */
int crack_main(int argc, char **argv);

#endif
