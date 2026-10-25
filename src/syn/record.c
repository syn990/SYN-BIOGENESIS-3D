/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/record.c, the SYN edition of its C port
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

#include "syn/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "sim/params.h"
#include "syn/arena_os.h"

#define SIM_TAG 3
#define RECORD_FILE "arena.record"
#define RECORD_VERSION 1

/* The file: "SYNRECRD", the version and the simulation, then each round as
 * its length (4 bytes) and its bytes, so a round cut short at the end (the
 * program stopped while writing it) is left out and the rest still read. */

static struct {
    bool inited, path_set;
    char path[1100];
    RecordRound *r;
    int n, cap;
    unsigned version;
} R;

/* ---- bytes ---- */

typedef struct {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *v, size_t k)
{
    if (b->n + k > b->cap) {
        b->cap = (b->n + k) * 2 + 256;
        b->p = realloc(b->p, b->cap);
        if (!b->p)
            abort();
    }
    memcpy(b->p + b->n, v, k);
    b->n += k;
}

static void put_u8(Buf *b, unsigned v)
{
    uint8_t c = (uint8_t)v;
    put(b, &c, 1);
}

static void put_u32(Buf *b, uint32_t v)
{
    uint8_t q[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, q, 4);
}

static void put_i32(Buf *b, int v)
{
    put_u32(b, (uint32_t)v);
}

static void put_u64(Buf *b, uint64_t v)
{
    put_u32(b, (uint32_t)v);
    put_u32(b, (uint32_t)(v >> 32));
}

static void put_f32(Buf *b, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put_u32(b, u);
}

static void put_str(Buf *b, const char *s)
{
    size_t k = strlen(s);
    if (k > 255)
        k = 255;
    put_u8(b, (unsigned)k);
    put(b, s, k);
}

static void put_blob(Buf *b, const void *p, size_t k)
{
    put_u32(b, (uint32_t)k);
    if (k)
        put(b, p, k);
}

typedef struct {
    const uint8_t *p;
    size_t n, at;
    bool bad;
} Rd;

static const uint8_t *take(Rd *r, size_t k)
{
    if (r->bad || r->n - r->at < k) {
        r->bad = true;
        return NULL;
    }
    const uint8_t *q = r->p + r->at;
    r->at += k;
    return q;
}

