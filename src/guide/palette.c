/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * SYN-BIOGENESIS's src/syn/palette.c for 0.9's colours alone.
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

#include "guide/palette.h"

int syn_col(uint32_t colour)
{
    return colour < COL_COUNT ? (int)colour : COL_BROWN;
}

void syn_col_rgb(int slot, uint8_t *r, uint8_t *g, uint8_t *b)
{
    bio_color_rgb(slot, r, g, b);
}

const char *syn_col_name(int slot)
{
    static const char *const names[COL_COUNT] = {"green", "red",    "cyan",   "blue", "white",
                                                 "gray",  "yellow", "purple", "brown"};
    return slot >= 0 && slot < COL_COUNT ? names[slot] : "";
}

Role syn_col_role(int slot)
{
    switch (slot) {
    case COL_GREEN: return ROLE_PHOTO;
    case COL_RED: return ROLE_EAT;
    case COL_CYAN: return ROLE_MOVE;
    case COL_BLUE: return ROLE_DEFEND;
    case COL_WHITE: return ROLE_INFECT;
    case COL_GRAY: return ROLE_KILL;
    case COL_YELLOW: return ROLE_REPRO;
    case COL_PURPLE: return ROLE_PHOTO; /* a producer, as Color Mod files it */
    case COL_BROWN: return ROLE_DEAD;
    default: return ROLE_OTHER;
    }
}

int syn_genes(const GeneticCode *gc)
{
    return gc->ngenes;
}

int syn_gene_col(const GeneticCode *gc, int i)
{
    return gc->genes[i].color;
}

double syn_gene_len(const GeneticCode *gc, int i)
{
    return gc->genes[i].length;
}
