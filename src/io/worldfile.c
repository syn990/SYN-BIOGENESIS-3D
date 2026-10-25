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

#include "io/worldfile.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/params.h"

#define MAGIC "B3W"
/* 3: with 0.9's statistics (version 2's open, their statistics begun anew) */
#define VERSION 4
/* Version 4: the methane in the water, each code's height and body plan,
 * purple among the colours (brown 8, not 7), and the parameters that came
 * with them.
 * Versions 2 and 3 are read as they were with those worked out: no
 * methane, each code's height from its light drive, the new parameters
 * their defaults. */

/* ---- writing ---- */

typedef struct {
    unsigned char *b;
    size_t n, cap;
} Buf;

static void put(Buf *o, const void *p, size_t n)
{
    if (o->n + n > o->cap) {
        o->cap = (o->n + n) * 2 + 4096;
        o->b = realloc(o->b, o->cap);
        if (!o->b)
            abort();
    }
    memcpy(o->b + o->n, p, n);
    o->n += n;
}

static void put_u64(Buf *o, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (unsigned char)(v >> (8 * i));
    put(o, b, 8);
}

static void put_i32(Buf *o, int32_t v)
{
    uint32_t u = (uint32_t)v;
    unsigned char b[4] = {(unsigned char)u, (unsigned char)(u >> 8), (unsigned char)(u >> 16),
                          (unsigned char)(u >> 24)};
    put(o, b, 4);
}

static void put_f64(Buf *o, double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    put_u64(o, u);
}

static void put_u8(Buf *o, unsigned v)
{
    unsigned char c = (unsigned char)v;
    put(o, &c, 1);
}

static void put_v3(Buf *o, V3 v)
{
    put_f64(o, v.x);
    put_f64(o, v.y);
    put_f64(o, v.z);
}

static void put_q(Buf *o, Quat q)
{
    put_f64(o, q.w);
    put_f64(o, q.x);
    put_f64(o, q.y);
    put_f64(o, q.z);
}

#define NPARAMS k_n_param_fields

/* every code an organism carries or will pass on, once each */
typedef struct {
    const GeneticCode **c;
    int n, cap;
} Codes;

static int code_index(Codes *t, const GeneticCode *gc, bool add)
{
    if (!gc)
        return -1;
    for (int i = t->n - 1; i >= 0; i--)
        if (t->c[i] == gc)
            return i;
    if (!add)
        return -1;
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->c = realloc(t->c, (size_t)t->cap * sizeof *t->c);
        if (!t->c)
            abort();
    }
    t->c[t->n] = gc;
    return t->n++;
}

/* the statistics' codes, in the table; their figures */
static void stats_codes(const WorldStats *s, const GeneticCode **out)
{
    const GeneticCode *c[9] = {s->alive_most_children, s->alive_most_kills, s->alive_most_infections,
                               s->most_children,       s->most_kills,       s->most_infections,
                               s->last_born,           s->last_dead,        s->last_infected};
    memcpy(out, c, sizeof c);
}

static void put_series(Buf *o, const StatsSeries *s)
{
    put_i32(o, s->n);
    for (int i = 0; i < s->n; i++)
        put_f64(o, s->v[i]);
}

static void put_stats(Buf *o, const WorldStats *s, Codes *t)
{
    const int64_t i64[12] = {s->time,           s->max_population_time, s->min_population_time,
                             s->population_sum, s->death_sum,           s->birth_sum,
                             s->infections_sum, s->max_oxygen_time,     s->min_oxygen_time,
                             s->max_co2_time,   s->min_co2_time,        s->most_children_time};
    for (int i = 0; i < 12; i++)
        put_u64(o, (uint64_t)i64[i]);
    put_u64(o, (uint64_t)s->most_kills_time);
    put_u64(o, (uint64_t)s->most_infections_time);
    const int32_t i32[14] = {s->max_population,        s->max_births,          s->max_deaths,
                             s->min_population,        s->mass_extinctions,    s->mass_extinction_state,
                             s->created_organisms,     s->death_last_time,     s->birth_last_time,
                             s->alive_most_children_n, s->alive_most_kills_n,  s->alive_most_infections_n,
                             s->most_children_n,       s->most_kills_n};
    for (int i = 0; i < 14; i++)
        put_i32(o, i32[i]);
    put_i32(o, s->most_infections_n);
    put_f64(o, s->max_oxygen);
    put_f64(o, s->min_oxygen);
    put_f64(o, s->max_co2);
    put_f64(o, s->min_co2);
    const GeneticCode *c[9];
    stats_codes(s, c);
    for (int i = 0; i < 9; i++)
        put_i32(o, code_index(t, c[i], false));
    put_series(o, &s->population);
    put_series(o, &s->deaths);
    put_series(o, &s->births);
    put_series(o, &s->oxygen);
    put_series(o, &s->co2);
}

