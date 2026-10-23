/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
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

/* Reading Biogenesis 0.9 genetic code files (.bgg).
 *
 * A 0.9 being arrives flat: every gene's pitch is 0, so its body is the
 * one it had in 2D, lying in a plane. A gene may also carry a pitch
 * attribute (radians), which 0.9 never wrote, for beings saved from 3D.
 * This is a small, forgiving reader for the format 0.9 writes; files with
 * Color Mod's colours are refused, since their rules are not here.
 */
#ifndef IO_BGG_H
#define IO_BGG_H

#include "sim/genetic_code.h"

/* NULL with a message in err on failure. */
GeneticCode *bgg_load(const char *path, char *err, int errlen);

#endif