static uint32_t get_u32(Rd *r)
{
    const uint8_t *q = take(r, 4);
    return q ? (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24 : 0;
}

static unsigned get_u8(Rd *r)
{
    const uint8_t *q = take(r, 1);
    return q ? q[0] : 0;
}

static int get_i32(Rd *r)
{
    return (int)get_u32(r);
}

static uint64_t get_u64(Rd *r)
{
    uint64_t lo = get_u32(r);
    return lo | (uint64_t)get_u32(r) << 32;
}

static float get_f32(Rd *r)
{
    uint32_t u = get_u32(r);
    float v;
    memcpy(&v, &u, 4);
    return v;
}

static void get_str(Rd *r, char *out, size_t n)
{
    unsigned k = get_u8(r);
    const uint8_t *q = take(r, k);
    size_t m = q ? (k < n - 1 ? k : n - 1) : 0;
    if (m)
        memcpy(out, q, m);
    out[m] = '\0';
}

static const uint8_t *get_blob(Rd *r, size_t *k)
{
    *k = get_u32(r);
    return *k ? take(r, *k) : NULL;
}

/* ---- codes ---- */


static GeneticCode *parse(const uint8_t *p, size_t n)
{
    char err[160];
    return p && n ? bgg_parse((const char *)p, n, err, sizeof err) : NULL;
}

static uint8_t *copy(const uint8_t *p, size_t n)
{
    if (!p || !n)
        return NULL;
    uint8_t *q = malloc(n);
    if (!q)
        abort();
    memcpy(q, p, n);
    return q;
}

static char *text_copy(const uint8_t *p, size_t n)
{
    if (!p || !n)
        return NULL;
    char *q = malloc(n + 1);
    if (!q)
        abort();
    memcpy(q, p, n);
    q[n] = 0;
    return q;
}

static void round_free(RecordRound *r)
{
    gc_unref(r->final);
    gc_unref(r->genesis);
    free(r->fbgg);
    free(r->gbgg);
    for (int i = 0; i < r->nf; i++)
        free(r->f[i].bgg);
    free(r->params);
}

/* ---- the file ---- */

static void encode(Buf *b, const RecordRound *r)
{
    put_u8(b, (unsigned)r->source);
    put_u64(b, (uint64_t)r->when);
    put_i32(b, r->cycle);
    put_u64(b, (uint64_t)r->seed);
    put_i32(b, r->level);
    put_u8(b, (unsigned)r->outcome);
    put_u64(b, r->settings);
    put_i32(b, r->map);
    put_i32(b, r->units);
    put_u8(b, r->knockout);
    put_i32(b, r->wild);
    put_i32(b, r->t_end);
    put_i32(b, r->t_lone);
    put_i32(b, r->pop);
    put_i32(b, r->lines);
    put_f32(b, r->share);
    put_u8(b, (unsigned)r->nf);
    for (int i = 0; i < r->nf; i++) {
        put_u32(b, r->f[i].uid);
        put_str(b, r->f[i].name);
        put_i32(b, r->f[i].alive);
        put_blob(b, r->f[i].bgg, r->f[i].bgg_n);
    }
    put_u32(b, r->uid);
    put_str(b, r->name);
    put_str(b, r->line);
    put_str(b, r->plan);
    put_blob(b, r->fbgg, r->fbgg_n);
    put_str(b, r->gname);
    put_str(b, r->gplan);
    put_blob(b, r->gbgg, r->gbgg_n);
    put_u32(b, (uint32_t)r->ncurve);
    for (int i = 0; i < r->ncurve; i++) {
        put_i32(b, r->curve[i].t);
        put_i32(b, r->curve[i].pop);
        put_i32(b, r->curve[i].lines);
    }
    put_blob(b, r->params, r->params ? strlen(r->params) : 0);
}

static bool decode(Rd *d, RecordRound *r)
{
    memset(r, 0, sizeof *r);
    r->source = (int)get_u8(d);
    r->when = (int64_t)get_u64(d);
    r->cycle = get_i32(d);
    r->seed = (int64_t)get_u64(d);
    r->level = get_i32(d);
    r->outcome = (int)get_u8(d);
    r->settings = get_u64(d);
    r->map = get_i32(d);
    r->units = get_i32(d);
    r->knockout = get_u8(d) != 0;
    r->wild = get_i32(d);
    r->t_end = get_i32(d);
    r->t_lone = get_i32(d);
    r->pop = get_i32(d);
    r->lines = get_i32(d);
    r->share = get_f32(d);
    r->nf = (int)get_u8(d);
    if (r->nf > REC_FIGHTERS)
        return false;
    for (int i = 0; i < r->nf; i++) {
        r->f[i].uid = get_u32(d);
        get_str(d, r->f[i].name, sizeof r->f[i].name);
        r->f[i].alive = get_i32(d);
        size_t k;
        const uint8_t *q = get_blob(d, &k);
        r->f[i].bgg = d->bad ? NULL : copy(q, k);
        r->f[i].bgg_n = r->f[i].bgg ? k : 0;
    }
    r->uid = get_u32(d);
    get_str(d, r->name, sizeof r->name);
    get_str(d, r->line, sizeof r->line);
    get_str(d, r->plan, sizeof r->plan);
    size_t fn, gn;
    const uint8_t *fp = get_blob(d, &fn);
    get_str(d, r->gname, sizeof r->gname);
    get_str(d, r->gplan, sizeof r->gplan);
    const uint8_t *gp = get_blob(d, &gn);
    uint32_t nc = get_u32(d);
    if (d->bad || nc > REC_CURVE)
        return false;
    r->ncurve = (int)nc;
    for (int i = 0; i < r->ncurve; i++) {
        r->curve[i].t = get_i32(d);
        r->curve[i].pop = get_i32(d);
        r->curve[i].lines = get_i32(d);
    }
    size_t pn;
    const uint8_t *pp = get_blob(d, &pn);
    if (d->bad)
        return false;
    r->params = text_copy(pp, pn);
    r->fbgg = copy(fp, fn);
    r->fbgg_n = fp ? fn : 0;
    r->gbgg = copy(gp, gn);
    r->gbgg_n = gp ? gn : 0;
    r->final = parse(r->fbgg, r->fbgg_n);
    r->genesis = parse(r->gbgg, r->gbgg_n);
    return true;
}

static RecordRound *slot(void)
{
    if (R.n == R.cap) {
        R.cap = R.cap ? R.cap * 2 : 256;
        R.r = realloc(R.r, (size_t)R.cap * sizeof *R.r);
        if (!R.r)
            abort();
    }
    return &R.r[R.n++];
}

static void header(Buf *b)
{
    put(b, "SYNRECRD", 8);
    put_u32(b, RECORD_VERSION);
    put_u32(b, SIM_TAG);
}

static void append(Buf *b, const RecordRound *r)
{
    Buf one = {0};
    encode(&one, r);
    put_u32(b, (uint32_t)one.n);
    put(b, one.p, one.n);
    free(one.p);
}

/* The whole file again, by way of a temporary one. */
static bool rewrite(void)
{
    Buf b = {0};
    header(&b);
    for (int i = 0; i < R.n; i++)
        append(&b, &R.r[i]);
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", R.path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(b.p, 1, b.n, f) == b.n;
    if (f && fclose(f) != 0)
        ok = false;
    ok = ok && arena_os_replace(tmp, R.path);
    if (!ok) {
        remove(tmp);
        fprintf(stderr, "arena: %s can't be written\n", R.path);
    }
    free(b.p);
    return ok;
}

void syn_record_set_file(const char *path)
{
    R.path_set = true;
    snprintf(R.path, sizeof R.path, "%s", path ? path : "");
}

void syn_record_init(void)
{
    if (R.inited)
        return;
    R.inited = true;
    if (!R.path_set)
        snprintf(R.path, sizeof R.path, "%s/%s", params_config_dir(), RECORD_FILE);
    if (!R.path[0])
        return;
    FILE *f = fopen(R.path, "rb");
    if (!f)
        return;
    Buf b = {0};
    uint8_t chunk[65536];
    size_t k;
    while ((k = fread(chunk, 1, sizeof chunk, f)) > 0)
        put(&b, chunk, k);
    fclose(f);
    Rd d = {b.p, b.n, 0, false};
    const uint8_t *magic = take(&d, 8);
    uint32_t version = magic && !memcmp(magic, "SYNRECRD", 8) ? get_u32(&d) : 0;
    bool ok = version >= 1 && version <= RECORD_VERSION && get_u32(&d) == SIM_TAG && !d.bad;
    if (!ok) {
        fprintf(stderr, "arena: %s isn't this simulation's record; it is left as it is\n", R.path);
        R.path[0] = '\0';
        free(b.p);
        return;
    }
    while (d.n - d.at >= 4) {
        uint32_t len = get_u32(&d);
        const uint8_t *q = take(&d, len);
        if (!q)
            break;
        Rd one = {q, len, 0, false};
        RecordRound *r = slot();
        if (!decode(&one, r)) {
            round_free(r);
            R.n--;
        }
    }
    free(b.p);
    R.version++;
    /* an older file, written again as this version before anything is
     * added to it */
    if (version < RECORD_VERSION && !rewrite())
        R.path[0] = '\0';
}

void syn_record_free(void)
{
    for (int i = 0; i < R.n; i++)
        round_free(&R.r[i]);
    free(R.r);
    R.r = NULL;
    R.n = R.cap = 0;
    R.inited = false;
}

void syn_record_add(const RecordRound *in, const uint8_t *fbgg, size_t fn, const uint8_t *gbgg,
                    size_t gn)
{
    syn_record_init();
    RecordRound *r = slot();
    *r = *in;
    if (r->ncurve > REC_CURVE)
        r->ncurve = REC_CURVE;
    if (r->nf > REC_FIGHTERS)
        r->nf = REC_FIGHTERS;
    for (int i = 0; i < r->nf; i++) {
        r->f[i].bgg = copy(in->f[i].bgg, in->f[i].bgg_n);
        r->f[i].bgg_n = r->f[i].bgg ? in->f[i].bgg_n : 0;
    }
    r->fbgg = copy(fbgg, fn);
    r->fbgg_n = r->fbgg ? fn : 0;
    r->gbgg = copy(gbgg, gn);
    r->gbgg_n = r->gbgg ? gn : 0;
    r->final = parse(r->fbgg, r->fbgg_n);
    r->genesis = parse(r->gbgg, r->gbgg_n);
    r->params = in->params ? text_copy((const uint8_t *)in->params, strlen(in->params)) : NULL;
    R.version++;
    if (!R.path[0])
        return;
    Buf b = {0};
    FILE *f = fopen(R.path, "ab");
    if (f && fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0)
        header(&b);
    append(&b, r);
    /* in one write, on the end: whole, even beside another program's */
    if (!f || fwrite(b.p, 1, b.n, f) != b.n || fclose(f) != 0) {
        if (f)
            fclose(f);
        fprintf(stderr, "arena: %s can't be written\n", R.path);
    }
    free(b.p);
}

bool syn_record_has(int64_t seed, int cycle)
{
    syn_record_init();
    for (int i = 0; i < R.n; i++)
        if (R.r[i].seed == seed && R.r[i].cycle == cycle)
            return true;
    return false;
}

int syn_record_count(void)
{
    syn_record_init();
    return R.n;
}

const RecordRound *syn_record_at(int i)
{
    return i >= 0 && i < R.n ? &R.r[i] : NULL;
}

unsigned syn_record_version(void)
{
    return R.version;
}

/* ---- how far a bloodline went ---- */

/* the genes of a .bgg file, as their lines' text, and the line before them */
static int gene_lines(const uint8_t *p, size_t n, const char **at, size_t *len, int max,
                      const char **head, size_t *head_n)
{
    const char *s = (const char *)p, *end = s + n;
    int k = 0;
    *head = NULL;
    *head_n = 0;
    for (const char *q = s; q < end;) {
        const char *nl = memchr(q, '\n', (size_t)(end - q));
        const char *e = nl ? nl : end;
        const char *t = q;
        while (t < e && (*t == ' ' || *t == '\t'))
            t++;
        if ((size_t)(e - t) > 6 && !memcmp(t, "<gene ", 6)) {
            if (k < max) {
                at[k] = t;
                len[k] = (size_t)(e - t);
            }
            k++;
        } else if ((size_t)(e - t) > 14 && !memcmp(t, "<genetic_code ", 14)) {
            *head = t;
            *head_n = (size_t)(e - t);
        }
        q = nl ? nl + 1 : end;
    }
    return k < max ? k : max;
}

int syn_record_changes(const RecordRound *r)
{
    if (!r->fbgg || !r->gbgg)
        return -1;
    enum { MAXG = 256 };
    const char *a[MAXG], *b[MAXG], *ha, *hb;
    size_t al[MAXG], bl[MAXG], han, hbn;
    int na = gene_lines(r->gbgg, r->gbgg_n, a, al, MAXG, &ha, &han);
    int nb = gene_lines(r->fbgg, r->fbgg_n, b, bl, MAXG, &hb, &hbn);
    int d = na > nb ? na - nb : nb - na;
    for (int i = 0; i < na && i < nb; i++)
        d += al[i] != bl[i] || memcmp(a[i], b[i], al[i]);
    d += han != hbn || (ha && hb && memcmp(ha, hb, han));
    return d;
}
