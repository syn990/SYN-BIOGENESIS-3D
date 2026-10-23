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

/* The field guide to the colours: what each segment colour does, in one
 * line and in a few, for the simulation this build runs (0.9's seven, or
 * Color Mod's 46 and the colours its segments turn for a while), as its
 * code does it with the default parameters. Keyed by palette slot
 * (syn/palette.h). */
#ifndef GUIDE_GUIDE_H
#define GUIDE_GUIDE_H

#include "guide/palette.h"

/* One line: "Shield: blocks almost every attack." ("" for no entry) */
const char *syn_guide_short(int slot);
/* A few sentences */
const char *syn_guide_long(int slot);
/* A gene colour (false: a colour segments only turn, or none) */
bool syn_guide_is_gene(int slot);
/* The slot named (any case; "c4" and "darkgray" too), or -1 */
int syn_guide_find(const char *name);
/* A job's heading: "LEAVES", "EATERS"... */
const char *syn_guide_role(Role r);

#endif