bool worldfile_save(const World *w, const char *path, char *err, int errlen)
{
    Buf o = {0};
    put(&o, MAGIC, 4);
    put_i32(&o, VERSION);
    put_i32(&o, NPARAMS);
    for (int i = 0; i < NPARAMS; i++)
        put_f64(&o, params_get(&P, &k_param_fields[i]));
    put_i32(&o, w->width);
    put_i32(&o, w->height);
    put_i32(&o, w->depth);
    put_u64(&o, (uint64_t)w->frame);
    put_f64(&o, w->o2);
    put_f64(&o, w->co2);
    put_f64(&o, w->lost);
    put_i32(&o, w->next_id);
    put_i32(&o, w->population);
    const int64_t counts[6] = {w->counts.births, w->counts.deaths,     w->counts.kills,
                               w->counts.infections, w->counts.bites, w->counts.touches};
    for (int i = 0; i < 6; i++)
        put_u64(&o, (uint64_t)counts[i]);
    put_f64(&o, w->counts.eaten);
    put_u64(&o, g_rng.seed);
    put_i32(&o, w->fx);
    put_i32(&o, w->fy);
    put_i32(&o, w->fz);
    for (int i = 0; i < world_n_cells(w); i++)
        put_f64(&o, w->co2_cell[i]);
    put_f64(&o, w->ch4);
    for (int i = 0; i < world_n_cells(w); i++)
        put_f64(&o, w->ch4_cell[i]);

    Codes t = {0};
    for (int i = 0; i < w->norgs; i++) {
        code_index(&t, w->orgs[i]->gc, true);
        code_index(&t, w->orgs[i]->infected_gc, true);
    }
    const GeneticCode *sc[9];
    stats_codes(&w->stats, sc);
    for (int i = 0; i < 9; i++)
        code_index(&t, sc[i], true);
    put_i32(&o, t.n);
    for (int i = 0; i < t.n; i++) {
        const GeneticCode *gc = t.c[i];
        put_i32(&o, (int32_t)gc->lineage);
        put_i32(&o, gc->symmetry);
        put_i32(&o, gc->mirror);
        put_u8(&o, gc->disperse_children);
        put_i32(&o, gc->reproduce_energy);
        put_i32(&o, gc->max_age);
        for (int d = 0; d < DRIVE_COUNT; d++)
            put_f64(&o, gc->drive[d]);
        put_f64(&o, gc->height);
        put_u8(&o, gc->plan);
        put_f64(&o, gc->cone);
        put_i32(&o, gc->ngenes);
        for (int k = 0; k < gc->ngenes; k++) {
            put_f64(&o, gc->genes[k].length);
            put_f64(&o, gc->genes[k].theta);
            put_f64(&o, gc->genes[k].pitch);
            put_u8(&o, gc->genes[k].color);
        }
    }
    put_i32(&o, w->norgs);
    for (int i = 0; i < w->norgs; i++) {
        const Organism *g = w->orgs[i];
        put_i32(&o, code_index(&t, g->gc, false));
        put_i32(&o, code_index(&t, g->infected_gc, false));
        const int32_t ints[15] = {g->n_children,      g->parent_id,   g->id,
                                  g->generation,      g->infected_generation,
                                  g->total_children,  g->total_kills, g->total_infected,
                                  g->growth_ratio,    g->age,         g->frames_color,
                                  g->time_to_reproduce, g->has_grown, g->founder,
                                  g->infected_founder};
        for (int k = 0; k < 15; k++)
            put_i32(&o, ints[k]);
        put_v3(&o, g->pos);
        put_q(&o, g->q);
        put_v3(&o, g->v);
        put_v3(&o, g->w);
        put_v3(&o, g->prev_pos);
        put_q(&o, g->prev_q);
        put_v3(&o, g->aim);
        put_f64(&o, g->energy);
        put_u8(&o, g->flash_color);
        put_u8(&o, g->has_moved);
        put_u8(&o, g->alive);
    }
    put_stats(&o, &w->stats, &t);
    free(t.c);

    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(o.b, 1, o.n, f) == o.n;
    if (f && fclose(f) != 0)
        ok = false;
    free(o.b);
    if (!ok)
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
    return ok;
}

