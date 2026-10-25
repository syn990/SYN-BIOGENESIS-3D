/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/family.c, the SYN edition of its C port
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

#include "syn/family.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "syn/family_names.h"
#include "guide/palette.h"

/* The species in the order they were founded, so a parent always comes
 * before its children (forgetting keeps the order). */
static struct {
    Species *sp;
    int n, cap;
    uint32_t *map; /* id -> index + 1, open addressing */
    int map_cap;
    uint32_t next_id; /* never reset: codes copied earlier keep their marks */
    int seen_next;    /* the world's next organism id when last looked at */
    int pop;
    uint32_t top;
    int top_n;
    int named; /* families named */
    unsigned version;
    bool started;
} F = {.next_id = 1};

/* ---- holding codes (as syn/history.c) ---- */

#define hold gc_assign

/* ---- the maps ---- */

static unsigned mix32(uint32_t k)
{
    k ^= k >> 16;
    k *= 0x7feb352du;
    k ^= k >> 15;
    return k;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

#define FNV0 0xcbf29ce484222325ULL

static void map_rebuild(void)
{
    int cap = 1024;
    while (cap < F.n * 2 + 2)
        cap *= 2;
    if (cap != F.map_cap) {
        free(F.map);
        F.map = malloc((size_t)cap * sizeof *F.map);
        if (!F.map)
            abort();
        F.map_cap = cap;
    }
    memset(F.map, 0, (size_t)cap * sizeof *F.map);
    unsigned m = (unsigned)cap - 1;
    for (int i = 0; i < F.n; i++) {
        unsigned h = mix32(F.sp[i].id) & m;
        while (F.map[h])
            h = (h + 1) & m;
        F.map[h] = (uint32_t)i + 1;
    }
}

static Species *by_id(uint32_t id)
{
    if (!id || !F.map_cap)
        return NULL;
    unsigned m = (unsigned)F.map_cap - 1, h = mix32(id) & m;
    while (F.map[h]) {
        Species *s = &F.sp[F.map[h] - 1];
        if (s->id == id)
            return s;
        h = (h + 1) & m;
    }
    return NULL;
}


/* ---- species ---- */

static int commonest(const Organism *o)
{
    int count[SYN_NCOL] = {0};
    for (int s = 0; s < o->segments; s++) {
        int slot = o->seg_color[s];
        if (slot >= 0 && slot < SYN_NCOL)
            count[slot]++;
    }
    int best = 0;
    for (int c = 1; c < SYN_NCOL; c++)
        if (count[c] > count[best])
            best = c;
    return best;
}

/* How far a new species (begun by o) has moved from the plan its family
 * began with. */
#define DIVERGE FAM_DIVERGE

static int divergence(const Species *head, const Species *s, const Organism *o)
{
    (void)s;
    const GeneticCode *a = head->gc, *b = o ? o->gc : NULL;
    if (!a || !b)
        return DIVERGE;
    int d = abs(a->symmetry - b->symmetry) + (a->mirror != b->mirror);
    /* the genes' colours: insertions, deletions and recolourings */
    int na = a->ngenes, nb = b->ngenes, stack[65] = {0};
    int *row = nb < 65 ? stack : malloc((size_t)(nb + 1) * sizeof *row);
    if (!row)
        abort();
    for (int j = 0; j <= nb; j++)
        row[j] = j;
    for (int i = 1; i <= na; i++) {
        int diag = row[0];
        row[0] = i;
        for (int j = 1; j <= nb; j++) {
            int up = row[j], v = diag + (a->genes[i - 1].color != b->genes[j - 1].color);
            if (up + 1 < v)
                v = up + 1;
            if (row[j - 1] + 1 < v)
                v = row[j - 1] + 1;
            row[j] = v;
            diag = up;
        }
    }
    d += row[nb];
    if (row != stack)
        free(row);
    return d;
}

/* A new species; o (may be NULL) is the organism it begins with. */
static Species *add(uint32_t parent, uint64_t key, int founder, const Organism *o, int64_t t)
{
    if (F.n == F.cap) {
        F.cap = F.cap ? F.cap * 2 : 256;
        F.sp = realloc(F.sp, (size_t)F.cap * sizeof *F.sp);
        if (!F.sp)
            abort();
    }
    Species *s = &F.sp[F.n++];
    memset(s, 0, sizeof *s);
    s->id = F.next_id++;
    s->key = key;
    s->founder = founder;
    s->born = s->last_seen = s->peak_t = t;
    s->root = s->id;
    s->family = s->id;
    Species *p = by_id(parent);
    if (p) {
        s->parent = p->id;
        s->root = p->root;
        s->depth = p->depth + 1;
        p->kids++;
        /* its parent's family, until it has moved far enough to begin one */
        const Species *head = by_id(p->family);
        if (head && divergence(head, s, o) < DIVERGE)
            s->family = head->id;
    }
    if (o) {
        s->members = 1;
        s->colour = (uint8_t)commonest(o);
        hold(&s->gc, o->gc);
    }
    /* the maps at most half full */
    if (F.n * 2 + 2 > F.map_cap)
        map_rebuild();
    else {
        unsigned m = (unsigned)F.map_cap - 1, h = mix32(s->id) & m;
        while (F.map[h])
            h = (h + 1) & m;
        F.map[h] = (uint32_t)F.n;
    }
    F.version++;
    return &F.sp[F.n - 1];
}


/* The body plan: what makes a species. */
static uint64_t plan_key(const GeneticCode *gc)
{
    uint64_t h = FNV0;
    int v[3] = {gc->symmetry, gc->mirror, gc->ngenes};
    h = fnv(h, v, sizeof v);
    for (int i = 0; i < gc->ngenes; i++)
        h = fnv(h, &gc->genes[i].color, 1);
    return h;
}

/* A newborn (or a code found without a species): into its parent's
 * species if the plan is the same, else a new one under it. */
static Species *place(Organism *o, int64_t t)
{
    GeneticCode *gc = o->gc;
    if (!gc)
        return NULL;
    uint64_t key = plan_key(gc);
    Species *p = by_id(gc->lineage);
    if (p && p->key == key) {
        p->members++;
        return p;
    }
    Species *s = add(p ? p->id : 0, key, o->id, o, t);
    gc->lineage = s->id;
    return s;
}

static Species *found(const Organism *o)
{
    if (!o->gc)
        return NULL;
    Species *s = by_id(o->gc->lineage);
    return s && s->key == plan_key(o->gc) ? s : NULL;
}


/* ---- the public side ---- */

static void release(void)
{
    for (int i = 0; i < F.n; i++)
        hold(&F.sp[i].gc, NULL);
    F.n = 0;
    if (F.map_cap)
        map_rebuild();
    F.top = 0;
    F.top_n = 0;
    F.named = 0;
    F.version++;
}

void syn_family_reset(World *w)
{
    release();
    F.started = true;
    F.seen_next = w->next_id;
    int64_t t = world_time_units(w);
    /* the world as found: one species per plan, a root each */
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (o->removed || !o->alive || !o->gc)
            continue;
        uint64_t key = plan_key(o->gc);
        Species *s = NULL;
        for (int j = 0; j < F.n && !s; j++)
            if (F.sp[j].key == key)
                s = &F.sp[j];
        if (s)
            s->members++;
        else
            s = add(0, key, o->id, o, t);
        o->gc->lineage = s->id;
    }
    syn_family_unit(w, t);
}

