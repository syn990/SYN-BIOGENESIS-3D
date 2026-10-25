/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * SYN-BIOGENESIS's src/syn/palette.h for 0.9's colours alone, which are
 * all this program has: what the field guide and the kinds read.
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

/* A slot per colour (0.9's eight: the slot is the colour), with its RGB,
 * its name and its job. */
#ifndef GUIDE_PALETTE_H
#define GUIDE_PALETTE_H

#include <stdbool.h>
#include <stdint.h>

#include "sim/genetic_code.h"

typedef enum {
    ROLE_PHOTO,    /* makes energy from light */
    ROLE_EAT,      /* eats */
    ROLE_KILL,     /* kills */
    ROLE_DEFEND,   /* shields */
    ROLE_INFECT,   /* infects with its own code */
    ROLE_HEAL,     /* (Color Mod's; none in 0.9) */
    ROLE_MOVE,     /* propels the organism */
    ROLE_REPRO,    /* more children */
    ROLE_SENSE,    /* (Color Mod's eyes; none in 0.9) */
    ROLE_OTHER,
    ROLE_INACTIVE,
    ROLE_DEAD,
    ROLE_COUNT
} Role;

#define SYN_NCOL COL_COUNT
#define SYN_DEAD COL_BROWN
#define GENE_COLORS 8

int syn_col(uint32_t colour);
void syn_col_rgb(int slot, uint8_t *r, uint8_t *g, uint8_t *b);
/* "green" */
const char *syn_col_name(int slot);
Role syn_col_role(int slot);

int syn_genes(const GeneticCode *gc);
int syn_gene_col(const GeneticCode *gc, int i);
double syn_gene_len(const GeneticCode *gc, int i);

#endif
