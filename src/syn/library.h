/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/library.h, the SYN edition of its C port
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

/* The being library: genetic codes kept from one launch to the next, in
 * folders, as files.
 *
 * It is the configuration folder's beings folder
 * (~/.config/syn-biogenesis-3d/beings). Each being is a .bgg file, 0.9's
 * own format with this program's bends and drives, named
 * after the being, with an .about file beside it saying where it came
 * from and when it was put there. The folders are the categories, nested
 * as deep as wanted; a .bgg put in one by hand is in the library too.
 * Deleting moves a being to the Bin folder; deleting it there removes it.
 *
 * Folders are named relative to the library, with '/' between levels,
 * "" for the library itself. Names of beings and folders are kept to
 * what every file system takes.
 *
 * SYN-BIOGENESIS's own libraries, of 0.9 beings and of Color Mod's
 * (~/.config/syn-biogenesis/beings/0.9 and .../colour), are read beside it
 * as they are, Color Mod's beings converted as they are read: the _in
 * functions read any library by its root. A being imported from any of
 * them, or any file, is kept as this program's. */
#ifndef SYN_LIBRARY_H
#define SYN_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>

#include "sim/genetic_code.h"

#define LIB_PATH 512
#define LIB_NAME 64
#define LIB_BIN "Bin"

typedef struct {
    char name[LIB_NAME]; /* the file's name without .bgg */
    char origin[200];    /* where it came from (its .about), "" if not said */
    char saved[32];      /* when it was put here, "" if not said */
} LibEntry;

/* For tests: the library somewhere else (NULL: the configuration folder's). */
void syn_library_set_root(const char *dir);
const char *syn_library_root(void);
/* SYN-BIOGENESIS's library of 0.9 beings, read only; _of(1) its Color Mod
 * beings', read as this program reads them (io/bgg.h). */
const char *syn_library_theirs(void);
const char *syn_library_theirs_of(int sim);
/* The library's folder made, with Saved, Imported, Arena and Bin the
 * first time (a library without its Bin is one from before it had
 * them). */
bool syn_library_ready(char *err, size_t n);

/* Every folder, each after its parent and in name order, "" first. */
int syn_library_folders(char (*out)[LIB_PATH], int max);
/* The beings in one folder (not those below it), in name order. */
int syn_library_list(const char *folder, LibEntry *out, int max);
int syn_library_count(const char *folder);
/* A being's code, as the simulation reads its file (with one reference,
 * for gc_unref). */
GeneticCode *syn_library_read(const char *folder, const char *name, char *err, size_t n);
/* The same, of the library at root (NULL: this one's). */
bool syn_library_path_in(const char *root, const char *folder, const char *name, char *out, size_t n);
int syn_library_folders_in(const char *root, char (*out)[LIB_PATH], int max);
int syn_library_list_in(const char *root, const char *folder, LibEntry *out, int max);
int syn_library_count_in(const char *root, const char *folder);
GeneticCode *syn_library_read_in(const char *root, const char *folder, const char *name, char *err,
                                 size_t n);

/* A code put in a folder under a name, made unique ("NAME (2)") if
 * taken; got is the name it has. */
bool syn_library_put(const char *folder, const char *name, const GeneticCode *gc,
                     const char *origin, char *got, size_t gotn, char *err, size_t n);
/* A .bgg file from anywhere copied in, if the simulation can read it. */
bool syn_library_import(const char *path, const char *folder, char *got, size_t gotn, char *err,
                        size_t n);
bool syn_library_move(const char *folder, const char *name, const char *to, char *err, size_t n);
bool syn_library_rename(const char *folder, const char *name, const char *to_name, char *got,
                        size_t gotn, char *err, size_t n);
/* To the Bin; from the Bin, gone. */
bool syn_library_delete(const char *folder, const char *name, char *err, size_t n);
bool syn_library_new_folder(const char *parent, const char *name, char *got, size_t gotn,
                            char *err, size_t n);
/* Only an empty one, and never the Bin. */
bool syn_library_remove_folder(const char *folder, char *err, size_t n);
/* A folder renamed where it is (not the library itself, nor the Bin);
 * got is its path after. */
bool syn_library_rename_folder(const char *folder, const char *to_name, char *got, size_t gotn,
                               char *err, size_t n);
/* The Bin's beings removed. */
int syn_library_empty_bin(void);

/* A name kept to what file systems take, at most LIB_NAME - 1 long. */
void syn_library_clean_name(const char *in, char *out, size_t n);

#endif