void syn_family_frame(World *w)
{
    if (!F.started || w->next_id == F.seen_next)
        return;
    int lo = F.seen_next;
    F.seen_next = w->next_id;
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (!o->removed && o->id >= lo)
            place(o, world_time_units(w));
    }
}

/* Its number, and its family's name if it has none yet. */
static void name(Species *s)
{
    Species *head = by_id(s->family);
    if (!head || s->number)
        return;
    if (!head->name_no)
        head->name_no = ++F.named;
    s->number = ++head->numbered;
}

/* The living counted: each species' alive and carriers, the most living. */
static int count(World *w, int64_t t)
{
    for (int i = 0; i < F.n; i++)
        F.sp[i].alive = F.sp[i].carriers = 0;
    int pop = 0;
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (o->removed || !o->alive)
            continue;
        pop++;
        /* pasted, created or revived since: a species of its own */
        Species *s = found(o);
        if (!s)
            s = place(o, t);
        if (s)
            s->alive++;
        /* an infected organism's next child takes the infecting code's species */
        if (o->infected_gc) {
            Species *c = by_id(o->infected_gc->lineage);
            if (c)
                c->carriers++;
        }
    }
    F.pop = pop;
    F.top = 0;
    F.top_n = 0;
    for (int i = 0; i < F.n; i++)
        if (F.sp[i].alive > F.top_n) {
            F.top_n = F.sp[i].alive;
            F.top = F.sp[i].id;
        }
    return pop;
}

