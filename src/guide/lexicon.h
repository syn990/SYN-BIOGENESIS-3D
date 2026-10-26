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

/* The book of words: text assembled from a seed, the same on every
 * machine.
 *
 * A BANK is a named list of phrases; a phrase may hold {references} to
 * other banks. A bank id may carry a tag ("habit.grazer"): a lookup for
 * `habit` with the tag `grazer` takes that bank, else plain `habit`, and
 * the tag holds for every reference inside. A {reference} that names a
 * bound variable (lexicon_set_var) says the variable instead, which is how
 * the facts of one species get into its sentence.
 *
 * The file's format:
 *
 *   [bank]
 *   id=habit.grazer
 *   words=It eats leaves.|It grazes.
 *   words=...
 *
 * "~" is an empty phrase, chosen on purpose. */
#ifndef GUIDE_LEXICON_H
#define GUIDE_LEXICON_H

#include <stddef.h>

/* Forget every bank. */
void lexicon_init(void);
/* Read a file of banks: the problems found (0: clean), each on stderr. A
 * file that can't be opened is one problem, and every compose then gives
 * the empty string. */
int lexicon_load(const char *path);
/* Banks loaded so far */
int lexicon_banks(void);

/* Text from bank `id` (tag: NULL or "" for none), its references expanded. */
void lexicon_compose(char *out, size_t n, const char *id, const char *tag, unsigned seed);
/* How many phrases a lookup chooses from (0: nothing written for it). */
int lexicon_count(const char *id, const char *tag);

/* {key} says value from now on (value NULL or "": unbound). */
void lexicon_set_var(const char *key, const char *value);
void lexicon_clear_vars(void);

#endif
