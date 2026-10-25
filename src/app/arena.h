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

/* The arena's screen (syn/arena.h runs the arena itself): SETUP, BATTLES,
 * TREE and RECORD, as SYN-BIOGENESIS shows them. */
#ifndef APP_ARENA_H
#define APP_ARENA_H

struct App;

void arena_screen(struct App *a, int w, int h);
/* For a picture of the screen: SYN3D_SHOT_ARENA_VIEW picks the view, and
 * SYN3D_SHOT_ARENA=rounds,units,seed runs rounds first on a ladder kept in
 * memory. */
void arena_shot_prepare(void);
void arena_screen_free(void);

#endif