void syn_family_unit(World *w, int64_t t)
{
    if (!F.started)
        return;
    int pop = count(w, t);
    bool forget = false;
    /* in the order they were founded, so names come in that order */
    for (int i = 0; i < F.n; i++) {
        Species *s = &F.sp[i];
        if (s->alive > 0) {
            s->last_seen = t;
            s->extinct = false;
            if (s->alive > s->peak) {
                s->peak = s->alive;
                s->peak_t = t;
            }
            if (s->peak >= FAM_NOTABLE && s->alive * 50 >= pop) {
                s->notable = true;
                name(s);
            }
        } else if (s->carriers == 0) {
            s->extinct = true;
        }
    }
    /* the forgotten, children first: a parent can follow its last child */
    for (int i = F.n - 1; i >= 0; i--) {
        Species *s = &F.sp[i];
        if (!s->extinct || s->notable || s->kids > 0 || s->pins > 0)
            continue;
        Species *p = by_id(s->parent);
        if (p)
            p->kids--;
        hold(&s->gc, NULL);
        s->id = 0;
        forget = true;
    }
    if (forget) {
        int k = 0;
        for (int i = 0; i < F.n; i++)
            if (F.sp[i].id)
                F.sp[k++] = F.sp[i];
        F.n = k;
        map_rebuild();
    }
    F.version++;
}

void syn_family_free(void)
{
    release();
    free(F.sp);
    free(F.map);
    F.sp = NULL;
    F.map = NULL;
    F.cap = F.map_cap = 0;
    F.started = false;
}

uint32_t syn_family_of(const Organism *o)
{
    const Species *s = o ? found(o) : NULL;
    return s ? s->id : 0;
}

const Species *syn_family_get(uint32_t id)
{
    return by_id(id);
}

int syn_family_count(void)
{
    return F.n;
}

const Species *syn_family_at(int i)
{
    return i >= 0 && i < F.n ? &F.sp[i] : NULL;
}

int syn_family_index(uint32_t id)
{
    const Species *s = by_id(id);
    return s ? (int)(s - F.sp) : -1;
}

uint32_t syn_family_top(int *alive)
{
    if (alive)
        *alive = F.top_n;
    return F.top;
}

void syn_family_pin(uint32_t id, int d)
{
    Species *s = by_id(id);
    if (s)
        s->pins += d;
}

unsigned syn_family_version(void)
{
    return F.version;
}

/* ---- names ---- */

const Species *syn_family_head(const Species *s)
{
    return s ? by_id(s->family) : NULL;
}

const Species *syn_family_parent_head(const Species *s)
{
    const Species *head = syn_family_head(s);
    const Species *p = head ? by_id(head->parent) : NULL;
    return p ? by_id(p->family) : NULL;
}

