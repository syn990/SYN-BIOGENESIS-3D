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

/* A whole world in a file (.b3w): the parameters, the carbon (the CO2 in
 * every cell of the water), the dice's state, every organism as it stands
 * between two frames, and every genetic code once, shared where the world
 * shares it (a virus's code in every organism it has infected, an
 * import's in all its copies), since the rules compare codes by
 * identity. A world loaded goes on exactly as
 * the one saved would have, frame for frame (tests/check.c checks this).
 *
 * The format is this program's own: little-endian, doubles as their bits,
 * a magic word and a version first. */
#ifndef IO_WORLDFILE_H
#define IO_WORLDFILE_H

#include <stdbool.h>

#include "sim/world.h"

#define WORLDFILE_EXT "b3w"

/* Between frames only. False with err set. */
bool worldfile_save(const World *w, const char *path, char *err, int errlen);
/* Replaces w (and the parameters P and the dice g_rng) with the file's. On
 * failure w and P are as they were. */
bool worldfile_load(World *w, const char *path, char *err, int errlen);

#endif
