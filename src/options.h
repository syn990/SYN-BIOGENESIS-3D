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

/* The options every mode shares: which world to make. The window and the
 * headless run each read their own options around these. */
#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "sim/world.h"

#define MAX_IMPORTS 4

typedef struct {
    int64_t seed;
    /* beings (.bgg) to add after genesis, copies of each; the i-th one's
     * descendants carry lineage mark i + 1 */
    char import[MAX_IMPORTS][1024];
    int nimports;
    int copies;
} WorldOptions;

#define WORLD_OPTIONS_HELP                                                                        \
    "  --seed N          the world's seed (default 1)\n"                                         \
    "  --size W H D      the world box (default 1000 1000 32)\n"                                 \
    "  --organisms N     random founders (default 15)\n"                                         \
    "  --co2 X           carbon in the air at genesis (default 5000)\n"                          \
    "  --pitch P         percent of new genes that bend out of their plane\n"                    \
    "                    (default 50; 0 keeps every new body flat, as in 0.9)\n"                 \
    "  --radius R        segment thickness: capsule radius (default 1)\n"                        \
    "  --import FILE     add copies of a Biogenesis 0.9 being (.bgg); up to 4, each\n"           \
    "                    with its own line of descendants (CSV: marked, line2...)\n"            \
    "  --copies K        copies of each imported being (default 1)\n"

/* Sets the parameters' defaults and o's. */
void world_options_init(WorldOptions *o);
/* Reads the option at argv[*i] if it is one of these, moving *i past its
 * values. Returns false (leaving *i) if it is not. */
bool world_option(WorldOptions *o, int argc, char **argv, int *i);
/* Checks the parameters; on a problem writes it to err and returns false. */
bool world_options_check(char *err, int errlen);
/* Seeds the dice and makes the world: genesis, then the imports, in
 * order. Returns the number of imported founders placed, or -1 with err set. */
int world_options_genesis(const WorldOptions *o, World *w, char *err, int errlen);
/* The one being to import from now on (NULL: none). */
void world_options_import_only(WorldOptions *o, const char *path);

#endif