int syn_family_named(void)
{
    return F.named;
}

/* where the names start in the list (syn_family_name_base) */
static int name_base;

void syn_family_name_base(int base)
{
    const int count = (int)(sizeof k_family_names / sizeof k_family_names[0]);
    name_base = (base % count + count) % count;
}

void syn_family_title(int no, char *out, size_t n)
{
    out[0] = '\0';
    if (no <= 0)
        return;
    const int count = (int)(sizeof k_family_names / sizeof k_family_names[0]);
    int round = (no - 1) / count + 1;
    const char *g = k_family_names[(name_base + no - 1) % count];
    if (round == 1) {
        snprintf(out, n, "%s", g);
        return;
    }
    /* the second time round VELORAX II, and so on */
    static const struct {
        int v;
        const char *s;
    } k_roman[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
                   {40, "XL"},  {10, "X"},   {9, "IX"},   {5, "V"},    {4, "IV"},   {1, "I"}};
    char r[32];
    size_t k = 0;
    for (size_t i = 0; i < sizeof k_roman / sizeof k_roman[0]; i++)
        while (round >= k_roman[i].v && k + 3 < sizeof r) {
            k += (size_t)snprintf(r + k, sizeof r - k, "%s", k_roman[i].s);
            round -= k_roman[i].v;
        }
    snprintf(out, n, "%s %s", g, r);
}

void syn_family_name(const Species *s, char *out, size_t n)
{
    out[0] = '\0';
    const Species *head = syn_family_head(s);
    if (!head || !head->name_no)
        return;
    char fam[40];
    syn_family_title(head->name_no, fam, sizeof fam);
    if (s->number)
        snprintf(out, n, "%s-%d", fam, s->number);
    else
        snprintf(out, n, "%s", fam);
}

void syn_family_label(const Species *s, char *out, size_t n)
{
    syn_family_name(s, out, n);
    if (!s)
        return;
    if (!out[0])
        snprintf(out, n, "#%d", s->founder);
    else if (!s->number) {
        /* as a biologist writes a species of a genus not yet named */
        size_t k = strlen(out);
        snprintf(out + k, n - k, " sp.");
    }
}

uint32_t syn_line_key(const Organism *o)
{
    const Species *s = o && o->gc ? syn_family_get(o->gc->lineage) : NULL;
    return s ? s->root : 0;
}

const Species *syn_family_line(uint32_t line)
{
    return by_id(line);
}

/* ---- saved beside a world ----
 *
 * Little-endian: "SYNFAMLY", the format, the simulation (3: this one),
 * next_id, the names given, the species, and which species each
 * organism's codes belong to. Loaded, next_id is the saved
 * one (past every id read): the species the reset founded are gone and
 * every organism's mark rewritten, so a world opened again numbers its
 * species as if it had never stopped. */

#define SAVE_MAGIC "SYNFAMLY"
#define SAVE_FORMAT 1
#define SAVE_SIM 3

typedef struct {
    uint8_t *p;
    size_t n, cap;
} Out;

static void put(Out *b, const void *v, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n)
            cap *= 2;
        b->p = realloc(b->p, cap);
        if (!b->p)
            abort();
        b->cap = cap;
    }
    memcpy(b->p + b->n, v, n);
    b->n += n;
}

static void put32(Out *b, uint32_t v)
{
    uint8_t x[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, x, 4);
}

static void put64(Out *b, uint64_t v)
{
    put32(b, (uint32_t)v);
    put32(b, (uint32_t)(v >> 32));
}

typedef struct {
    const uint8_t *p;
    size_t n, at;
    bool bad;
} In;

static const uint8_t *get(In *b, size_t n)
{
    if (b->bad || n > b->n - b->at) {
        b->bad = true;
        return NULL;
    }
    const uint8_t *p = b->p + b->at;
    b->at += n;
    return p;
}

