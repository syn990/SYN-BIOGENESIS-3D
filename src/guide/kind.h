/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of the SYN edition of SYN-BIOGENESIS, a C port of Biogenesis
 * (https://biogenesis.sourceforge.net/).
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

/* What kind of being a genetic code makes, as SYN-TREK classes its planets:
 * a kind read from how much of the body does each job (its leaves, eaters,
 * killers...; syn/palette.h's roles, by length), a few traits, and a
 * description composed from the book of words (assets/lexicon/species.txt,
 * syn/lexicon.h), tagged by the kind and seeded by the code itself, so a
 * code is described the same way every time and everywhere. */
#ifndef GUIDE_KIND_H
#define GUIDE_KIND_H

#include <stddef.h>

#include "sim/genetic_code.h"

typedef enum {
    KIND_PLANT,     /* leaves, nothing more to speak of */
    KIND_DRIFTER,   /* leaves that move */
    KIND_FORTRESS,  /* leaves behind a quarter or more of shield */
    KIND_NETTLE,    /* leaves and killing parts */
    KIND_BLIGHT,    /* leaves and a virus */
    KIND_MIXED,     /* leaves and eating parts: "Half-plant" */
    KIND_GRAZER,    /* eats plants (Color Mod's maroon) */
    KIND_HUNTER,    /* eats eaters (Color Mod's red) */
    KIND_OMNIVORE,  /* eats almost anything (orange) */
    KIND_RAIDER,    /* bites eaters hard, the rest lightly (fire) */
    KIND_SCAVENGER, /* eats the dead and the weak (pink) */
    KIND_PARASITE,  /* small bites (cream) */
    KIND_PREDATOR,  /* 0.9's red: bites anything */
    KIND_VIRUS,     /* no way to feed but to infect */
    KIND_KILLER,    /* kills, can't feed */
    KIND_HUSK,      /* no way to feed at all */
    KIND_COUNT
} Kind;

/* The kind; its traits ("armoured, fertile": the biggest three not already
 * in the kind) written to traits if given */
Kind syn_kind(const GeneticCode *gc, char *traits, size_t n);
/* "Grazer" */
const char *syn_kind_name(Kind k);
/* What the kind is, in a line, for the field guide */
const char *syn_kind_meaning(Kind k);
/* "Grazer · armoured, fertile" */
void syn_kind_label(const GeneticCode *gc, char *out, size_t n);
/* A sentence or two about it: its size, its arms and how it lives */
void syn_kind_describe(const GeneticCode *gc, char *out, size_t n);

#endif
