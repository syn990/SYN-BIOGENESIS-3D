/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/sidecar.h, the SYN edition of its C port
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

/* What the window knows of a saved world beyond the world file: the
 * statistics' history (syn/history.h) and the family tree with its names
 * (syn/family.h), in a file beside it, the world's name and ".syn"
 * (world.b3w.syn), as SYN-BIOGENESIS keeps them. The world file holds the
 * world alone, so the program without a window reads it as ever.
 *
 * The sidecar carries the size and a hash of the world file's bytes. When
 * they don't match the file being opened (it was saved again by another
 * program, or replaced), the sidecar is ignored and the history starts at
 * the opening, as without one. */
#ifndef SYN_SIDECAR_H
#define SYN_SIDECAR_H

#include <stdbool.h>
#include <stddef.h>

#include "sim/world.h"

/* After the world has been saved at world_path. */
bool syn_sidecar_save(const World *w, const char *world_path);
/* After the world has been opened from world_path and the history reset
 * (syn_history_reset(w, true)): the history and tree saved with it, if
 * its sidecar is there and matches. */
bool syn_sidecar_load(World *w, const char *world_path);

#endif
