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

/* The arena: two beings from the library fight in round after round, one
 * seed a round, each round run by this program itself (--headless) as a
 * child process, on every core but the window's. A round is decided by
 * whose line is still alive at its end. Any round can then be watched: the
 * window makes the very same world from the same seed, so what is shown is
 * the round that was scored, frame for frame. */
#ifndef APP_ARENA_H
#define APP_ARENA_H

#include <stdbool.h>

#define ARENA_MAX_ROUNDS 48

typedef enum { SHAPE_SLAB, SHAPE_DEFAULT, SHAPE_CUBE, SHAPE_COUNT } ArenaShape;

typedef struct {
    int seed;
    int line[2];   /* living descendants of each being at the end */
    int alive;     /* everything alive at the end */
    bool done, failed;
} ArenaRound;

struct App;
typedef struct Arena Arena;

Arena *arena_new(void);
/* Stops waiting for rounds still running (they finish on their own). */
void arena_free(Arena *ar);
void arena_screen(struct App *a, int w, int h);
/* Picks the beings named (a library name, or a .bgg path in the library)
 * and starts the fight. False if either isn't in the library. */
bool arena_start(struct App *a, const char *first, const char *second);
/* The world a round is fought in, set up in the window's options and
 * parameters (0.9's defaults and the shape's box). */
void arena_shape_params(ArenaShape s);
const char *arena_shape_name(ArenaShape s);

#endif