/* ---- reading ---- */

typedef struct {
    const unsigned char *b;
    size_t n, at;
    bool bad;
} In;

static const unsigned char *take(In *in, size_t n)
{
    if (in->bad || in->at + n > in->n) {
        in->bad = true;
        return NULL;
    }
    const unsigned char *p = in->b + in->at;
    in->at += n;
    return p;
}

static uint64_t get_u64(In *in)
{
    const unsigned char *p = take(in, 8);
    uint64_t v = 0;
    for (int i = 0; p && i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static int32_t get_i32(In *in)
{
    const unsigned char *p = take(in, 4);
    if (!p)
        return 0;
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                     (uint32_t)p[3] << 24);
}

static double get_f64(In *in)
{
    uint64_t u = get_u64(in);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static unsigned get_u8(In *in)
{
    const unsigned char *p = take(in, 1);
    return p ? *p : 0;
}

static V3 get_v3(In *in)
{
    V3 v;
    v.x = get_f64(in);
    v.y = get_f64(in);
    v.z = get_f64(in);
    return v;
}

static Quat get_q(In *in)
{
    Quat q;
    q.w = get_f64(in);
    q.x = get_f64(in);
    q.y = get_f64(in);
    q.z = get_f64(in);
    return q;
}

static unsigned char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    unsigned char *b = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 65536 > cap) {
            cap = (len + 65536) * 2;
            unsigned char *nb = realloc(b, cap);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
        }
        size_t got = fread(b + len, 1, 65536, f);
        len += got;
        if (got < 65536)
            break;
    }
    fclose(f);
    *n = len;
    return b;
}

static bool get_series(In *in, StatsSeries *s)
{
    s->n = get_i32(in);
    if (in->bad || s->n < 0 || s->n > STATS_HISTORY)
        return false;
    for (int i = 0; i < s->n; i++)
        s->v[i] = get_f64(in);
    return !in->bad;
}

static bool get_stats(In *in, WorldStats *s, GeneticCode **codes, int ncodes)
{
    memset(s, 0, sizeof *s);
    int64_t *i64[14] = {&s->time,           &s->max_population_time, &s->min_population_time,
                        &s->population_sum, &s->death_sum,           &s->birth_sum,
                        &s->infections_sum, &s->max_oxygen_time,     &s->min_oxygen_time,
                        &s->max_co2_time,   &s->min_co2_time,        &s->most_children_time,
                        &s->most_kills_time, &s->most_infections_time};
    for (int i = 0; i < 14; i++)
        *i64[i] = (int64_t)get_u64(in);
    int *i32[15] = {&s->max_population,        &s->max_births,         &s->max_deaths,
                    &s->min_population,        &s->mass_extinctions,   &s->mass_extinction_state,
                    &s->created_organisms,     &s->death_last_time,    &s->birth_last_time,
                    &s->alive_most_children_n, &s->alive_most_kills_n, &s->alive_most_infections_n,
                    &s->most_children_n,       &s->most_kills_n,       &s->most_infections_n};
    for (int i = 0; i < 15; i++)
        *i32[i] = get_i32(in);
    s->max_oxygen = get_f64(in);
    s->min_oxygen = get_f64(in);
    s->max_co2 = get_f64(in);
    s->min_co2 = get_f64(in);
    GeneticCode **c[9] = {&s->alive_most_children, &s->alive_most_kills, &s->alive_most_infections,
                          &s->most_children,       &s->most_kills,       &s->most_infections,
                          &s->last_born,           &s->last_dead,        &s->last_infected};
    for (int i = 0; i < 9; i++) {
        int k = get_i32(in);
        if (in->bad || k < -1 || k >= ncodes)
            return false;
        *c[i] = k >= 0 ? gc_ref(codes[k]) : NULL;
    }
    return get_series(in, &s->population) && get_series(in, &s->deaths) && get_series(in, &s->births) &&
           get_series(in, &s->oxygen) && get_series(in, &s->co2);
}

