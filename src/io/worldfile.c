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
#define VERSION 1

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

/* the parameters, every field in order: an int or a double each */
#define PI_(f) {'i', offsetof(Params, f)}
#define PD_(f) {'d', offsetof(Params, f)}
static const struct {
    char type;
    size_t off;
} k_params[] = {
    PI_(initial_organisms), PD_(initial_o2), PD_(initial_co2), PI_(world_width),
    PI_(world_height), PI_(world_depth), PD_(segment_radius), PI_(max_age), PD_(rubbing),
    PD_(mutation_rate), PI_(segment_cost_divisor), PD_(organic_obtained_energy),
    PI_(green_obtained_energy_divisor), PD_(organic_subs_produced), PD_(red_energy_consumption),
    PD_(green_energy_consumption), PD_(blue_energy_consumption), PD_(cyan_energy_consumption),
    PD_(white_energy_consumption), PD_(gray_energy_consumption), PD_(yellow_energy_consumption),
    PI_(red_prob), PI_(green_prob), PI_(blue_prob), PI_(cyan_prob), PI_(white_prob),
    PI_(gray_prob), PI_(yellow_prob), PI_(pitch_prob), PI_(drain_subs_divisor),
    PI_(initial_energy), PD_(max_vel), PD_(max_rot), PD_(elasticity), PD_(decay_energy),
};
#define NPARAMS ((int)(sizeof k_params / sizeof k_params[0]))

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

bool worldfile_save(const World *w, const char *path, char *err, int errlen)
{
    Buf o = {0};
    put(&o, MAGIC, 4);
    put_i32(&o, VERSION);
    put_i32(&o, NPARAMS);
    for (int i = 0; i < NPARAMS; i++) {
        const char *base = (const char *)&P + k_params[i].off;
        if (k_params[i].type == 'i')
            put_f64(&o, (double)*(const int *)base);
        else
            put_f64(&o, *(const double *)base);
    }
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

    Codes t = {0};
    for (int i = 0; i < w->norgs; i++) {
        code_index(&t, w->orgs[i]->gc, true);
        code_index(&t, w->orgs[i]->infected_gc, true);
    }
    put_i32(&o, t.n);
    for (int i = 0; i < t.n; i++) {
        const GeneticCode *gc = t.c[i];
        put_i32(&o, (int32_t)gc->lineage);
        put_i32(&o, gc->symmetry);
        put_i32(&o, gc->mirror);
        put_u8(&o, gc->disperse_children);
        put_i32(&o, gc->reproduce_energy);
        put_i32(&o, gc->max_age);
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
        const int32_t ints[13] = {g->n_children,      g->parent_id,   g->id,
                                  g->generation,      g->infected_generation,
                                  g->total_children,  g->total_kills, g->total_infected,
                                  g->growth_ratio,    g->age,         g->frames_color,
                                  g->time_to_reproduce, g->has_grown};
        for (int k = 0; k < 13; k++)
            put_i32(&o, ints[k]);
        put_v3(&o, g->pos);
        put_q(&o, g->q);
        put_v3(&o, g->v);
        put_v3(&o, g->w);
        put_v3(&o, g->prev_pos);
        put_q(&o, g->prev_q);
        put_f64(&o, g->energy);
        put_u8(&o, g->flash_color);
        put_u8(&o, g->has_moved);
        put_u8(&o, g->alive);
    }
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
    if (version != VERSION) {
        snprintf(err, (size_t)errlen, "%s: a world of version %d, not %d", path, version, VERSION);
        goto out;
    }
    int np = get_i32(&in);
    if (np != NPARAMS) {
        snprintf(err, (size_t)errlen, "%s: %d parameters, not %d", path, np, NPARAMS);
        goto out;
    }
    for (int i = 0; i < NPARAMS; i++) {
        double v = get_f64(&in);
        char *base = (char *)&P + k_params[i].off;
        if (k_params[i].type == 'i')
            *(int *)base = (int)v;
        else
            *(double *)base = v;
    }
    world_init(&nw);
    nw.width = get_i32(&in);
    nw.height = get_i32(&in);
    nw.depth = get_i32(&in);
    nw.frame = (int64_t)get_u64(&in);
    nw.o2 = get_f64(&in);
    nw.co2 = get_f64(&in);
    nw.lost = get_f64(&in);
    nw.next_id = get_i32(&in);
    nw.population = get_i32(&in);
    int64_t *counts[6] = {&nw.counts.births,     &nw.counts.deaths, &nw.counts.kills,
                          &nw.counts.infections, &nw.counts.bites,  &nw.counts.touches};
    for (int i = 0; i < 6; i++)
        *counts[i] = (int64_t)get_u64(&in);
    nw.counts.eaten = get_f64(&in);
    uint64_t dice = get_u64(&in);

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
        int reproduce = get_i32(&in), max_age = get_i32(&in), ng = get_i32(&in);
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
        int32_t ints[13];
        for (int k = 0; k < 13; k++)
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
        g->pos = get_v3(&in);
        g->q = get_q(&in);
        g->v = get_v3(&in);
        g->w = get_v3(&in);
        g->prev_pos = get_v3(&in);
        g->prev_q = get_q(&in);
        g->energy = get_f64(&in);
        g->flash_color = (uint8_t)get_u8(&in);
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
