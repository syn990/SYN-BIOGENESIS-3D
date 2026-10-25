/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/record.h, the SYN edition of its C port
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

/* The arena's record: every round ever fought, kept for good, where the
 * ladder (syn/arena.h) keeps only who stands where.
 *
 * Each round: its seed, level, settings and outcome; when one bloodline
 * was left alive (the lone bloodline); the winning bloodline's genesis
 * being, the code it began the round as (a fighter, or a newcomer that
 * world_genesis made from the seed), and its final being, the one the
 * round ended with and the ladder kept; the fighters and how many of each
 * lived; and the living and bloodlines through the round.
 *
 * Kept in the configuration folder (arena.record), a round at a time on
 * the end of the file, each written whole, so two programs can add to it
 * at once. A round also keeps the parameters it ran with, by name, so the
 * window can make its world again and show it (round_begin, syn/round.h). */
#ifndef SYN_RECORD_H
#define SYN_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim/genetic_code.h"

#define REC_FIGHTERS 8
#define REC_CURVE 128 /* points of a round's curve at most */

typedef enum {
    REC_LIVE,     /* recorded as it was fought */
    REC_REPLAYED, /* from before the record: replayed from its seed, and the
                   * replay ended with the being the ladder kept */
    REC_LADDER,   /* from before the record: read from the ladder alone */
} RecordSource;

typedef struct {
    uint32_t uid;
    char name[40];
    int alive; /* of its bloodline at the end (-1: not known) */
    uint8_t *bgg; /* its code as a .bgg file, to put the round together again */
    size_t bgg_n;
} RecordFighter;

typedef struct {
    int t, pop, lines;
} RecordPoint;

typedef struct {
    int source;   /* RecordSource */
    int64_t when; /* seconds since 1970 it was recorded */
    int cycle, level, outcome; /* outcome: ArenaOutcome */
    int64_t seed;
    uint64_t settings; /* the parameters' hash, as the ladder's */
    int map, units, wild;
    bool knockout;
    int t_end;  /* time units it ran */
    int t_lone; /* the first time unit with one bloodline alive (-1: never) */
    int pop, lines;
    float share; /* of the living, the winning bloodline's */
    int nf;
    RecordFighter f[REC_FIGHTERS];
    /* the final being (no code: no survivors) */
    uint32_t uid; /* on the ladder (0: none) */
    char name[40], line[40], plan[160];
    GeneticCode *final;
    /* the winning bloodline's genesis being (no code: not known) */
    char gname[40], gplan[160];
    GeneticCode *genesis;
    /* the codes as .bgg files */
    uint8_t *fbgg, *gbgg;
    size_t fbgg_n, gbgg_n;
    int ncurve;
    RecordPoint curve[REC_CURVE];
    /* the parameters it ran with, as params_text writes them (NULL: not
     * kept) */
    char *params;
} RecordRound;

/* Before the first use: the file (NULL: in memory only). */
void syn_record_set_file(const char *path);
/* Reads the file, once. */
void syn_record_init(void);
void syn_record_free(void);

/* Added and written on the end of the file, with the final and genesis
 * beings' codes as .bgg files (NULL: none); r's own codes and files are
 * not read, but its fighters' .bgg files and its parameters are copied. */
void syn_record_add(const RecordRound *r, const uint8_t *fbgg, size_t fn, const uint8_t *gbgg,
                    size_t gn);
/* Whether a round with this seed and cycle is in it already. */
bool syn_record_has(int64_t seed, int cycle);

/* All of them, in the order they were recorded. */
int syn_record_count(void);
const RecordRound *syn_record_at(int i);
/* Changes with every round added. */
unsigned syn_record_version(void);

/* How far the final being is from the genesis being: the genes that
 * differ, place by place, plus those one has and the other hasn't, plus
 * one if the arms, mirroring or the like changed (-1: either not known). */
int syn_record_changes(const RecordRound *r);

#endif