bool worldfile_load(World *w, const char *path, char *err, int errlen)
{
    size_t n = 0;
    unsigned char *data = read_all(path, &n);
    if (!data) {
        snprintf(err, (size_t)errlen, "%s: cannot read", path);
        return false;
    }
    In in = {data, n, 0, false};
    Params saved = P;
    World nw;
    memset(&nw, 0, sizeof nw);
    GeneticCode **codes = NULL;
    int ncodes = 0;
    bool ok = false;
    const unsigned char *magic = take(&in, 4);
    if (!magic || memcmp(magic, MAGIC, 4)) {
        snprintf(err, (size_t)errlen, "%s: not a SYN-BIOGENESIS-3D world", path);
        goto out;
    }
    int version = get_i32(&in);
    if (version < 2 || version > VERSION) {
        snprintf(err, (size_t)errlen, "%s: a world of version %d, not %d", path, version, VERSION);
        goto out;
    }
    int np = get_i32(&in);
    if (version >= 4 ? np != NPARAMS : np < 1 || np > NPARAMS) {
        snprintf(err, (size_t)errlen, "%s: %d parameters, not %d", path, np, NPARAMS);
        goto out;
    }
    /* those an older version didn't have: their defaults */
    Params defaults;
    params_set_defaults(&defaults);
    for (int i = 0; i < NPARAMS; i++)
        params_put(&P, &k_param_fields[i], i < np ? get_f64(&in) : params_get(&defaults, &k_param_fields[i]));
    world_init(&nw);
    nw.width = get_i32(&in);
    nw.height = get_i32(&in);
    nw.depth = get_i32(&in);
    nw.frame = (int64_t)get_u64(&in);
    nw.o2 = get_f64(&in);
    double co2 = get_f64(&in);
    nw.lost = get_f64(&in);
    nw.next_id = get_i32(&in);
    nw.population = get_i32(&in);
    int64_t *counts[6] = {&nw.counts.births,     &nw.counts.deaths, &nw.counts.kills,
                          &nw.counts.infections, &nw.counts.bites,  &nw.counts.touches};
    for (int i = 0; i < 6; i++)
        *counts[i] = (int64_t)get_u64(&in);
    nw.counts.eaten = get_f64(&in);
    uint64_t dice = get_u64(&in);
    if (in.bad || nw.width < 1 || nw.height < 1 || nw.depth < 1)
        goto bad;
    world_reset_water(&nw, 0);
    int fx = get_i32(&in), fy = get_i32(&in), fz = get_i32(&in);
    if (in.bad || fx != nw.fx || fy != nw.fy || fz != nw.fz)
        goto bad;
    for (int i = 0; i < world_n_cells(&nw); i++)
        nw.co2_cell[i] = get_f64(&in);
    nw.co2 = co2;
    if (version >= 4) {
        nw.ch4 = get_f64(&in);
        for (int i = 0; i < world_n_cells(&nw); i++)
            nw.ch4_cell[i] = get_f64(&in);
    }

    ncodes = get_i32(&in);
    if (in.bad || ncodes < 0 || ncodes > 10000000)
        goto bad;
    codes = calloc((size_t)ncodes + 1, sizeof *codes);
    if (!codes)
        goto bad;
    for (int i = 0; i < ncodes; i++) {
        uint32_t lineage = (uint32_t)get_i32(&in);
        int symmetry = get_i32(&in), mirror = get_i32(&in);
        bool disperse = get_u8(&in) != 0;
        int reproduce = get_i32(&in), max_age = get_i32(&in);
        double drive[DRIVE_COUNT];
        for (int d = 0; d < DRIVE_COUNT; d++)
            drive[d] = get_f64(&in);
        double height = version >= 4 ? get_f64(&in)
                                     : gc_height_of_light_drive(drive[DRIVE_LIGHT], nw.depth, P.light_depth);
        int plan = version >= 4 ? get_u8(&in) : PLAN_DISK;
        double cone = version >= 4 ? get_f64(&in) : 0;
        int ng = get_i32(&in);
        if (in.bad || ng < 1 || ng > 100000 || symmetry < 1 || symmetry > 8)
            goto bad;
        Gene *genes = calloc((size_t)ng, sizeof *genes);
        if (!genes)
            goto bad;
        for (int k = 0; k < ng; k++) {
            genes[k].length = get_f64(&in);
            genes[k].theta = get_f64(&in);
            genes[k].pitch = get_f64(&in);
            genes[k].color = (uint8_t)get_u8(&in);
            if (genes[k].color >= COL_BROWN)
                in.bad = true;
        }
        if (in.bad) {
            free(genes);
            goto bad;
        }
        codes[i] = gc_new_from_genes(genes, ng, symmetry, mirror, disperse);
        free(genes);
        codes[i]->lineage = lineage;
        codes[i]->reproduce_energy = reproduce;
        codes[i]->max_age = max_age;
        memcpy(codes[i]->drive, drive, sizeof drive);
        codes[i]->height = bio_between(height, 0, 1);
        codes[i]->plan = (uint8_t)(plan < PLAN_COUNT ? plan : PLAN_DISK);
        codes[i]->cone = bio_between(cone, -D_PI / 2, D_PI / 2);
    }
    int norgs = get_i32(&in);
    if (in.bad || norgs < 0 || norgs > 10000000)
        goto bad;
    for (int i = 0; i < norgs; i++) {
        int ci = get_i32(&in), ii = get_i32(&in);
        if (in.bad || ci < 0 || ci >= ncodes || ii < -1 || ii >= ncodes)
            goto bad;
        Organism *g = org_new_bare(&nw, codes[ci]);
        world_append_raw(&nw, g);
        if (ii >= 0)
            g->infected_gc = gc_ref(codes[ii]);
        int32_t ints[15];
        for (int k = 0; k < 15; k++)
            ints[k] = get_i32(&in);
        g->n_children = ints[0];
        g->parent_id = ints[1];
        g->id = ints[2];
        g->generation = ints[3];
        g->infected_generation = ints[4];
        g->total_children = ints[5];
        g->total_kills = ints[6];
        g->total_infected = ints[7];
        g->growth_ratio = ints[8];
        g->age = ints[9];
        g->frames_color = ints[10];
        g->time_to_reproduce = ints[11];
        g->has_grown = ints[12];
        g->founder = ints[13];
        g->infected_founder = ints[14];
        g->pos = get_v3(&in);
        g->q = get_q(&in);
        g->v = get_v3(&in);
        g->w = get_v3(&in);
        g->prev_pos = get_v3(&in);
        g->prev_q = get_q(&in);
        g->aim = get_v3(&in);
        g->energy = get_f64(&in);
        g->flash_color = (uint8_t)get_u8(&in);
        /* brown was 7 before purple */
        if (version < 4 && g->flash_color == 7)
            g->flash_color = COL_BROWN;
        g->has_moved = get_u8(&in) != 0;
        g->alive = get_u8(&in) != 0;
        if (in.bad || g->growth_ratio < 1 || g->growth_ratio > 16)
            goto bad;
        org_build_body(g);
        if (!g->alive)
            for (int k = 0; k < g->segments; k++)
                g->seg_color[k] = COL_BROWN;
        org_update_geometry(g);
    }
    if (version >= 3) {
        if (!get_stats(&in, &nw.stats, codes, ncodes))
            goto bad;
    } else {
        /* from before the statistics: begun from the world as it was saved */
        stats_init(&nw.stats, nw.population, nw.o2, nw.co2);
        nw.stats.time = nw.frame / 256;
        nw.stats.max_population = nw.population;
        nw.stats.birth_sum = nw.counts.births;
        nw.stats.death_sum = nw.counts.deaths;
        nw.stats.infections_sum = nw.counts.infections;
    }
    if (in.bad || in.at != in.n)
        goto bad;
    /* the world, the parameters and the dice are the file's */
    world_free(w);
    *w = nw;
    memset(&nw, 0, sizeof nw);
    for (int i = 0; i < w->norgs; i++)
        w->orgs[i]->world = w; /* they were built in the loader's world */
    g_rng.seed = dice;
    ok = true;
    goto out;
bad:
    snprintf(err, (size_t)errlen, "%s: damaged or cut short", path);
out:
    for (int i = 0; i < ncodes && codes; i++)
        gc_unref(codes[i]);
    free(codes);
    if (!ok) {
        world_free(&nw);
        P = saved;
    }
    free(data);
    return ok;
}