static uint32_t get32(In *b)
{
    const uint8_t *x = get(b, 4);
    return x ? (uint32_t)x[0] | (uint32_t)x[1] << 8 | (uint32_t)x[2] << 16 | (uint32_t)x[3] << 24 : 0;
}

static uint64_t get64(In *b)
{
    uint64_t lo = get32(b);
    return lo | (uint64_t)get32(b) << 32;
}

size_t syn_family_save(const World *w, uint8_t **out)
{
    *out = NULL;
    Out b = {0};
    put(&b, SAVE_MAGIC, 8);
    put32(&b, SAVE_FORMAT);
    put32(&b, SAVE_SIM);
    put32(&b, F.next_id);
    put32(&b, (uint32_t)F.named);
    put32(&b, (uint32_t)F.n);
    for (int i = 0; i < F.n; i++) {
        const Species *s = &F.sp[i];
        put32(&b, s->id);
        put32(&b, s->parent);
        put32(&b, s->root);
        put32(&b, s->family);
        put32(&b, (uint32_t)s->depth);
        put32(&b, (uint32_t)s->founder);
        put64(&b, (uint64_t)s->born);
        put64(&b, (uint64_t)s->last_seen);
        put64(&b, (uint64_t)s->peak_t);
        put32(&b, (uint32_t)s->peak);
        put64(&b, (uint64_t)s->members);
        put32(&b, (uint32_t)s->pins);
        put32(&b, (uint32_t)s->name_no);
        put32(&b, (uint32_t)s->number);
        put32(&b, (uint32_t)s->numbered);
        uint8_t flags[3] = {s->notable, s->extinct, s->colour};
        put(&b, flags, 3);
        put64(&b, s->key);
        /* its code as its .bgg */
        size_t len = 0;
        uint8_t *bytes = s->gc ? bgg_bytes(s->gc, &len) : NULL;
        if (s->gc && !bytes) {
            free(b.p);
            return 0;
        }
        put32(&b, (uint32_t)len);
        if (len)
            put(&b, bytes, len);
        free(bytes);
    }
    /* the marks 0.9's file does not keep */
    uint32_t m = 0;
    for (int i = 0; i < w->norgs; i++)
        m += !w->orgs[i]->removed && w->orgs[i]->gc;
    put32(&b, m);
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (o->removed || !o->gc)
            continue;
        put32(&b, (uint32_t)o->id);
        put32(&b, o->gc->lineage);
        put32(&b, o->infected_gc ? o->infected_gc->lineage : 0);
    }
    *out = b.p;
    return b.n;
}

typedef struct {
    int id;
    uint32_t own, inf;
} Mark;

static int mark_cmp(const void *a, const void *b)
{
    int x = ((const Mark *)a)->id, y = ((const Mark *)b)->id;
    return (x > y) - (x < y);
}

