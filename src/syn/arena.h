/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/arena.h, the SYN edition of its C port
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

/* The arena: worlds run as fast as the processor allows, one after another
 * on every core, as a ladder that winners climb.
 *
 * A round is a world made from the parameters (and the arena's map, which
 * sets the water's shape) with its own seed, run for so many time units by
 * the simulation unchanged, in a worker: this program again
 * (--arena-worker), given the parameters, the seed and the fighters'
 * codes, and telling what happens as it goes. Any round can be made again
 * in the window from what the arena keeps of it (syn_arena_lane_round,
 * syn/record.h): the same world, frame for frame.
 *
 * The ladder: a qualifying round is a world as world_genesis makes it
 * with that seed, newcomers only, and its winner goes on the ladder at
 * level 1. A round of level L puts so many beings of level L (the seats)
 * in one world, each its own bloodline from as many copies as the others,
 * with a share of newcomers if wild_pct asks for one. The bloodline that
 * wins goes up to level L + 1: the fighter itself, or, if its bloodline
 * won as a new species, that offspring in its place; a newcomer that wins
 * goes up instead of them all. The rest are knocked out. The arena always
 * fights the highest level that has enough beings waiting, those who
 * reached it first, else qualifies another.
 *
 * The family tree (syn/family.h) runs beside it, so the round's species
 * are named and its bloodlines known: every first organism is a root,
 * each fighter's copies one bloodline. The winner is the bloodline with
 * the most living at the end (or when only one lives: sudden death), and
 * the being kept is its species with the most living, as the living
 * member of it with the most children carries it.
 *
 * The ladder is kept in the configuration folder (arena.board), with each
 * being's settings: the parameters as its round ran them, hashed, so
 * beings only meet in worlds of their own settings. */
#ifndef SYN_ARENA_H
#define SYN_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim/genetic_code.h"
#include "sim/params.h"

#define ARENA_SLOTS 8   /* fighters in a round at most */
#define ARENA_SERIES 8  /* bloodlines a lane's chart follows (the fighters' first), then "other" */
#define ARENA_LANES 32  /* workers at most */
#define ARENA_FEED 32   /* the newest beings named, across the lanes */
#define ARENA_BOARD 1000 /* entries kept */

/* the parameters' own world; the default box, and four and nine times its
 * surface; a pond and a cube */
typedef enum { MAP_PARAMS, MAP_SMALL, MAP_MEDIUM, MAP_LARGE, MAP_POND, MAP_CUBE, MAP_COUNT } ArenaMap;

typedef struct {
    int units;    /* time units a round (256 frames each) */
    int workers;
    int wild_pct; /* of a ladder round's first organisms, the newcomers */
    int seats;    /* fighters in a ladder round (0: qualifying rounds only) */
    int map;      /* ArenaMap */
    int target;   /* rounds to run, 0: until stopped */
    bool knockout; /* sudden death: a round ends when only one bloodline lives */
    int64_t seed; /* round c runs with seed + c */
} ArenaSettings;

/* where a being on the ladder stands */
typedef enum {
    ENTRY_IN,  /* in the running: waiting for a round of its level, or in one */
    ENTRY_OUT, /* knocked out */
    ENTRY_ON,  /* its bloodline won, and its offspring went up in its place */
} ArenaState;

typedef struct {
    uint32_t uid;
    uint32_t parent; /* the fighter whose bloodline it won in (0: a newcomer) */
    char name[40];   /* its species, as named in its round: "VELORAX-3" */
    char line[40];   /* its bloodline: "VELORAX" */
    char plan[160];  /* "7 arms; 8 segments per arm: ..." */
    int cycle;       /* the round it won to get on the ladder */
    int64_t seed;
    int wins;        /* rounds its bloodline won, the first included */
    int entered;     /* ladder rounds it fought in */
    float share;     /* of the living when it was found */
    float best;      /* the most its bloodline has held at a round's end */
    int alive, pop, t_end;
    uint64_t settings;
    int map;
    int start;       /* the level it went on the ladder at */
    int level;       /* its rung: 1 once qualified, one more a win */
    int state;       /* ArenaState */
    uint32_t by;     /* who knocked it out, or went up in its place (0: no one) */
    int last;        /* the round it reached its level in, or went out in */
    GeneticCode *gc;
    uint8_t *bgg; /* its code as a .bgg file */
    size_t bgg_n;
} ArenaEntry;

typedef struct {
    int t;
    int pop;
    float n[ARENA_SERIES + 1]; /* the followed lines, then the rest */
} ArenaSample;

typedef struct {
    bool up;   /* the worker is running */
    bool busy; /* on a round */
    int cycle;
    int level; /* the round's: 0 qualifying */
    int64_t seed;
    int units, t, pop, lines;
    double fps; /* frames a second, over the round so far */
    int nslots;
    char slot_name[ARENA_SLOTS][40];
    int slot_alive[ARENA_SLOTS];
    /* the chart: the lines followed, in the order they were first in the top */
    int nseries;
    uint32_t series_root[ARENA_SERIES];
    char series_label[ARENA_SERIES][40];
    int series_slot[ARENA_SERIES]; /* the fighter it is (-1: a newcomer's bloodline) */
    ArenaSample *samp;
    int nsamp, samp_cap;
    /* the species with the most living */
    uint32_t leader;
    char leader_name[40];
    int leader_alive;
    GeneticCode *leader_gc;
} ArenaLane;

