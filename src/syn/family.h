/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/family.h, the SYN edition of its C port
 * of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* A world's family tree: its species and which species each one came
 * from, recorded beside the simulation, for the arena's rounds and the
 * statistics.
 *
 * A species is a body plan, the arms, the mirroring and
 * the colours of the genes in order. Lengths and angles mutate within it;
 * a birth that changes the plan founds a new species, a child of the one
 * its code was copied from. gc->lineage carries the species: the
 * simulation copies it from parent to mutated child, and from the
 * infecting code to an infected parent's child, and reads it nowhere. The
 * bends (pitch) are lengths and angles here: they mutate within a species.
 * The lines are the roots of these trees.
 *
 * Species that never mattered are forgotten once they die out: those that
 * never had FAM_NOTABLE living at once (and as much as one in 50 of the
 * living), with nothing kept descending from them, best of no epoch.
 *
 * Names. Species are grouped into families: a root founds one, and so
 * does a species whose plan has moved far enough from the plan its family
 * began with (FAM_DIVERGE changes, counting arms gained or lost,
 * mirroring, and genes added, removed or recoloured one each). A family is
 * given
 * the next of 500 names (syn/family_names.h: invented ones, the Greek
 * letters among them; then the same again with II, III ...) when the
 * first of its species becomes notable, and each species is numbered in
 * its family when it does (VELORAX-3). Names are given once a time unit
 * from the world alone, never from what is shown. */
#ifndef SYN_FAMILY_H
#define SYN_FAMILY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim/world.h"

#define FAM_NOTABLE 5
#define FAM_DIVERGE 4 /* changes to the plan */

typedef struct {
    uint32_t id;     /* the mark in gc->lineage */
    uint32_t parent; /* 0: a root (genesis, the world as opened, a pasted or new code) */
    uint32_t root;
    int depth;       /* steps from its root */
    int founder;     /* the organism it began with */
    int64_t born, last_seen, peak_t; /* time units */
    int alive, peak;
    int64_t members; /* organisms born into it (or found in it) */
    int kids;        /* species kept that came from it */
    int pins;        /* epochs it is the best of */
    int carriers;    /* living organisms infected with its code */
    bool notable, extinct;
    uint64_t key;    /* its plan, hashed */
    uint8_t colour;  /* its founder's commonest palette slot */
    GeneticCode *gc; /* its founder's code (held) */
    uint32_t family; /* the species its family began with (its own id if it began one) */
    int name_no;     /* a family's first species: the family's name, from 1 (0: not named yet) */
    int number;      /* its number in its family (0: never notable) */
    int numbered;    /* a family's first species: how many numbers its family has given */
} Species;

/* With the history's (syn_history_reset). */
void syn_family_reset(World *w);
/* After every simulation frame: the newborn. */
void syn_family_frame(World *w);
/* Once a time unit, before the history reads the world: the living
 * counted, the species that never mattered forgotten. */
void syn_family_unit(World *w, int64_t t);
void syn_family_free(void);

/* The species an organism belongs to (0: none yet). */
uint32_t syn_family_of(const Organism *o);
/* NULL if unknown or forgotten. Pointers last until the next frame. */
const Species *syn_family_get(uint32_t id);
/* All of them, parents before their children. */
int syn_family_count(void);
const Species *syn_family_at(int i);
/* Its place among them, or -1. */
int syn_family_index(uint32_t id);
/* The species with the most living now, and how many (0: none). */
uint32_t syn_family_top(int *alive);
/* Pin (+1) or unpin (-1): a pinned species is never forgotten. */
void syn_family_pin(uint32_t id, int d);
/* Changes with every species founded, forgotten or counted. */
unsigned syn_family_version(void);

/* The species its family began with (itself for that one). */
const Species *syn_family_head(const Species *s);
/* The family its family came from (NULL for a root's). */
const Species *syn_family_parent_head(const Species *s);
/* How many families are named. */
int syn_family_named(void);
/* A family's name from its number: "VELORAX", "OMICRON II" ("" for 0). */
void syn_family_title(int no, char *out, size_t n);
/* The first family takes the name base places into the list (0, the
 * default: LAEXERA), and the rest follow it round, so the arena's rounds
 * (syn/arena.h), each a world of its own, don't all begin with the same
 * names. */
void syn_family_name_base(int base);
/* Its name: "VELORAX-3", or "VELORAX" before it is numbered; "" while its
 * family has no name. */
void syn_family_name(const Species *s, char *out, size_t n);
/* Its name ("VELORAX-3", "VELORAX sp." before it is numbered), else "#"
 * and its founder's number. */
void syn_family_label(const Species *s, char *out, size_t n);
/* The line an organism is in: its species' root (0: none yet). */
uint32_t syn_line_key(const Organism *o);
/* A line (the root species' id), as a species. */
const Species *syn_family_line(uint32_t line);

/* The whole tree as bytes, for saving beside a world: every species, its
 * code and name, the counters, and (0.9) which species each organism's
 * code and infecting code belong to. malloc'd; returns the length, 0 on
 * failure. */
size_t syn_family_save(const World *w, uint8_t **out);
/* The tree saved for this world, in place of the one found at reset
 * (call after syn_family_reset). The epochs' pins are in it: the history
 * must not pin its epochs' best again. False (and the tree unchanged) if
 * the bytes are not a tree saved by this simulation. */
bool syn_family_load(World *w, const uint8_t *in, size_t n);

/* What changed from its parent's plan ("6 to 7 arms, + blue segment
 * (8th)"); "" for a root. */
void syn_family_change(const Species *s, char *out, size_t n);
/* Its plan in words ("7 arms; 8 segments per arm: 5 green, yellow, 2 blue"). */
void syn_family_plan(const GeneticCode *gc, char *out, size_t n);

#endif