bool syn_family_load(World *w, const uint8_t *in, size_t n)
{
    In b = {in, n, 0, false};
    const uint8_t *magic = get(&b, 8);
    if (!magic || memcmp(magic, SAVE_MAGIC, 8) != 0 || get32(&b) != SAVE_FORMAT || get32(&b) != SAVE_SIM)
        return false;
    uint32_t next_id = get32(&b);
    int named = (int)get32(&b);
    uint32_t count_ = get32(&b);
    /* at least 91 bytes a species */
    if (b.bad || count_ > (n - b.at) / 91)
        return false;

    /* all of it read before anything changes */
    Species *sp = calloc(count_ ? count_ : 1, sizeof *sp);
    if (!sp)
        abort();
    uint32_t i;
    for (i = 0; i < count_ && !b.bad; i++) {
        Species *s = &sp[i];
        s->id = get32(&b);
        s->parent = get32(&b);
        s->root = get32(&b);
        s->family = get32(&b);
        s->depth = (int)get32(&b);
        s->founder = (int)get32(&b);
        s->born = (int64_t)get64(&b);
        s->last_seen = (int64_t)get64(&b);
        s->peak_t = (int64_t)get64(&b);
        s->peak = (int)get32(&b);
        s->members = (int64_t)get64(&b);
        s->pins = (int)get32(&b);
        s->name_no = (int)get32(&b);
        s->number = (int)get32(&b);
        s->numbered = (int)get32(&b);
        const uint8_t *flags = get(&b, 3);
        if (flags) {
            s->notable = flags[0] != 0;
            s->extinct = flags[1] != 0;
            s->colour = flags[2] < SYN_NCOL ? flags[2] : 0;
        }
        s->key = get64(&b);
        uint32_t len = get32(&b);
        const uint8_t *xml = get(&b, len);
        if (xml && len) {
            char err[256];
            s->gc = bgg_parse((const char *)xml, len, err, sizeof err);
            if (!s->gc)
                b.bad = true;
        }
        if (!s->id)
            b.bad = true;
        if (s->id >= next_id)
            next_id = s->id + 1;
    }
    uint32_t m = get32(&b);
    void *marks = NULL;
    if (!b.bad && m > (n - b.at) / 12)
        b.bad = true;
    if (!b.bad) {
        Mark *mk = malloc((m ? m : 1) * sizeof *mk);
        if (!mk)
            abort();
        for (uint32_t k = 0; k < m; k++) {
            mk[k].id = (int)get32(&b);
            mk[k].own = get32(&b);
            mk[k].inf = get32(&b);
            if (mk[k].own >= next_id)
                next_id = mk[k].own + 1;
            if (mk[k].inf >= next_id)
                next_id = mk[k].inf + 1;
        }
        marks = mk;
    }
    if (b.bad || b.at != n) {
        for (uint32_t k = 0; k < i; k++)
            hold(&sp[k].gc, NULL);
        free(sp);
        free(marks);
        return false;
    }

    /* the tree found at reset gives way */
    release();
    free(F.sp);
    F.sp = sp;
    F.n = F.cap = (int)count_;
    if (!F.cap) {
        free(F.sp);
        F.sp = NULL;
    }
    F.next_id = next_id;
    F.named = named;
    map_rebuild();
    for (int k = 0; k < F.n; k++) {
        Species *p = by_id(F.sp[k].parent);
        if (p)
            p->kids++;
    }
    /* each code's species again, even a forgotten species' (a corpse's):
     * its id is never given again; codes not in the file are placed anew */
    Mark *mk = marks;
    qsort(mk, m, sizeof *mk, mark_cmp);
    for (int k = 0; k < w->norgs; k++) {
        Organism *o = w->orgs[k];
        if (o->gc)
            o->gc->lineage = 0;
        if (o->infected_gc)
            o->infected_gc->lineage = 0;
    }
    for (int k = 0; k < w->norgs; k++) {
        Organism *o = w->orgs[k];
        Mark key = {.id = o->id};
        const Mark *at = o->removed ? NULL : bsearch(&key, mk, m, sizeof *mk, mark_cmp);
        if (!at)
            continue;
        if (o->gc)
            o->gc->lineage = at->own;
        if (o->infected_gc)
            o->infected_gc->lineage = at->inf;
    }
    free(marks);
    F.started = true;
    F.seen_next = w->next_id;
    count(w, world_time_units(w));
    F.version++;
    return true;
}

/* ---- in words ---- */

static const char *nth(int i)
{
    static char b[16];
    int n = i + 1;
    const char *suf = (n % 100 >= 11 && n % 100 <= 13) ? "th"
                      : n % 10 == 1                    ? "st"
                      : n % 10 == 2                    ? "nd"
                      : n % 10 == 3                    ? "rd"
                                                       : "th";
    snprintf(b, sizeof b, "%d%s", n, suf);
    return b;
}

