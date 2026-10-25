/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/history.h, the SYN edition of its C port
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

/* The SYN edition's history of a world, for its statistics screen.
 *
 * Recorded beside the simulation, never in it: once a time unit (256
 * frames) the world is read and a sample kept, for the whole run (old
 * samples are merged in pairs as it grows). The run is cut into epochs, and
 * each epoch keeps its champions: of the organisms alive in it, the ones
 * with the most children, victims and infections over their lives, the
 * most energy, the highest generation and the heaviest. Events (records, mass
 * extinctions, colours and lines rising and dying out, what the user did)
 * go on a timeline.
 *
 * Each epoch also keeps its best species: the one with the most living at
 * once in it (syn/family.h).
 *
 * Lines: the roots of the species' trees, which syn/family.c keeps
 * through GeneticCode.lineage (copied by the simulation from parent to
 * mutated child and never read by it), named after the organism each root
 * began with. Nothing else is written to the world.
 *
 * Each sample also keeps this program's own measures of the water: kills
 * and bites, how many bodies are flat and how level they lie, and how deep
 * the leaves and the hunters live. */
#ifndef SYN_HISTORY_H
#define SYN_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim/world.h"
#include "guide/palette.h"

#define HIST_COLOURS (SYN_NCOL - 1) /* palette slots of the living: all but brown */
#define HIST_TOP 12              /* lines kept per sample, for the river */

typedef struct {
    int64_t t; /* time unit */
    float pop, corpses;
    float births, deaths, infections; /* per unit, over the sample */
    float o2, co2, ch4;
    float seg[HIST_COLOURS]; /* segments of the living, by colour */
    float gen_mean, gen_max;
    float energy_mean, mass_total, size_mean, age_mean;
    /* this program's: kills and bites per unit; the living's share of flat
     * bodies and how level they lie (1 level); the mean depth of the green
     * length, and of the red, gray and white (0 the surface, 1 the floor) */
    float kills, bites, flat, level, leaves_depth, hunters_depth;
    float lines; /* lines with a living member */
    uint32_t top_line[HIST_TOP];
    float top_n[HIST_TOP];
} HistSample;

typedef enum {
    CH_CHILDREN,
    CH_KILLS,
    CH_INFECTED,
    CH_RICHEST, /* energy (not age: every organism dies at the same max_age) */
    CH_GENERATION,
    CH_HEAVIEST,
    CH_COUNT
} ChampionKind;

typedef struct {
    double value;
    int id, generation;
    uint32_t line;
    int64_t t;       /* when it was seen with that value */
    bool died;       /* seen as it left the world */
    uint8_t colour;  /* its commonest segment colour */
    GeneticCode *gc; /* held by the history */
} Champion;

typedef struct {
    int64_t t0, t1; /* [t0, t1) in time units */
    int samples;
    int pop_min, pop_max;
    double pop_sum;
    int births, deaths, infections, extinctions;
    double seg[HIST_COLOURS]; /* summed over the samples */
    uint32_t lead_line;       /* the biggest line seen in it */
    int lead_n;
    uint32_t best;            /* its best species (syn/family.h): the most living at once */
    int best_n;
    int64_t best_t;
    Champion ch[CH_COUNT];
} Epoch;

typedef enum {
    EV_START,      /* genesis, a world opened */
    EV_RECORD,     /* population, children, victims, infections */
    EV_EXTINCTION, /* mass extinctions, colours and lines dying out */
    EV_COLOUR,     /* a colour taking the lead, coming back */
    EV_LINE,       /* a line taking the lead */
    EV_ATMOSPHERE, /* a gas running out, recovering */
    EV_USER,       /* what the user did */
    EV_KINDS
} EventKind;

typedef struct {
    int64_t t;
    EventKind kind;
    int colour;      /* a BioColor, or -1 */
    uint32_t line;   /* or 0 */
    int key;         /* events with the same key close together merge */
    int count;       /* how many merged into it */
    double value;
    char text[96];
    char detail[128];
    GeneticCode *gc; /* held: the organism the event is about, if any */
} Event;

typedef struct {
    uint32_t id;
    int founder;       /* its first organism's id */
    int64_t founded, peak_t, last_seen;
    int peak, alive;
    bool led, notable, extinct;
    uint8_t colour;    /* the founder's commonest colour */
} Line;

typedef struct History {
    const HistSample *s;
    int n;
    int stride; /* time units per sample */
    const Epoch *epochs;
    int nepochs;
    int epoch_base; /* time units per epoch */
    const Event *ev;
    int nev;
    int64_t t_start, t_now;
} History;

/* After genesis (loaded false) or after a world is opened. */
void syn_history_reset(World *w, bool loaded);
/* After every simulation frame. */
void syn_history_frame(World *w);
/* fn called with the world after each time unit's own work (NULL: none):
 * the winner's calculation compared (syn/winner.h) */
void syn_history_on_unit(void (*fn)(const World *w));
/* An organism leaving the world (World.on_remove), before it is freed. */
void syn_history_removed(const World *w, const Organism *o);
/* Something the user did, for the timeline. key merges repeats (0: none). */
void syn_history_user(const World *w, int key, double value, const char *text);
void syn_history_free(void);

const History *syn_history(void);
/* The line with this id (NULL if unknown). */
const Line *syn_history_line(uint32_t id);
/* Champions of the living right now, as the original's statistics find
 * them (without touching the world's records). */
void syn_history_alive_best(const World *w, Champion out[CH_COUNT]);
const char *syn_champion_name(ChampionKind k);
/* A value as the champion's screen shows it ("12", "1,234.5") */
void syn_champion_value(ChampionKind k, double v, char *out, size_t n);

/* An epoch covering [t0, t1) built from the base epochs, champions included
 * (borrowed, not held). Returns false when no recorded epoch overlaps it. */
bool syn_history_merge(int64_t t0, int64_t t1, Epoch *out);

/* The history as bytes, to keep beside a saved world (syn/sidecar.h):
 * malloc'd, with their length; 0 when there is none. */
size_t syn_history_save(uint8_t **out);
/* After syn_history_reset(w, true) on the world it was saved with: the
 * history and the family tree (syn_family_save's bytes) back as they were,
 * and with note, its opening on the timeline. False, with nothing
 * changed, for bytes that don't read well. */
bool syn_history_load(World *w, const uint8_t *in, size_t n, const uint8_t *family,
                      size_t family_n, bool note);

#endif
