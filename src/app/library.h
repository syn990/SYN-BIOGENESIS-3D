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

/* The beings on this machine: SYN-BIOGENESIS's library of 0.9 beings
 * (~/.config/syn-biogenesis/beings/0.9: Saved, Arena, Imported, Forged),
 * read as they are, and this program's own Kept folder
 * (~/.config/syn-biogenesis-3d/beings/Kept), where the window keeps an
 * organism's code, with an .about beside it saying where it came from, as
 * SYN-BIOGENESIS does. */
#ifndef APP_LIBRARY_H
#define APP_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>

#include "sim/organism.h"

typedef struct {
    char name[96];
    char path[1024];
    char origin[200]; /* from its .about, "" if none */
    GeneticCode *gc;
    char kind[96];
} Being;

typedef struct {
    char name[48];
    char dir[1024];
    bool ours;
    int first, count; /* its beings in Library.beings */
} Folder;

#define LIBRARY_FOLDERS 8

typedef struct {
    Being *beings;
    int nbeings;
    Folder folders[LIBRARY_FOLDERS];
    int nfolders;
} Library;

/* (Re)reads every folder. */
void library_scan(Library *L);
void library_free(Library *L);
/* Keeps o's code in Kept. path gets the file written; false with err set. */
bool library_keep(const Organism *o, long long seed, char *path, size_t pathlen, char *err,
                  int errlen);

#endif