#define ARROW " to " /* the interface font has no arrow */

void syn_family_change(const Species *s, char *out, size_t n)
{
    out[0] = '\0';
    const Species *p = s ? by_id(s->parent) : NULL;
    if (!p || !p->gc || !s->gc) {
        if (p)
            snprintf(out, n, "a clade of its own");
        return;
    }
    const GeneticCode *a = p->gc, *b = s->gc;
    size_t k = 0;
#define PUT(...)                                                                                   \
    do {                                                                                           \
        if (k < n) {                                                                               \
            if (k)                                                                                 \
                k += (size_t)snprintf(out + k, n - k, ", ");                                       \
            if (k < n)                                                                             \
                k += (size_t)snprintf(out + k, n - k, __VA_ARGS__);                                \
        }                                                                                          \
    } while (0)
    if (a->symmetry != b->symmetry)
        PUT("%d" ARROW "%d arm%s", a->symmetry, b->symmetry, b->symmetry > 1 ? "s" : "");
    if (a->mirror != b->mirror)
        PUT("mirroring %s", b->mirror ? "on" : "off");
    int na = syn_genes(a), nb = syn_genes(b);
    if (na == nb) {
        int changed = 0;
        for (int i = 0; i < na; i++) {
            int ca = syn_gene_col(a, i), cb = syn_gene_col(b, i);
            if (ca == cb)
                continue;
            if (++changed <= 2)
                PUT("%s segment %s" ARROW "%s", nth(i), syn_col_name(ca), syn_col_name(cb));
        }
        if (changed > 2)
            PUT("%d more", changed - 2);
    } else if (nb == na + 1 || nb == na - 1) {
        /* one segment more or less: where */
        const GeneticCode *lo = nb < na ? b : a, *hi = nb < na ? a : b;
        int nl = nb < na ? nb : na, at = nl;
        for (int i = 0; i < nl; i++)
            if (syn_gene_col(lo, i) != syn_gene_col(hi, i)) {
                at = i;
                break;
            }
        bool same = true;
        for (int i = at; i < nl && same; i++)
            same = syn_gene_col(lo, i) == syn_gene_col(hi, i + 1);
        if (same)
            PUT("%s %s segment (%s)", nb > na ? "+" : "-", syn_col_name(syn_gene_col(hi, at)),
                nth(at));
        else
            PUT("%d" ARROW "%d segments, recoloured", na, nb);
    } else {
        PUT("%d" ARROW "%d segments", na, nb);
    }
    if (!k)
        snprintf(out, n, "lengths and angles only");
#undef PUT
}

void syn_family_plan(const GeneticCode *gc, char *out, size_t n)
{
    if (!gc) {
        snprintf(out, n, "never seen alive here");
        return;
    }
    int ng = syn_genes(gc);
    size_t k = (size_t)snprintf(out, n, "%d arm%s%s; %d segment%s%s:", gc->symmetry, gc->symmetry > 1 ? "s" : "",
                                gc->mirror ? ", mirrored" : "", ng, ng != 1 ? "s" : "",
                                gc->symmetry > 1 ? " per arm" : "");
    /* each colour once, in the order it first appears */
    int seen[64], cnt[64], m = 0;
    for (int i = 0; i < ng; i++) {
        int c = syn_gene_col(gc, i), j = 0;
        while (j < m && seen[j] != c)
            j++;
        if (j == m && m < 64) {
            seen[m] = c;
            cnt[m++] = 0;
        }
        if (j < m)
            cnt[j]++;
    }
    for (int j = 0; j < m && k < n; j++) {
        if (cnt[j] > 1)
            k += (size_t)snprintf(out + k, n - k, "%s %d %s", j ? "," : "", cnt[j], syn_col_name(seen[j]));
        else
            k += (size_t)snprintf(out + k, n - k, "%s %s", j ? "," : "", syn_col_name(seen[j]));
    }
}
