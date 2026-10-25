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

/* Written from the simulations' code (0.9's Organism.touchEffects and
 * segment effects; Color Mod 2.01's Organism.java), with Color Mod's own
 * readme for what its modifier genes are for. "A bite" is what an eating
 * segment takes in one touch: 0.9's length x 0.5, Color Mod's
 * 2 x the square root of its length. */
#include "guide/guide.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name; /* as syn_col_name has it */
    const char *line;
    const char *more;
} Entry;


static const Entry k_entries[] = {
    {"green", "Leaf: makes energy from light and CO2.",
     "Every frame it turns CO2 dissolved around it into energy, as much as the light at its depth "
     "and its length lying across that light allow. It turns itself to lie level."},
    {"red", "Eater: bites whatever it touches.",
     "Takes half its length in energy from any segment it touches; only blue stops it."},
    {"cyan", "Mover: swims the organism to its height and where its senses pull it.",
     "Each frame there is an 8 in 100 chance that it pushes the organism along its own length, "
     "whichever way is nearer where it is going: up or down to the height its line keeps to, and "
     "across as its drives pull it. A body bent in 3D can swim any way."},
    {"blue", "Shield: stops red, white and gray.",
     "Blocks bites, infections and kills, at no cost with the default parameters."},
    {"white", "Virus: makes others give birth to its kind.",
     "For 1 energy it infects the organism it touches, whose next child is then a copy of the infector. "
     "Blue stops it."},
    {"gray", "Killer: kills what it touches.",
     "For 1 energy it kills the organism it touches outright. Blue stops it."},
    {"yellow", "Fertility: one more child per segment.",
     "Every yellow segment is one more child at each birth."},
    {"purple", "Methane eater: lives on what the dead rot into.",
     "Color Mod's chemotroph. Every frame it turns methane dissolved around it into energy, by its "
     "whole length and whatever the light, as a leaf does CO2. The dead rot into methane where they "
     "lie, and they sink, so it is richest low, around the rotting floor."},
    {"brown", "Dead.",
     "A dead body. It sinks, and rots into methane where it comes to rest. Parents and children never "
     "attack each other while alive."},
};


static const Entry *entry(int slot)
{
    const char *name = syn_col_name(slot);
    for (size_t i = 0; i < sizeof k_entries / sizeof k_entries[0]; i++)
        if (!strcmp(k_entries[i].name, name))
            return &k_entries[i];
    return NULL;
}

const char *syn_guide_short(int slot)
{
    const Entry *e = entry(slot);
    return e ? e->line : "";
}

const char *syn_guide_long(int slot)
{
    const Entry *e = entry(slot);
    return e ? e->more : "";
}

bool syn_guide_is_gene(int slot)
{
    return slot >= 0 && slot < GENE_COLORS;
}

/* letters only, lower case: "Dark Gray" and "darkgray" are one */
static void squash(const char *s, char *out, size_t n)
{
    size_t k = 0;
    for (; *s && k + 1 < n; s++)
        if (isalnum((unsigned char)*s))
            out[k++] = (char)tolower((unsigned char)*s);
    out[k] = '\0';
}

int syn_guide_find(const char *name)
{
    char want[64], have[64];
    squash(name, want, sizeof want);
    if (!strcmp(want, "c4"))
        snprintf(want, sizeof want, "c4plant");
    for (int s = 0; s < SYN_NCOL; s++) {
        squash(syn_col_name(s), have, sizeof have);
        if (!strcmp(want, have))
            return s;
    }
    return -1;
}

const char *syn_guide_role(Role r)
{
    static const char *const t[ROLE_COUNT] = {
        "LEAVES", "EATERS", "KILLERS", "DEFENCE", "VIRUSES", "HELPERS", "MOVERS", "BREEDING", "SENSES",
        "OTHERS", "OUT OF ACTION", "DEAD",
    };
    return r >= 0 && r < ROLE_COUNT ? t[r] : "";
}