typedef struct {
    char name[40];
    int cycle, alive, lane;
    GeneticCode *gc;
} ArenaBeing;

typedef enum {
    OUT_QUALIFIED, /* a qualifying round's winner, on the ladder at level 1 */
    OUT_ADVANCED,  /* a fighter won, and went up a level */
    OUT_EVOLVED,   /* a fighter's bloodline won as its offspring, which went up */
    OUT_UPSET,     /* a newcomer beat the fighters, and went up in their place */
    OUT_EXTINCT,   /* no one survived */
    OUT_KINDS
} ArenaOutcome;

typedef struct {
    int cycle;
    int level;   /* the round's: 0 qualifying */
    int outcome; /* ArenaOutcome */
    float share;
    int t_end;
    bool knockout;
    int nout;     /* fighters knocked out */
    uint32_t uid; /* the being that went up (0: none) */
    char name[40];
} ArenaResult;

/* The ladder and settings of this simulation, from the configuration
 * folder (once; later calls do nothing). */
void syn_arena_init(void);
/* Before syn_arena_init: keep the ladder in this file instead (NULL: in
 * memory only, as screenshots do). */
void syn_arena_set_board(const char *path);
void syn_arena_free(void);
ArenaSettings *syn_arena_settings(void);
/* After a change to the settings: kept with the ladder. */
void syn_arena_settings_changed(void);

/* Starts the workers and keeps them busy until stopped or the target is
 * reached. False, with a reason, if no worker could start. */
bool syn_arena_start(char *err, size_t n);
void syn_arena_stop(void);
bool syn_arena_running(void);
/* Reads what the workers have said and gives them their next rounds: call
 * often (the window: every frame). Returns true if anything changed. */
bool syn_arena_poll(void);

/* The parameters a round runs with now (the map applied), hashed, and the
 * world's size and first organisms. */
uint64_t syn_arena_fingerprint(void);
void syn_arena_world(int *w, int *h, int *d, int *organisms);
const char *syn_arena_map_name(int map);
int syn_arena_cores(void);

int syn_arena_lane_count(void);
const ArenaLane *syn_arena_lane(int i);
/* The beings with these settings, best first (rank from 0): the highest
 * level, those in the running before those out, the most wins. */
int syn_arena_count(void);
const ArenaEntry *syn_arena_at(int rank);
/* The highest level of those in the running with these settings (0: none
 * yet). */
int syn_arena_top_level(void);
/* The being is fighting in a round now. */
bool syn_arena_in_round(uint32_t uid);
/* Where it stands, in words: "in the running", "knocked out by VELORAX-3,
 * round 812". */
void syn_arena_standing(const ArenaEntry *e, char *out, size_t n);
/* The beings kept under other settings. */
int syn_arena_others(void);
const ArenaEntry *syn_arena_find(uint32_t uid);
/* Newest first. */
int syn_arena_feed_count(void);
const ArenaBeing *syn_arena_feed(int i);
int syn_arena_result_count(void);
const ArenaResult *syn_arena_result(int i);
/* Rounds done (all time) and this run's: rounds, seconds, frames. */
int syn_arena_cycles(void);
void syn_arena_run_stats(int *cycles, double *seconds, double *frames);
/* How long a round has taken with these settings, on average, in this
 * program's runs so far (0: none has ended yet). */
double syn_arena_round_seconds(void);

/* The ladder printed into dir: each being's code (NN-NAME.bgg, which the
 * original opens), and winners.txt. Returns how many were written; what
 * happened in msg. */
int syn_arena_print(const char *dir, char *msg, size_t n);
/* Forget the beings with these settings. */
void syn_arena_clear(void);
/* A being from outside the arena (the library, a .bgg file) put on the
 * ladder at level 1, under the settings as they are now, to wait for a
 * round of its level as a qualifier's winner does; its name is given.
 * NULL, with why, if it can't be (the ladder is another simulation's, or
 * full of beings fighting). */
const ArenaEntry *syn_arena_enter(const GeneticCode *gc, const char *name, char *err, size_t n);

const char *syn_outcome_name(int outcome);

/* What it takes to make a round's world again (round_begin, syn/round.h):
 * its parameters, seed, share of newcomers and fighters' codes (held). */
typedef struct {
    Params params;
    int64_t seed;
    int wild, units, nf, cycle, level;
    GeneticCode *code[ARENA_SLOTS];
    char name[ARENA_SLOTS][40];
} ArenaRoundSpec;
/* The round a lane is fighting now; false if it is on none. */
bool syn_arena_lane_round(int lane, ArenaRoundSpec *out);
/* A round of the record (syn/record.h); false if it didn't keep enough. */
bool syn_arena_record_round(int i, ArenaRoundSpec *out);
void syn_arena_round_spec_free(ArenaRoundSpec *s);

/* syn-biogenesis-3d --arena [options]: rounds without a window, their
 * winners printed as they come. */
int syn_arena_main(int argc, char **argv);
/* syn-biogenesis-3d --arena-worker: what the arena runs. */
int syn_arena_worker(int argc, char **argv);

#endif
