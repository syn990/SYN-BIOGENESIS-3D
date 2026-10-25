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
 * attribute (radians), which 0.9 never wrote, for beings saved from 3D,
 * and the genetic code its drives (light="..." and so on, -1 to 1). A 0.9
 * being has none, and is given the drives its body asks for
 * (gc_instincts).
 * This is a small, forgiving reader, and one reader for every Biogenesis:
 * what a file lacks is worked out rather than refused. A 0.9 being's
 * height comes from its body (gc_body_height), as its drives do; one saved
 * by an earlier version of this program, with drives but no height, keeps
 * to the height its light drive liked. A Color Mod 2.01 being comes in as
 * one of this program's: each of Color Mod's colours becomes the colour
 * here that does its job (its leaves green, its eaters red, its killers
 * gray, its shields and healers blue, its viruses white, its movers and
 * eyes cyan, its breeders yellow, its methane eaters purple), its branches
 * are straightened into arms, and its reaction and modifier genes, which
 * have no counterpart here, are left behind. What was done is said in a
 * BggInfo.
 */
#ifndef IO_BGG_H
#define IO_BGG_H

#include <stdint.h>
#include <stdio.h>

#include "sim/genetic_code.h"

/* What reading a file found and did. */
typedef struct {
    char from[32];  /* "SYN-BIOGENESIS-3D", "Biogenesis 0.9" or "Color Mod 2.01" */
    int converted;  /* genes whose colour was given this program's */
    int straightened; /* genes that branched */
    bool height, drives; /* worked out, not read */
    char note[300]; /* all that, in words ("" if nothing was) */
} BggInfo;

/* NULL with a message in err on failure. */
GeneticCode *bgg_load(const char *path, char *err, int errlen);
/* The same from a file's bytes in memory. */
GeneticCode *bgg_parse(const void *bytes, size_t n, char *err, int errlen);
/* Either, saying what it found and did (info may be NULL). */
GeneticCode *bgg_load_info(const char *path, BggInfo *info, char *err, int errlen);
GeneticCode *bgg_parse_info(const void *bytes, size_t n, BggInfo *info, char *err, int errlen);

/* Writes gc as 0.9 writes a .bgg, with this program's own as attributes
 * declared optional in the DTD: pitch="..." on its bent genes, and on
 * <genetic_code> its drives, its height, and its plan and cone when it is
 * not 0.9's disk. A purple gene is "purple". The shipped 0.9's own parser
 * reads such files and ignores the bends
 * (checked 2026-10-23 with SYN-BIOGENESIS's tools/javaref BggInterop: the
 * same symmetry, genes, lengths and turns, bit for bit). */
bool bgg_save(const char *path, const GeneticCode *gc, char *err, int errlen);
void bgg_write(FILE *f, const GeneticCode *gc);
/* The file's bytes, malloc'd (NULL if out of memory). */
uint8_t *bgg_bytes(const GeneticCode *gc, size_t *n);

#endif
