/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/arena.c, the SYN edition of its C port
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

#include "syn/arena.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "compat.h"
#include "io/bgg.h"
#include "io/worldfile.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/world.h"
#include "syn/arena_os.h"
#include "syn/family.h"
#include "syn/record.h"
#include "syn/round.h"


#define SIM_TAG 3
#define SIM_NAME "SYN-BIOGENESIS-3D"
#define BOARD_FILE "arena.board"

/* 1: before the ladder, its entries read as level 1; 2: without the level
 * each came on at */
#define BOARD_VERSION 3
#define NAME_STEP 97 /* round c's names start c * 97 into the list: all 500 in 500 rounds */
#define CACHE 16     /* codes a lane keeps of the species it was told about */

/* ---- bytes ---- */

typedef struct {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void need(Buf *b, size_t k)
{
    if (b->n + k <= b->cap)
        return;
    size_t c = b->cap ? b->cap : 256;
    while (c < b->n + k)
        c *= 2;
    uint8_t *q = realloc(b->p, c);
    if (!q)
        abort();
    b->p = q;
    b->cap = c;
}

static void put(Buf *b, const void *v, size_t k)
{
    need(b, k);
    if (k)
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
    uint8_t c[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, c, 4);
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
    put(b, p, k);
}

typedef struct {
    const uint8_t *p;
    size_t n, at;
    bool bad;
} Rd;

static const uint8_t *take(Rd *r, size_t k)
{
    if (r->bad || k > r->n - r->at) {
        r->bad = true;
        return NULL;
    }
    const uint8_t *q = r->p + r->at;
    r->at += k;
    return q;
}

static unsigned get_u8(Rd *r)
{
    const uint8_t *q = take(r, 1);
    return q ? q[0] : 0;
}

static uint32_t le32(const uint8_t *q)
{
    return (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
}

static uint32_t get_u32(Rd *r)
{
    const uint8_t *q = take(r, 4);
    return q ? le32(q) : 0;
}

static int get_i32(Rd *r)
{
    return (int)(int32_t)get_u32(r);
}

static uint64_t get_u64(Rd *r)
{
    uint64_t lo = get_u32(r);
    return lo | (uint64_t)get_u32(r) << 32;
}

static float get_f32(Rd *r)
{
    uint32_t u = get_u32(r);
    float f;
    memcpy(&f, &u, 4);
    return f;
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
    return take(r, *k);
}

static bool write_all(int fd, const void *p, size_t n)
{
    const uint8_t *q = p;
    while (n) {
        ssize_t k = write(fd, q, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        q += k;
        n -= (size_t)k;
    }
    return true;
}

static bool read_all(int fd, void *p, size_t n)
{
    uint8_t *q = p;
    while (n) {
        ssize_t k = read(fd, q, n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return false;
        q += k;
        n -= (size_t)k;
    }
    return true;
}

/* a message: its type, its length, then it */
static bool send_msg(int fd, int type, const Buf *b)
{
    uint8_t h[5] = {(uint8_t)type, (uint8_t)b->n, (uint8_t)(b->n >> 8), (uint8_t)(b->n >> 16),
                    (uint8_t)(b->n >> 24)};
    return write_all(fd, h, 5) && write_all(fd, b->p, b->n);
}

static double now_s(void)
{
    return arena_os_now();
}

/* ---- codes ---- */

/* its .bgg file's bytes, malloc'd */
static uint8_t *code_bytes(const GeneticCode *gc, size_t *n)
{
    return bgg_bytes(gc, n);
}

static GeneticCode *code_parse(const uint8_t *p, size_t n)
{
    char err[160];
    return p && n ? bgg_parse(p, n, err, (int)sizeof err) : NULL;
}

#define hold gc_assign
static void drop(GeneticCode *gc)
{
    gc_unref(gc);
}

/* ---- the map ---- */

const char *syn_arena_map_name(int map)
{
    static const char *const k[MAP_COUNT] = {"PARAMETERS", "SMALL", "MEDIUM", "LARGE", "POND", "CUBE"};
    return map >= 0 && map < MAP_COUNT ? k[map] : "?";
}

uint64_t syn_arena_fingerprint(void)
{
    return round_fingerprint(syn_arena_settings()->map);
}

void syn_arena_world(int *w, int *h, int *d, int *organisms)
{
    Params q;
    round_params(&q, syn_arena_settings()->map);
    *w = q.world_width;
    *h = q.world_height;
    *d = q.world_depth;
    *organisms = q.initial_organisms;
}

int syn_arena_cores(void)
{
    int n = arena_os_cores();
    return n < 1 ? 1 : n > ARENA_LANES ? ARENA_LANES : n;
}

/* ================= the worker: one cycle at a time ================= */

static struct {
    uint32_t *told; /* species whose codes were sent */
    int ntold, cap_told;
    uint32_t *fed; /* species sent for the feed */
    int nfed, cap_fed;
    int nseries;
    uint32_t series[ARENA_SERIES];
    int series_slot[ARENA_SERIES];
    int *sp, *rt; /* the living by species and by line, per species index */
    int cap;
} WK;

static bool has_id(const uint32_t *a, int n, uint32_t v)
{
    for (int i = 0; i < n; i++)
        if (a[i] == v)
            return true;
    return false;
}

static void add_id(uint32_t **a, int *n, int *cap, uint32_t v)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *a = realloc(*a, (size_t)*cap * sizeof **a);
        if (!*a)
            abort();
    }
    (*a)[(*n)++] = v;
}

typedef struct {
    int pop, lines;
} Count;

/* the living, by species (WK.sp) and by line (WK.rt), per species index */
static void tally(World *w, Count *c)
{
    int ns = syn_family_count();
    if (ns > WK.cap) {
        WK.cap = ns * 2;
        WK.sp = realloc(WK.sp, (size_t)WK.cap * sizeof *WK.sp);
        WK.rt = realloc(WK.rt, (size_t)WK.cap * sizeof *WK.rt);
        if (!WK.sp || !WK.rt)
            abort();
    }
    if (ns) {
        memset(WK.sp, 0, (size_t)ns * sizeof *WK.sp);
        memset(WK.rt, 0, (size_t)ns * sizeof *WK.rt);
    }
    c->pop = c->lines = 0;
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (o->removed || !o->alive)
            continue;
        c->pop++;
        int k = syn_family_index(syn_family_of(o));
        if (k < 0)
            continue;
        WK.sp[k]++;
        int r = syn_family_index(syn_family_at(k)->root);
        if (r >= 0)
            WK.rt[r]++;
    }
    for (int k = 0; k < ns; k++)
        c->lines += WK.rt[k] > 0;
}

/* a line by its family's name, else its founder's number */
static void line_label(const Species *root, char *out, size_t n)
{
    if (root && root->name_no)
        syn_family_title(root->name_no, out, n);
    else
        snprintf(out, n, "#%d", root ? root->founder : 0);
}

/* the bigger of two (more living, then the earlier founder: the same in
 * every worker, unlike species ids) */
static bool beats(int na, const Species *a, int nb, const Species *b)
{
    return na != nb ? na > nb : a->founder < b->founder;
}

static void send_being(int fd, const Species *s, int alive, bool feed)
{
    if (!s->gc)
        return;
    size_t bn = 0;
    uint8_t *bytes = code_bytes(s->gc, &bn);
    if (!bytes)
        return;
    char label[48];
    syn_family_label(s, label, sizeof label);
    Buf b = {0};
    put_u32(&b, s->id);
    put_u8(&b, feed);
    put_i32(&b, alive);
    put_str(&b, label);
    put_blob(&b, bytes, bn);
    send_msg(fd, 'B', &b);
    free(bytes);
    free(b.p);
}

/* once a time unit: the bloodlines followed, the fighters, the leader */
static void report(int fd, World *w, const Count *c, double fps, const uint32_t *slot_root, int ns)
{
    int nsp = syn_family_count();
    /* a line is followed once it is among the biggest and notable */
    while (WK.nseries < ARENA_SERIES) {
        int best = -1;
        for (int k = 0; k < nsp; k++) {
            const Species *s = syn_family_at(k);
            if (WK.rt[k] < FAM_NOTABLE || s->root != s->id || has_id(WK.series, WK.nseries, s->id))
                continue;
            if (best < 0 || beats(WK.rt[k], s, WK.rt[best], syn_family_at(best)))
                best = k;
        }
        if (best < 0)
            break;
        WK.series[WK.nseries] = syn_family_at(best)->id;
        WK.series_slot[WK.nseries] = -1;
        WK.nseries++;
    }
    /* named species, for the feed */
    for (int k = 0; k < nsp; k++) {
        const Species *s = syn_family_at(k);
        if (s->number > 0 && !has_id(WK.fed, WK.nfed, s->id)) {
            send_being(fd, s, WK.sp[k], true);
            add_id(&WK.fed, &WK.nfed, &WK.cap_fed, s->id);
            if (!has_id(WK.told, WK.ntold, s->id))
                add_id(&WK.told, &WK.ntold, &WK.cap_told, s->id);
        }
    }
    /* the leader, its code first if not sent */
    int lead = -1;
    for (int k = 0; k < nsp; k++)
        if (WK.sp[k] > 0 && (lead < 0 || beats(WK.sp[k], syn_family_at(k), WK.sp[lead], syn_family_at(lead))))
            lead = k;
    if (lead >= 0 && !has_id(WK.told, WK.ntold, syn_family_at(lead)->id)) {
        send_being(fd, syn_family_at(lead), WK.sp[lead], false);
        add_id(&WK.told, &WK.ntold, &WK.cap_told, syn_family_at(lead)->id);
    }

    Buf b = {0};
    put_i32(&b, (int)world_time_units(w));
    put_i32(&b, c->pop);
    put_i32(&b, c->lines);
    put_f32(&b, (float)fps);
    put_u8(&b, (unsigned)WK.nseries);
    for (int i = 0; i < WK.nseries; i++) {
        int k = syn_family_index(WK.series[i]);
        char label[48];
        line_label(k >= 0 ? syn_family_at(k) : NULL, label, sizeof label);
        put_u32(&b, WK.series[i]);
        put_i32(&b, k >= 0 ? WK.rt[k] : 0);
        put_u8(&b, (unsigned)(WK.series_slot[i] + 1));
        put_str(&b, label);
    }
    put_u8(&b, (unsigned)ns);
    for (int j = 0; j < ns; j++) {
        int k = slot_root[j] ? syn_family_index(slot_root[j]) : -1;
        put_i32(&b, k >= 0 ? WK.rt[k] : 0);
    }
    if (lead >= 0) {
        char label[48];
        syn_family_label(syn_family_at(lead), label, sizeof label);
        put_u32(&b, syn_family_at(lead)->id);
        put_i32(&b, WK.sp[lead]);
        put_str(&b, label);
    } else {
        put_u32(&b, 0);
        put_i32(&b, 0);
        put_str(&b, "");
    }
    send_msg(fd, 'U', &b);
    free(b.p);
}

static void family_begin(World *w)
{
    syn_family_reset(w);
}

static Organism *by_org_id(World *w, int id)
{
    for (int i = 0; i < w->norgs; i++)
        if (w->orgs[i]->id == id && !w->orgs[i]->removed)
            return w->orgs[i];
    return NULL;
}

static void run_cycle(int fd, const uint8_t *p, size_t n)
{
    Rd r = {p, n, 0, false};
    int cycle = get_i32(&r);
    int64_t seed = (int64_t)get_u64(&r);
    int units = get_i32(&r);
    int base = get_i32(&r);
    bool knockout = get_u8(&r) != 0;
    int random_pct = get_i32(&r);
    size_t pn;
    const uint8_t *pp = get_blob(&r, &pn);
    int ns = (int)get_u8(&r);
    if (r.bad || pn != sizeof P || ns > ARENA_SLOTS || units < 1) {
        fprintf(stderr, "arena worker: a job it can't read\n");
        _exit(3);
    }
    memcpy(&P, pp, sizeof P);
    GeneticCode *code[ARENA_SLOTS] = {0};
    for (int k = 0; k < ns; k++) {
        size_t cn;
        const uint8_t *cp = get_blob(&r, &cn);
        code[k] = r.bad ? NULL : code_parse(cp, cn);
        if (!code[k]) {
            fprintf(stderr, "arena worker: a fighter's code it can't read\n");
            _exit(3);
        }
    }

    syn_family_name_base(base);
    WK.ntold = WK.nfed = WK.nseries = 0;

    /* the round's world (syn/round.h), its family tree begun after genesis */
    World w;
    world_init(&w);
    int first[ARENA_SLOTS], placed[ARENA_SLOTS];
    round_begin(&w, seed, code, ns, random_pct, family_begin, first, placed);
    syn_family_frame(&w);
    uint32_t slot_root[ARENA_SLOTS] = {0};
    for (int k = 0; k < ns; k++) {
        Organism *o = first[k] >= 0 ? by_org_id(&w, first[k]) : NULL;
        const Species *s = o ? syn_family_get(syn_family_of(o)) : NULL;
        slot_root[k] = s ? s->root : 0;
        if (slot_root[k] && WK.nseries < ARENA_SERIES) {
            WK.series[WK.nseries] = slot_root[k];
            WK.series_slot[WK.nseries] = k;
            WK.nseries++;
        }
    }

    Buf b = {0};
    put_i32(&b, cycle);
    put_u8(&b, (unsigned)ns);
    for (int k = 0; k < ns; k++) {
        put_u32(&b, slot_root[k]);
        put_i32(&b, placed[k]);
    }
    send_msg(fd, 'S', &b);
    b.n = 0;

    Count c;
    tally(&w, &c);
    bool can_ko = knockout && c.lines >= 2;
    bool ko = false;
    /* for the record: the living and the bloodlines once a time unit, and
     * when one bloodline was left */
    int t_lone = c.lines == 1 ? 0 : -1;
    RecordPoint *curve = malloc(((size_t)units + 2) * sizeof *curve);
    if (!curve)
        abort();
    int ncurve = 0;
    curve[ncurve++] = (RecordPoint){0, c.pop, c.lines};
    double t0 = now_s();
    int64_t frames = (int64_t)units * 256, f = 0;
    while (f < frames) {
        world_time(&w);
        syn_family_frame(&w);
        f++;
        if (w.frame % 256 != 0)
            continue;
        syn_family_unit(&w, world_time_units(&w));
        tally(&w, &c);
        if (ncurve < units + 2)
            curve[ncurve++] = (RecordPoint){(int)world_time_units(&w), c.pop, c.lines};
        if (t_lone < 0 && c.lines == 1)
            t_lone = (int)world_time_units(&w);
        double dt = now_s() - t0;
        report(fd, &w, &c, dt > 0 ? (double)f / dt : 0, slot_root, ns);
        if (c.pop == 0)
            break;
        if (can_ko && c.lines == 1 && f < frames) {
            ko = true;
            break;
        }
    }
    tally(&w, &c);

    /* the winner: the line with the most living, its species with the
     * most, and of that the living member with the most children */
    int nsp = syn_family_count();
    int win = -1, best = -1;
    for (int k = 0; k < nsp; k++)
        if (WK.rt[k] > 0 && (win < 0 || beats(WK.rt[k], syn_family_at(k), WK.rt[win], syn_family_at(win))))
            win = k;
    if (win >= 0) {
        uint32_t root = syn_family_at(win)->id;
        for (int k = 0; k < nsp; k++)
            if (WK.sp[k] > 0 && syn_family_at(k)->root == root &&
                (best < 0 || beats(WK.sp[k], syn_family_at(k), WK.sp[best], syn_family_at(best))))
                best = k;
    }
    Organism *being = NULL;
    if (best >= 0) {
        uint32_t sid = syn_family_at(best)->id;
        for (int i = 0; i < w.norgs; i++) {
            Organism *o = w.orgs[i];
            if (o->removed || !o->alive || syn_family_of(o) != sid)
                continue;
            if (!being || o->total_children > being->total_children ||
                (o->total_children == being->total_children &&
                 (o->energy > being->energy || (o->energy == being->energy && o->id < being->id))))
                being = o;
        }
    }
    put_i32(&b, cycle);
    put_i32(&b, (int)world_time_units(&w));
    put_i32(&b, c.pop);
    put_i32(&b, c.lines);
    put_u8(&b, ko);
    put_u64(&b, (uint64_t)f);
    if (!being) {
        put_u8(&b, 0);
    } else {
        const Species *ls = syn_family_at(win), *bs = syn_family_at(best);
        int slot = -1;
        for (int k = 0; k < ns; k++)
            if (slot_root[k] && slot_root[k] == ls->id)
                slot = k;
        char name[48], line[48], plan[200];
        syn_family_label(bs, name, sizeof name);
        line_label(ls, line, sizeof line);
        syn_family_plan(being->gc, plan, sizeof plan);
        size_t bn = 0;
        uint8_t *bytes = code_bytes(being->gc, &bn);
        put_u8(&b, bytes ? 1 : 0);
        put_i32(&b, slot);
        put_u8(&b, slot >= 0 && bs->id == ls->id);
        put_i32(&b, WK.rt[win]);
        put_i32(&b, WK.sp[best]);
        put_str(&b, name);
        put_str(&b, line);
        put_str(&b, plan);
        put_blob(&b, bytes, bytes ? bn : 0);
        free(bytes);
    }
    put_u8(&b, (unsigned)ns);
    for (int k = 0; k < ns; k++) {
        int i = slot_root[k] ? syn_family_index(slot_root[k]) : -1;
        put_i32(&b, i >= 0 ? WK.rt[i] : 0);
    }
    /* for the record: the lone bloodline's time, the winning bloodline's
     * genesis being (the code its root began the round with), and the
     * curve, at most REC_CURVE points of it evenly through the round */
    put_i32(&b, t_lone);
    const Species *root = win >= 0 ? syn_family_at(win) : NULL;
    size_t gn = 0;
    uint8_t *gbytes = root && root->gc ? code_bytes(root->gc, &gn) : NULL;
    char gname[48] = "", gplan[200] = "";
    if (root) {
        syn_family_label(root, gname, sizeof gname);
        if (root->gc)
            syn_family_plan(root->gc, gplan, sizeof gplan);
    }
    put_str(&b, gname);
    put_str(&b, gplan);
    put_blob(&b, gbytes, gbytes ? gn : 0);
    free(gbytes);
    int points = ncurve < REC_CURVE ? ncurve : REC_CURVE;
    put_u32(&b, (uint32_t)points);
    for (int i = 0; i < points; i++) {
        const RecordPoint *q = &curve[points < 2 ? 0 : (int)((int64_t)i * (ncurve - 1) / (points - 1))];
        put_i32(&b, q->t);
        put_i32(&b, q->pop);
        put_i32(&b, q->lines);
    }
    free(curve);
    /* test hook: SYN3D_ARENA_WORLDS=dir keeps each cycle's world as it
     * ended (dir/cycle-N.b3w), to be compared with the round made again
     * (tests/arena_check.c); saved before the result is told, after which
     * the worker may be stopped */
    const char *keep = getenv("SYN3D_ARENA_WORLDS");
    if (keep && *keep) {
        char path[1100], err[256];
        snprintf(path, sizeof path, "%s/cycle-%d.b3w", keep, cycle);
        if (!worldfile_save(&w, path, err, (int)sizeof err))
            fprintf(stderr, "arena worker: %s: %s\n", path, err);
    }
    send_msg(fd, 'R', &b);
    free(b.p);

    world_free(&w);
    for (int k = 0; k < ns; k++)
        drop(code[k]);
}

int syn_arena_worker(int argc, char **argv)
{
    bool nice = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--nice"))
            nice = true;
    /* the messages go out on what was standard output; anything printed
     * goes to standard error instead */
    int fd = arena_os_worker_begin(nice);
    if (fd < 0)
        return 0;
    Buf job = {0};
    for (;;) {
        uint8_t h[5];
        if (!read_all(0, h, 5))
            break;
        uint32_t len = le32(h + 1);
        if (h[0] != 'J' || len > (64u << 20))
            break;
        job.n = 0;
        need(&job, len);
        if (!read_all(0, job.p, len))
            break;
        run_cycle(fd, job.p, len);
    }
    free(job.p);
    syn_family_free();
    return 0;
}

/* ================= the arena: workers, the ladder ================= */

typedef struct {
    ArenaProc proc;
    Buf rx;
    bool up, busy;
    /* the round given it, as given: its settings may change meanwhile */
    int cycle;
    int level; /* 0: qualifying */
    int64_t seed;
    uint64_t fp;
    int map, units, wild;
    bool knockout;
    int nslot;
    Params params; /* the round's: P with its map, when it was given */
    uint32_t slot_uid[ARENA_SLOTS];
    uint32_t slot_root[ARENA_SLOTS];
    double started;
    struct {
        uint32_t id;
        GeneticCode *gc;
    } cache[CACHE];
    int cache_at;
} Worker;

static struct {
    bool inited, cli, quiet, board_set, loaded;
    ArenaSettings set;   /* what runs */
    ArenaSettings saved; /* what is kept with the board (the window's) */
    char path[1100];     /* the board ("": none) */
    ArenaEntry *e;
    int n, cap;
    uint32_t next_uid;
    int cycles; /* given out, all time */
    bool dirty;
    bool foreign; /* the ladder named is the other simulation's: left alone */
    double saved_at;
    /* the ranking of the entries with the current settings */
    int *rank;
    int nrank, rank_cap;
    uint64_t rank_fp;
    bool rank_dirty;
    /* the run */
    bool running;
    int nlanes;
    Worker wk[ARENA_LANES];
    ArenaLane lane[ARENA_LANES];
    int given, done;
    double t0, frames;
    int respawns;
    ArenaBeing feed[ARENA_FEED];
    int nfeed, feed_at;
    ArenaResult *res;
    int nres, res_cap;
    /* how long rounds take with these settings (est_fp, est_units) */
    uint64_t est_fp;
    int est_units, est_n;
    double est_secs;
} A;

void syn_arena_set_board(const char *path)
{
    if (A.inited)
        return;
    A.board_set = true;
    snprintf(A.path, sizeof A.path, "%s", path ? path : "");
}

ArenaSettings *syn_arena_settings(void)
{
    syn_arena_init();
    return &A.set;
}

void syn_arena_settings_changed(void)
{
    A.saved = A.set;
    A.dirty = true;
    A.rank_dirty = true;
}


/* ---- the ladder ---- */

static void entry_free(ArenaEntry *e)
{
    hold(&e->gc, NULL);
    free(e->bgg);
    e->bgg = NULL;
}

/* the highest level first; at a level, those in the running, then the
 * most wins, then the first there */
static int cmp_rank(const void *a, const void *b)
{
    const ArenaEntry *x = &A.e[*(const int *)a], *y = &A.e[*(const int *)b];
    if (x->level != y->level)
        return y->level - x->level;
    if ((x->state == ENTRY_IN) != (y->state == ENTRY_IN))
        return x->state == ENTRY_IN ? -1 : 1;
    if (x->wins != y->wins)
        return y->wins - x->wins;
    if (x->last != y->last)
        return x->last - y->last;
    return (x->uid > y->uid) - (x->uid < y->uid);
}

static void ensure_rank(void)
{
    uint64_t fp = syn_arena_fingerprint();
    if (!A.rank_dirty && fp == A.rank_fp)
        return;
    A.rank_fp = fp;
    A.rank_dirty = false;
    if (A.n > A.rank_cap) {
        A.rank_cap = A.n * 2;
        A.rank = realloc(A.rank, (size_t)A.rank_cap * sizeof *A.rank);
        if (!A.rank)
            abort();
    }
    A.nrank = 0;
    for (int i = 0; i < A.n; i++)
        if (A.e[i].settings == fp)
            A.rank[A.nrank++] = i;
    qsort(A.rank, (size_t)A.nrank, sizeof *A.rank, cmp_rank);
}

int syn_arena_count(void)
{
    syn_arena_init();
    ensure_rank();
    return A.nrank;
}

const ArenaEntry *syn_arena_at(int rank)
{
    ensure_rank();
    return rank >= 0 && rank < A.nrank ? &A.e[A.rank[rank]] : NULL;
}

int syn_arena_others(void)
{
    ensure_rank();
    return A.n - A.nrank;
}

static ArenaEntry *find(uint32_t uid)
{
    for (int i = 0; i < A.n; i++)
        if (A.e[i].uid == uid)
            return &A.e[i];
    return NULL;
}

const ArenaEntry *syn_arena_find(uint32_t uid)
{
    return uid ? find(uid) : NULL;
}

int syn_arena_top_level(void)
{
    ensure_rank();
    for (int i = 0; i < A.nrank; i++)
        if (A.e[A.rank[i]].state == ENTRY_IN)
            return A.e[A.rank[i]].level;
    return 0;
}

static bool in_round(uint32_t uid)
{
    for (int l = 0; l < A.nlanes; l++)
        if (A.wk[l].busy)
            for (int k = 0; k < A.wk[l].nslot; k++)
                if (A.wk[l].slot_uid[k] == uid)
                    return true;
    return false;
}

bool syn_arena_in_round(uint32_t uid)
{
    return uid && in_round(uid);
}

/* the one to forget first: out of the running before in it, then the
 * lowest level, then the oldest */
static bool sooner_gone(const ArenaEntry *x, const ArenaEntry *y)
{
    if ((x->state == ENTRY_IN) != (y->state == ENTRY_IN))
        return y->state == ENTRY_IN;
    if (x->level != y->level)
        return x->level < y->level;
    return x->last < y->last;
}

static ArenaEntry *add_entry(void)
{
    if (A.n >= ARENA_BOARD) {
        /* full: one goes, never one fighting now */
        int out = -1;
        for (int i = 0; i < A.n; i++) {
            if (in_round(A.e[i].uid))
                continue;
            if (out < 0 || sooner_gone(&A.e[i], &A.e[out]))
                out = i;
        }
        if (out < 0)
            return NULL;
        entry_free(&A.e[out]);
        A.e[out] = A.e[--A.n];
    }
    if (A.n == A.cap) {
        A.cap = A.cap ? A.cap * 2 : 64;
        A.e = realloc(A.e, (size_t)A.cap * sizeof *A.e);
        if (!A.e)
            abort();
    }
    ArenaEntry *e = &A.e[A.n++];
    memset(e, 0, sizeof *e);
    e->uid = A.next_uid++;
    A.rank_dirty = true;
    A.dirty = true;
    return e;
}

static void config_dir_make(void)
{
    const char *dir = params_config_dir();
    char parent[1024];
    snprintf(parent, sizeof parent, "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        mkdir(parent, 0755);
    }
    mkdir(dir, 0755);
}

static void put_settings(Buf *b, const ArenaSettings *s)
{
    put_i32(b, s->units);
    put_i32(b, s->workers);
    put_i32(b, s->wild_pct);
    put_i32(b, s->seats);
    put_i32(b, s->map);
    put_i32(b, s->target);
    put_u8(b, s->knockout);
    put_u64(b, (uint64_t)s->seed);
}

static void get_settings(Rd *r, ArenaSettings *s)
{
    s->units = get_i32(r);
    s->workers = get_i32(r);
    s->wild_pct = get_i32(r);
    s->seats = get_i32(r);
    s->map = get_i32(r);
    s->target = get_i32(r);
    s->knockout = get_u8(r) != 0;
    s->seed = (int64_t)get_u64(r);
}

static void clamp_settings(ArenaSettings *s)
{
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
    s->units = CLAMP(s->units, 1, 100000);
    s->workers = CLAMP(s->workers, 1, ARENA_LANES);
    s->wild_pct = CLAMP(s->wild_pct, 0, 90);
    /* a ladder round is two at least */
    s->seats = s->seats == 1 ? 2 : CLAMP(s->seats, 0, ARENA_SLOTS);
    s->map = CLAMP(s->map, 0, MAP_COUNT - 1);
    s->target = CLAMP(s->target, 0, 1000000);
#undef CLAMP
}

static bool board_save(void)
{
    if (!A.path[0])
        return false;
    Buf b = {0};
    put(&b, "SYNARENA", 8);
    put_u32(&b, BOARD_VERSION);
    put_u32(&b, SIM_TAG);
    put_settings(&b, &A.saved);
    put_u32(&b, A.next_uid);
    put_i32(&b, A.cycles);
    put_u32(&b, (uint32_t)A.n);
    for (int i = 0; i < A.n; i++) {
        const ArenaEntry *e = &A.e[i];
        put_u32(&b, e->uid);
        put_u32(&b, e->parent);
        put_str(&b, e->name);
        put_str(&b, e->line);
        put_str(&b, e->plan);
        put_i32(&b, e->cycle);
        put_u64(&b, (uint64_t)e->seed);
        put_i32(&b, e->wins);
        put_i32(&b, e->entered);
        put_f32(&b, e->share);
        put_f32(&b, e->best);
        put_i32(&b, e->alive);
        put_i32(&b, e->pop);
        put_i32(&b, e->t_end);
        put_u64(&b, e->settings);
        put_i32(&b, e->map);
        put_i32(&b, e->start);
        put_i32(&b, e->level);
        put_u8(&b, (unsigned)e->state);
        put_u32(&b, e->by);
        put_i32(&b, e->last);
        put_blob(&b, e->bgg, e->bgg_n);
    }
    config_dir_make();
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", A.path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(b.p, 1, b.n, f) == b.n;
    if (f && fclose(f) != 0)
        ok = false;
    ok = ok && arena_os_replace(tmp, A.path);
    if (!ok) {
        remove(tmp);
        fprintf(stderr, "arena: %s can't be written\n", A.path);
    }
    free(b.p);
    A.dirty = !ok;
    A.saved_at = now_s();
    return ok;
}

/* The board as saved, or none. A file that isn't one is moved aside
 * (name.bad), never overwritten. */
static void board_load(void)
{
    if (!A.path[0])
        return;
    FILE *f = fopen(A.path, "rb");
    if (!f)
        return;
    Buf b = {0};
    uint8_t chunk[65536];
    size_t k;
    while ((k = fread(chunk, 1, sizeof chunk, f)) > 0)
        put(&b, chunk, k);
    fclose(f);
    Rd r = {b.p, b.n, 0, false};
    const uint8_t *magic = take(&r, 8);
    uint32_t version = magic && !memcmp(magic, "SYNARENA", 8) ? get_u32(&r) : 0;
    bool ok = version >= 1 && version <= BOARD_VERSION;
    uint32_t tag = ok ? get_u32(&r) : 0;
    if (ok && tag != SIM_TAG) {
        /* sound, but another program's: not this arena's to change */
        fprintf(stderr, "arena: %s is another program's ladder, not %s's; it is left as it is\n",
                A.path, SIM_NAME);
        free(b.p);
        A.path[0] = '\0';
        A.foreign = true;
        return;
    }
    ok = ok && tag == SIM_TAG;
    ArenaSettings s = A.set;
    uint32_t next_uid = 0, n = 0;
    int cycles = 0;
    if (ok) {
        get_settings(&r, &s);
        next_uid = get_u32(&r);
        cycles = get_i32(&r);
        n = get_u32(&r);
        ok = !r.bad && n <= ARENA_BOARD * 2u && cycles >= 0;
    }
    ArenaEntry *es = ok && n ? calloc(n, sizeof *es) : NULL;
    if (ok && n && !es)
        abort();
    uint32_t got = 0;
    for (; ok && got < n; got++) {
        ArenaEntry *e = &es[got];
        e->uid = get_u32(&r);
        e->parent = get_u32(&r);
        get_str(&r, e->name, sizeof e->name);
        get_str(&r, e->line, sizeof e->line);
        get_str(&r, e->plan, sizeof e->plan);
        e->cycle = get_i32(&r);
        e->seed = (int64_t)get_u64(&r);
        e->wins = get_i32(&r);
        e->entered = get_i32(&r);
        e->share = get_f32(&r);
        e->best = get_f32(&r);
        e->alive = get_i32(&r);
        e->pop = get_i32(&r);
        e->t_end = get_i32(&r);
        e->settings = get_u64(&r);
        e->map = get_i32(&r);
        if (version >= 2) {
            e->start = version >= 3 ? get_i32(&r) : 0;
            e->level = get_i32(&r);
            e->state = (int)get_u8(&r);
            e->by = get_u32(&r);
            e->last = get_i32(&r);
            /* without it: a level a win since it came on, its offspring's
             * win aside (those from before the ladder: level 1) */
            if (version == 2) {
                int up = e->wins - (e->state == ENTRY_ON ? 2 : 1);
                e->start = e->level - (up > 0 ? up : 0);
                if (e->start < 1)
                    e->start = 1;
            }
        } else {
            /* the winners before the ladder: qualified */
            e->start = e->level = 1;
            e->state = ENTRY_IN;
            e->last = e->cycle;
        }
        size_t bn;
        const uint8_t *bp = get_blob(&r, &bn);
        if (r.bad || !bn || e->uid == 0 || e->uid >= next_uid || e->start < 1 || e->level < e->start || e->state < ENTRY_IN ||
            e->state > ENTRY_ON) {
            ok = false;
            break;
        }
        GeneticCode *gc = code_parse(bp, bn);
        if (!gc) {
            ok = false;
            break;
        }
        hold(&e->gc, gc);
        drop(gc);
        e->bgg = malloc(bn);
        if (!e->bgg)
            abort();
        memcpy(e->bgg, bp, bn);
        e->bgg_n = bn;
    }
    ok = ok && r.at == r.n;
    /* one from before the ladder is kept as it was (name.v1), once: what
     * is saved here from now on, the older arena can't read */
    if (ok && version == 1) {
        char old[1200];
        snprintf(old, sizeof old, "%s.v1", A.path);
        FILE *g = fopen(old, "rb");
        if (g)
            fclose(g);
        else if ((g = fopen(old, "wb"))) {
            bool kept = fwrite(b.p, 1, b.n, g) == b.n;
            if (fclose(g) != 0 || !kept) {
                remove(old);
                fprintf(stderr, "arena: %s can't be written\n", old);
            }
        }
    }
    free(b.p);
    if (!ok) {
        for (uint32_t i = 0; i < got && es; i++)
            entry_free(&es[i]);
        free(es);
        char bad[1200];
        snprintf(bad, sizeof bad, "%s.bad", A.path);
        if (arena_os_replace(A.path, bad))
            fprintf(stderr, "arena: %s isn't a ladder this program reads; moved to %s\n", A.path, bad);
        return;
    }
    /* the settings before the ladder meant other things: its own instead,
     * the computer's share as it was */
    if (version == 1) {
        int workers = s.workers;
        s = A.set;
        s.workers = workers;
    }
    clamp_settings(&s);
    A.set = A.saved = s;
    A.loaded = true;
    A.e = es;
    A.n = A.cap = (int)n;
    A.next_uid = next_uid;
    A.cycles = cycles;
    A.rank_dirty = true;
}

void syn_arena_init(void)
{
    if (A.inited)
        return;
    A.inited = true;
    ArenaSettings *s = &A.set;
    s->units = 5 * (P.max_age > 0 ? P.max_age : 1); /* five lifetimes */
    s->workers = syn_arena_cores() > 1 ? syn_arena_cores() - 1 : 1;
    s->wild_pct = 0;
    s->seats = 2;
    s->map = MAP_SMALL;
    s->target = 0;
    s->knockout = true;
    s->seed = (int64_t)time(NULL) * 1000;
    A.saved = *s;
    A.next_uid = 1;
    if (!A.cli && !A.board_set)
        snprintf(A.path, sizeof A.path, "%s/%s", params_config_dir(), BOARD_FILE);
    /* the record beside the ladder (none without one) */
    char rp[1200] = "";
    size_t pl = strlen(A.path);
    if (pl > 6 && !strcmp(A.path + pl - 6, ".board"))
        snprintf(rp, sizeof rp, "%.*s.record", (int)(pl - 6), A.path);
    else if (pl)
        snprintf(rp, sizeof rp, "%s.record", A.path);
    syn_record_set_file(rp);
    board_load();
}

/* ---- the lanes ---- */

static void cache_put(Worker *k, uint32_t id, GeneticCode *gc)
{
    for (int i = 0; i < CACHE; i++)
        if (k->cache[i].id == id) {
            hold(&k->cache[i].gc, gc);
            return;
        }
    k->cache[k->cache_at].id = id;
    hold(&k->cache[k->cache_at].gc, gc);
    k->cache_at = (k->cache_at + 1) % CACHE;
}

static GeneticCode *cache_get(Worker *k, uint32_t id)
{
    for (int i = 0; i < CACHE; i++)
        if (k->cache[i].id == id && id)
            return k->cache[i].gc;
    return NULL;
}

static void cache_clear(Worker *k)
{
    for (int i = 0; i < CACHE; i++) {
        hold(&k->cache[i].gc, NULL);
        k->cache[i].id = 0;
    }
    k->cache_at = 0;
}

static void lane_reset(ArenaLane *L)
{
    L->busy = false;
    L->t = L->pop = L->lines = 0;
    L->fps = 0;
    L->nslots = 0;
    L->nseries = 0;
    L->nsamp = 0;
    L->leader = 0;
    L->leader_name[0] = '\0';
    L->leader_alive = 0;
    hold(&L->leader_gc, NULL);
}

static bool spawn(int lane, char *err, size_t errn)
{
    Worker *k = &A.wk[lane];
    if (!arena_os_start(&k->proc, !A.cli, err, errn))
        return false;
    k->rx.n = 0;
    k->up = true;
    k->busy = false;
    k->started = now_s();
    A.lane[lane].up = true;
    lane_reset(&A.lane[lane]);
    return true;
}

static void worker_end(int lane)
{
    Worker *k = &A.wk[lane];
    if (!k->up)
        return;
    arena_os_end(&k->proc);
    k->up = k->busy = false;
    k->nslot = 0;
    cache_clear(k);
    A.lane[lane].up = false;
    lane_reset(&A.lane[lane]);
}

static int cmp_waited(const void *a, const void *b)
{
    const ArenaEntry *x = &A.e[*(const int *)a], *y = &A.e[*(const int *)b];
    if (x->last != y->last)
        return x->last - y->last;
    return (x->uid > y->uid) - (x->uid < y->uid);
}

/* The next round: of the highest level with a seat for each fighter among
 * those waiting, the ones there longest (who: their entries); else a
 * qualifying round. Its level, 0 for qualifying. */
static int pick(int *who, int *n)
{
    *n = 0;
    int seats = A.set.seats;
    if (seats < 2 || A.nrank < seats)
        return 0;
    int *cand = malloc((size_t)A.nrank * sizeof *cand);
    if (!cand)
        abort();
    int level = 0;
    /* the ranking has the highest level first: each level in turn */
    for (int i = 0, j; i < A.nrank && !level; i = j) {
        int lv = A.e[A.rank[i]].level, m = 0;
        for (j = i; j < A.nrank && A.e[A.rank[j]].level == lv; j++) {
            const ArenaEntry *e = &A.e[A.rank[j]];
            if (e->state == ENTRY_IN && !in_round(e->uid))
                cand[m++] = A.rank[j];
        }
        if (m >= seats) {
            qsort(cand, (size_t)m, sizeof *cand, cmp_waited);
            memcpy(who, cand, (size_t)seats * sizeof *who);
            *n = seats;
            level = lv;
        }
    }
    free(cand);
    return level;
}

/* A round's job to a worker: these fighters, in this order. */
static bool send_job(Worker *k, int cycle, int64_t seed, int units, bool knockout, int wild,
                     const ArenaEntry *const *f, int nf)
{
    const Params q = k->params;
    Buf b = {0};
    put_i32(&b, cycle);
    put_u64(&b, (uint64_t)seed);
    put_i32(&b, units);
    put_i32(&b, (int)((int64_t)cycle * NAME_STEP % 500));
    put_u8(&b, knockout);
    put_i32(&b, wild);
    put_blob(&b, &q, sizeof q);
    put_u8(&b, (unsigned)nf);
    for (int d = 0; d < nf; d++)
        put_blob(&b, f[d]->bgg, f[d]->bgg_n);
    bool ok = send_msg(k->proc.to, 'J', &b);
    free(b.p);
    return ok;
}

static void dispatch(int lane)
{
    Worker *k = &A.wk[lane];
    ArenaLane *L = &A.lane[lane];
    if (!A.running || !k->up || k->busy)
        return;
    if (A.set.target > 0 && A.given >= A.set.target)
        return;
    ensure_rank();
    int who[ARENA_SLOTS], nd;
    int level = pick(who, &nd);
    int cycle = ++A.cycles;
    A.given++;
    A.dirty = true;
    const ArenaEntry *f[ARENA_SLOTS];
    lane_reset(L);
    for (int d = 0; d < nd; d++) {
        const ArenaEntry *e = f[d] = &A.e[who[d]];
        k->slot_uid[d] = e->uid;
        k->slot_root[d] = 0;
        snprintf(L->slot_name[d], sizeof L->slot_name[d], "%s", e->name);
        L->slot_alive[d] = 0;
    }
    k->nslot = nd;
    k->cycle = cycle;
    k->level = level;
    k->seed = A.set.seed + cycle;
    k->fp = syn_arena_fingerprint();
    k->map = A.set.map;
    k->units = A.set.units;
    k->wild = A.set.wild_pct;
    k->knockout = A.set.knockout;
    round_params(&k->params, A.set.map);
    k->busy = true;
    k->started = now_s();
    cache_clear(k);
    L->busy = true;
    L->cycle = cycle;
    L->level = level;
    L->seed = A.set.seed + cycle;
    L->units = A.set.units;
    L->nslots = nd;
    if (!send_job(k, cycle, A.set.seed + cycle, A.set.units, A.set.knockout, A.set.wild_pct, f, nd))
        k->busy = L->busy = false; /* it died: read as such */
}

static void sample_add(ArenaLane *L, const ArenaSample *s)
{
    if (L->nsamp == L->samp_cap) {
        L->samp_cap = L->samp_cap ? L->samp_cap * 2 : 512;
        L->samp = realloc(L->samp, (size_t)L->samp_cap * sizeof *L->samp);
        if (!L->samp)
            abort();
    }
    L->samp[L->nsamp++] = *s;
}

static void feed_add(const char *name, int cycle, int alive, int lane, GeneticCode *gc)
{
    ArenaBeing *b = &A.feed[A.feed_at];
    snprintf(b->name, sizeof b->name, "%s", name);
    b->cycle = cycle;
    b->alive = alive;
    b->lane = lane;
    hold(&b->gc, gc);
    A.feed_at = (A.feed_at + 1) % ARENA_FEED;
    if (A.nfeed < ARENA_FEED)
        A.nfeed++;
}

/* ---- rounds made again ---- */

static bool spec_code(ArenaRoundSpec *s, const uint8_t *bgg, size_t n, const char *name)
{
    GeneticCode *gc = code_parse(bgg, n);
    if (!gc)
        return false;
    s->code[s->nf] = gc;
    snprintf(s->name[s->nf], sizeof s->name[0], "%s", name);
    s->nf++;
    return true;
}

bool syn_arena_lane_round(int lane, ArenaRoundSpec *out)
{
    memset(out, 0, sizeof *out);
    if (lane < 0 || lane >= A.nlanes || !A.wk[lane].busy)
        return false;
    const Worker *k = &A.wk[lane];
    out->params = k->params;
    out->seed = k->seed;
    out->wild = k->wild;
    out->units = k->units;
    out->cycle = k->cycle;
    out->level = k->level;
    for (int d = 0; d < k->nslot; d++) {
        const ArenaEntry *e = find(k->slot_uid[d]);
        if (!e || !spec_code(out, e->bgg, e->bgg_n, e->name)) {
            syn_arena_round_spec_free(out);
            return false;
        }
    }
    return true;
}

bool syn_arena_record_round(int i, ArenaRoundSpec *out)
{
    memset(out, 0, sizeof *out);
    const RecordRound *r = syn_record_at(i);
    if (!r || !r->params)
        return false;
    params_set_defaults(&out->params);
    params_read_text(&out->params, r->params);
    out->seed = r->seed;
    out->wild = r->wild;
    out->units = r->units;
    out->cycle = r->cycle;
    out->level = r->level;
    for (int d = 0; d < r->nf; d++)
        if (!spec_code(out, r->f[d].bgg, r->f[d].bgg_n, r->f[d].name)) {
            syn_arena_round_spec_free(out);
            return false;
        }
    return true;
}

void syn_arena_round_spec_free(ArenaRoundSpec *s)
{
    for (int d = 0; d < s->nf; d++)
        drop(s->code[d]);
    s->nf = 0;
}

const char *syn_outcome_name(int outcome)
{
    static const char *const k[OUT_KINDS] = {"QUALIFIED", "WON", "WON AS ITS OFFSPRING", "UPSET", "NO SURVIVORS"};
    return outcome >= 0 && outcome < OUT_KINDS ? k[outcome] : "?";
}

static void result_add(const ArenaResult *r)
{
    if (A.nres == A.res_cap) {
        A.res_cap = A.res_cap ? A.res_cap * 2 : 256;
        A.res = realloc(A.res, (size_t)A.res_cap * sizeof *A.res);
        if (!A.res)
            abort();
    }
    A.res[A.nres++] = *r;
}

/* the ladder told of a round's end: who goes up, who is out */
/* what a worker tells at a round's end (run_cycle's 'R') */
typedef struct {
    int cycle, t_end, pop, lines;
    bool ko, won, own;
    uint64_t frames;
    int slot, line_alive, alive;
    char name[40], line[40], plan[160];
    const uint8_t *bp, *gp; /* the final and genesis beings' .bgg files */
    size_t bn, gn;
    int nsl;
    int slot_alive[ARENA_SLOTS];
    RecordRound rec; /* the lone bloodline's time, the genesis being's names, the curve */
} Told;

static void read_told(Rd *r, Told *t)
{
    memset(t, 0, sizeof *t);
    t->slot = -1;
    t->cycle = get_i32(r);
    t->t_end = get_i32(r);
    t->pop = get_i32(r);
    t->lines = get_i32(r);
    t->ko = get_u8(r) != 0;
    t->frames = get_u64(r);
    t->won = get_u8(r) != 0;
    if (t->won) {
        t->slot = get_i32(r);
        t->own = get_u8(r) != 0;
        t->line_alive = get_i32(r);
        t->alive = get_i32(r);
        get_str(r, t->name, sizeof t->name);
        get_str(r, t->line, sizeof t->line);
        get_str(r, t->plan, sizeof t->plan);
        t->bp = get_blob(r, &t->bn);
    }
    t->nsl = (int)get_u8(r);
    for (int j = 0; j < t->nsl; j++) {
        int v = get_i32(r);
        if (j < ARENA_SLOTS)
            t->slot_alive[j] = v;
    }
    RecordRound *rec = &t->rec;
    rec->t_lone = get_i32(r);
    get_str(r, rec->gname, sizeof rec->gname);
    get_str(r, rec->gplan, sizeof rec->gplan);
    t->gp = get_blob(r, &t->gn);
    uint32_t nc = get_u32(r);
    for (uint32_t j = 0; j < nc && !r->bad; j++) {
        RecordPoint q;
        q.t = get_i32(r);
        q.pop = get_i32(r);
        q.lines = get_i32(r);
        if (rec->ncurve < REC_CURVE)
            rec->curve[rec->ncurve++] = q;
    }
}

static void on_result(int lane, Rd *r)
{
    Worker *k = &A.wk[lane];
    ArenaLane *L = &A.lane[lane];
    Told *t = malloc(sizeof *t);
    if (!t)
        abort();
    read_told(r, t);
    ArenaResult res = {0};
    res.cycle = t->cycle;
    res.t_end = t->t_end;
    res.knockout = t->ko;
    int pop = t->pop, lines = t->lines, slot = t->slot, line_alive = t->line_alive, alive = t->alive;
    uint64_t frames = t->frames;
    bool won = t->won, own = t->own;
    const char *name = t->name, *line = t->line, *plan = t->plan;
    const uint8_t *bp = t->bp, *gp = t->gp;
    size_t bn = t->bn, gn = t->gn;
    RecordRound *rec = &t->rec;
    for (int j = 0; j < t->nsl && j < ARENA_SLOTS; j++)
        L->slot_alive[j] = t->slot_alive[j];
    L->busy = false;
    if (r->bad || res.cycle != k->cycle || slot >= k->nslot) {
        k->busy = false;
        fprintf(stderr, "arena: round %d's result couldn't be read\n", k->cycle);
        free(t);
        return;
    }
    A.done++;
    A.frames += (double)frames;
    if (A.est_fp != k->fp || A.est_units != k->units) {
        A.est_fp = k->fp;
        A.est_units = k->units;
        A.est_n = 0;
        A.est_secs = 0;
    }
    A.est_secs += now_s() - k->started;
    A.est_n++;
    res.level = k->level;
    res.share = pop > 0 ? (float)line_alive / (float)pop : 0;
    uint32_t winner = slot >= 0 ? k->slot_uid[slot] : 0;
    GeneticCode *gc = won ? code_parse(bp, bn) : NULL;

    /* who goes up: the fighter that won, else a new being (made while the
     * fighters are still in the round, so none of them is forgotten for
     * room) */
    ArenaEntry *up = NULL;
    if (!won || !gc) {
        res.outcome = OUT_EXTINCT;
    } else if (winner && own) {
        res.outcome = OUT_ADVANCED;
        up = find(winner);
    } else {
        res.outcome = k->level == 0 ? OUT_QUALIFIED : winner ? OUT_EVOLVED : OUT_UPSET;
        up = add_entry();
        if (up) {
            const ArenaEntry *par = winner ? find(winner) : NULL;
            up->parent = winner;
            snprintf(up->name, sizeof up->name, "%s", name);
            snprintf(up->line, sizeof up->line, "%s", par ? par->line : line);
            snprintf(up->plan, sizeof up->plan, "%s", plan);
            up->cycle = res.cycle;
            up->seed = k->seed;
            up->share = res.share;
            up->alive = alive;
            up->pop = pop;
            up->t_end = res.t_end;
            up->settings = k->fp;
            up->map = k->map;
            up->start = k->level + 1;
            hold(&up->gc, gc);
            up->bgg = malloc(bn);
            if (!up->bgg)
                abort();
            memcpy(up->bgg, bp, bn);
            up->bgg_n = bn;
        }
    }
    drop(gc);
    k->busy = false;
    if (up) {
        up->level = k->level + 1;
        up->wins++;
        up->state = ENTRY_IN;
        up->by = 0;
        up->last = res.cycle;
        if (res.share > up->best)
            up->best = res.share;
        res.uid = up->uid;
        snprintf(res.name, sizeof res.name, "%s", up->name);
    }
    /* the fighters: each fought; the one whose offspring went up won too,
     * and the rest are out (all of them with no survivors) */
    if (up || res.outcome == OUT_EXTINCT)
        for (int d = 0; d < k->nslot; d++) {
            ArenaEntry *e = find(k->slot_uid[d]);
            if (!e)
                continue;
            e->entered++;
            if (e == up)
                continue;
            e->last = res.cycle;
            e->by = up ? up->uid : 0;
            if (e->uid == winner) {
                e->wins++;
                if (res.share > e->best)
                    e->best = res.share;
                e->state = ENTRY_ON;
            } else {
                e->state = ENTRY_OUT;
                res.nout++;
            }
        }
    A.rank_dirty = true;
    A.dirty = true;
    result_add(&res);

    /* the round, for good */
    rec->source = REC_LIVE;
    rec->when = (int64_t)time(NULL);
    rec->cycle = res.cycle;
    rec->level = res.level;
    rec->outcome = res.outcome;
    rec->seed = k->seed;
    rec->settings = k->fp;
    rec->map = k->map;
    rec->units = k->units;
    rec->knockout = k->knockout;
    rec->wild = k->wild;
    rec->t_end = res.t_end;
    rec->pop = pop;
    rec->lines = lines;
    rec->share = res.share;
    rec->nf = k->nslot < REC_FIGHTERS ? k->nslot : REC_FIGHTERS;
    for (int d = 0; d < rec->nf; d++) {
        const ArenaEntry *fe = find(k->slot_uid[d]);
        rec->f[d].uid = k->slot_uid[d];
        snprintf(rec->f[d].name, sizeof rec->f[d].name, "%s", L->slot_name[d]);
        rec->f[d].alive = L->slot_alive[d];
        rec->f[d].bgg = fe ? fe->bgg : NULL;
        rec->f[d].bgg_n = fe ? fe->bgg_n : 0;
    }
    rec->uid = res.uid;
    if (won) {
        /* the fighter itself went up: by its name on the ladder */
        snprintf(rec->name, sizeof rec->name, "%s", res.outcome == OUT_ADVANCED && up ? up->name : name);
        snprintf(rec->line, sizeof rec->line, "%s", up ? up->line : line);
        snprintf(rec->plan, sizeof rec->plan, "%s", plan);
    }
    /* a fighter's bloodline began the round as the fighter */
    const ArenaEntry *fw = winner ? find(winner) : NULL;
    if (fw) {
        snprintf(rec->gname, sizeof rec->gname, "%s", fw->name);
        snprintf(rec->gplan, sizeof rec->gplan, "%s", fw->plan);
    }
    rec->params = params_text(&k->params);
    syn_record_add(rec, won ? bp : NULL, won ? bn : 0, gp, gn);
    free(rec->params);
    rec->params = NULL;
    if (A.cli && !A.quiet) {
        char what[300], where[24];
        const ArenaEntry *par = up && up->parent ? find(up->parent) : NULL;
        if (res.level)
            snprintf(where, sizeof where, "level %d", res.level);
        else
            snprintf(where, sizeof where, "qualifying");
        if (res.outcome == OUT_EXTINCT)
            snprintf(what, sizeof what, "no survivors%s", k->nslot ? ": all knocked out" : "");
        else if (!up)
            snprintf(what, sizeof what, "%-16s won, but the ladder is full", name);
        else
            snprintf(what, sizeof what, "%-16s bloodline %-14s %3.0f%% of %d living   %s%s%s: level %d",
                     up->name, up->line, res.share * 100, pop, syn_outcome_name(res.outcome),
                     res.outcome == OUT_EVOLVED ? " of " : "", res.outcome == OUT_EVOLVED && par ? par->name : "",
                     up->level);
        printf("round %5d  %-10s  t %5d%s  %s\n", res.cycle, where, res.t_end, res.knockout ? " KO" : "   ", what);
        fflush(stdout);
    }
    free(t);
}

static void on_msg(int lane, int type, Rd *r)
{
    Worker *k = &A.wk[lane];
    ArenaLane *L = &A.lane[lane];
    switch (type) {
    case 'S': {
        int cycle = get_i32(r);
        int ns = (int)get_u8(r);
        for (int j = 0; j < ns && j < ARENA_SLOTS; j++) {
            k->slot_root[j] = get_u32(r);
            L->slot_alive[j] = get_i32(r);
        }
        (void)cycle;
        break;
    }
    case 'U': {
        ArenaSample s = {0};
        L->t = s.t = get_i32(r);
        L->pop = s.pop = get_i32(r);
        L->lines = get_i32(r);
        L->fps = get_f32(r);
        int nser = (int)get_u8(r);
        float sum = 0;
        for (int i = 0; i < nser; i++) {
            uint32_t root = get_u32(r);
            int alive = get_i32(r);
            int slot = (int)get_u8(r) - 1;
            char label[40];
            get_str(r, label, sizeof label);
            if (i >= ARENA_SERIES)
                continue;
            L->series_root[i] = root;
            L->series_slot[i] = slot;
            snprintf(L->series_label[i], sizeof L->series_label[i], "%s",
                     slot >= 0 && slot < L->nslots ? L->slot_name[slot] : label);
            s.n[i] = (float)alive;
            sum += (float)alive;
        }
        L->nseries = nser < ARENA_SERIES ? nser : ARENA_SERIES;
        s.n[ARENA_SERIES] = (float)s.pop > sum ? (float)s.pop - sum : 0;
        int nsl = (int)get_u8(r);
        for (int j = 0; j < nsl; j++) {
            int v = get_i32(r);
            if (j < ARENA_SLOTS)
                L->slot_alive[j] = v;
        }
        L->leader = get_u32(r);
        L->leader_alive = get_i32(r);
        get_str(r, L->leader_name, sizeof L->leader_name);
        hold(&L->leader_gc, cache_get(k, L->leader));
        if (!r->bad)
            sample_add(L, &s);
        break;
    }
    case 'B': {
        uint32_t id = get_u32(r);
        bool feed = get_u8(r) != 0;
        int alive = get_i32(r);
        char label[40];
        get_str(r, label, sizeof label);
        size_t bn;
        const uint8_t *bp = get_blob(r, &bn);
        GeneticCode *gc = r->bad ? NULL : code_parse(bp, bn);
        if (!gc)
            break;
        cache_put(k, id, gc);
        if (feed)
            feed_add(label, k->cycle, alive, lane, gc);
        drop(gc);
        break;
    }
    case 'R':
        on_result(lane, r);
        break;
    default:
        break;
    }
}

/* what a worker has said; false when it has gone */
static bool pump(int lane, bool *changed)
{
    Worker *k = &A.wk[lane];
    bool eof = false;
    for (;;) {
        need(&k->rx, 65536);
        long m = arena_os_read(&k->proc, k->rx.p + k->rx.n, k->rx.cap - k->rx.n);
        if (m > 0) {
            k->rx.n += (size_t)m;
            *changed = true;
            continue;
        }
        if (m < 0)
            eof = true;
        break;
    }
    size_t at = 0;
    while (k->rx.n - at >= 5) {
        uint32_t len = le32(k->rx.p + at + 1);
        if (k->rx.n - at - 5 < len)
            break;
        Rd r = {k->rx.p + at + 5, len, 0, false};
        on_msg(lane, k->rx.p[at], &r);
        at += 5 + len;
    }
    if (at) {
        memmove(k->rx.p, k->rx.p + at, k->rx.n - at);
        k->rx.n -= at;
    }
    return !eof;
}

bool syn_arena_start(char *err, size_t n)
{
    syn_arena_init();
    if (A.running)
        return true;
    clamp_settings(&A.set);
    A.nlanes = A.set.workers;
    A.given = A.done = 0;
    A.frames = 0;
    A.respawns = 0;
    A.t0 = now_s();
    int up = 0;
    char why[200] = "";
    for (int i = 0; i < A.nlanes; i++)
        up += spawn(i, why, sizeof why);
    if (!up) {
        snprintf(err, n, "%s", why);
        return false;
    }
    A.running = true;
    for (int i = 0; i < A.nlanes; i++)
        dispatch(i);
    return true;
}

void syn_arena_stop(void)
{
    for (int i = 0; i < ARENA_LANES; i++)
        worker_end(i);
    if (A.running && A.dirty)
        board_save();
    A.running = false;
}

bool syn_arena_running(void)
{
    return A.running;
}

bool syn_arena_poll(void)
{
    if (!A.running)
        return false;
    bool changed = false;
    for (int i = 0; i < A.nlanes; i++) {
        Worker *k = &A.wk[i];
        if (!k->up)
            continue;
        if (!pump(i, &changed)) {
            /* it died: its cycle is lost; another takes its place */
            bool young = now_s() - k->started < 1.0;
            worker_end(i);
            fprintf(stderr, "arena: worker %d stopped%s\n", i + 1, young ? " as it started" : "");
            char err[200];
            if (++A.respawns <= 8 && spawn(i, err, sizeof err))
                dispatch(i);
            changed = true;
        }
    }
    for (int i = 0; i < A.nlanes; i++)
        dispatch(i);
    bool busy = false, up = false;
    for (int i = 0; i < A.nlanes; i++) {
        busy |= A.wk[i].busy;
        up |= A.wk[i].up;
    }
    /* the target reached, or no worker left */
    if (!busy && (!up || (A.set.target > 0 && A.given >= A.set.target))) {
        syn_arena_stop();
        changed = true;
    } else if (A.dirty && now_s() - A.saved_at > 10) {
        board_save();
    }
    return changed;
}

int syn_arena_lane_count(void)
{
    return A.nlanes;
}

const ArenaLane *syn_arena_lane(int i)
{
    return i >= 0 && i < A.nlanes ? &A.lane[i] : NULL;
}

int syn_arena_feed_count(void)
{
    return A.nfeed;
}

const ArenaBeing *syn_arena_feed(int i)
{
    if (i < 0 || i >= A.nfeed)
        return NULL;
    return &A.feed[(A.feed_at - 1 - i + ARENA_FEED) % ARENA_FEED];
}

int syn_arena_result_count(void)
{
    return A.nres;
}

const ArenaResult *syn_arena_result(int i)
{
    return i >= 0 && i < A.nres ? &A.res[i] : NULL;
}

int syn_arena_cycles(void)
{
    syn_arena_init();
    return A.cycles;
}

double syn_arena_round_seconds(void)
{
    if (!A.est_n || A.est_fp != syn_arena_fingerprint() || A.est_units != A.set.units)
        return 0;
    return A.est_secs / A.est_n;
}

void syn_arena_run_stats(int *cycles, double *seconds, double *frames)
{
    *cycles = A.done;
    *seconds = A.t0 > 0 ? now_s() - A.t0 : 0;
    *frames = A.frames;
}

void syn_arena_clear(void)
{
    syn_arena_init();
    uint64_t fp = syn_arena_fingerprint();
    int j = 0;
    for (int i = 0; i < A.n; i++) {
        if (A.e[i].settings == fp && !in_round(A.e[i].uid))
            entry_free(&A.e[i]);
        else
            A.e[j++] = A.e[i];
    }
    A.n = j;
    A.rank_dirty = true;
    A.dirty = true;
    board_save();
}

const ArenaEntry *syn_arena_enter(const GeneticCode *gc, const char *name, char *err, size_t n)
{
    syn_arena_init();
    if (A.foreign) {
        snprintf(err, n, "the ladder in %s is another simulation's", A.path);
        return NULL;
    }
    /* the arena keeps the code as it reads back from the bytes it saves */
    size_t bn = 0;
    uint8_t *bytes = code_bytes(gc, &bn);
    GeneticCode *copy = code_parse(bytes, bn);
    ArenaEntry *e = copy ? add_entry() : NULL;
    if (!e) {
        snprintf(err, n, copy ? "the ladder is full of beings in rounds" : "the code can't be kept");
        free(bytes);
        drop(copy);
        return NULL;
    }
    snprintf(e->name, sizeof e->name, "%s", name && *name ? name : "NEWCOMER");
    snprintf(e->line, sizeof e->line, "%s", e->name);
    syn_family_plan(copy, e->plan, sizeof e->plan);
    e->cycle = A.cycles;
    e->settings = syn_arena_fingerprint();
    e->map = A.set.map;
    e->start = e->level = 1;
    e->state = ENTRY_IN;
    e->last = A.cycles;
    hold(&e->gc, copy);
    drop(copy);
    e->bgg = bytes;
    e->bgg_n = bn;
    board_save();
    return e;
}

void syn_arena_free(void)
{
    if (!A.inited)
        return;
    syn_arena_stop();
    if (A.dirty)
        board_save();
    for (int i = 0; i < A.n; i++)
        entry_free(&A.e[i]);
    free(A.e);
    free(A.rank);
    free(A.res);
    for (int i = 0; i < ARENA_LANES; i++) {
        free(A.wk[i].rx.p);
        free(A.lane[i].samp);
    }
    for (int i = 0; i < ARENA_FEED; i++)
        hold(&A.feed[i].gc, NULL);
    memset(&A, 0, sizeof A);
    syn_record_free();
}

static void stem(const char *name, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = name; *p && k + 1 < n; p++)
        out[k++] = (*p == ' ' || *p == '/' || *p == '#') ? (*p == '#' ? 'n' : '-') : *p;
    out[k] = '\0';
}

void syn_arena_standing(const ArenaEntry *e, char *out, size_t n)
{
    const ArenaEntry *by = e->by ? find(e->by) : NULL;
    if (e->state == ENTRY_IN)
        snprintf(out, n, "in the running");
    else if (e->state == ENTRY_ON && by)
        snprintf(out, n, "went on as its offspring %s, round %d", by->name, e->last);
    else if (e->state == ENTRY_ON)
        snprintf(out, n, "went on as its offspring, round %d", e->last);
    else if (by)
        snprintf(out, n, "knocked out by %s, round %d", by->name, e->last);
    else if (e->by)
        snprintf(out, n, "knocked out, round %d", e->last);
    else
        snprintf(out, n, "out in round %d: no one survived", e->last);
}

int syn_arena_print(const char *dir, char *msg, size_t n)
{
    syn_arena_init();
    ensure_rank();
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        snprintf(msg, n, "%s can't be made: %s", dir, strerror(errno));
        return 0;
    }
    char path[1400];
    snprintf(path, sizeof path, "%s/winners.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(msg, n, "%s can't be written", path);
        return 0;
    }
    int w, h, d, orgs;
    syn_arena_world(&w, &h, &d, &orgs);
    time_t now = time(NULL);
    char when[64];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(f, "SYN-BIOGENESIS arena winners: %s, %s\n", SIM_NAME, when);
    fprintf(f, "world %s: %d x %d x %d, %d beings at the start; rounds of %d time units (256 frames = 1 "
               "time unit); %d rounds fought\n",
            syn_arena_map_name(A.set.map), w, h, d, orgs, A.set.units, A.cycles);
    fprintf(f, "the ladder, highest first: a being is on level 1 once it wins a qualifying round (newcomers "
               "only), and goes up a level for every round it wins against beings of its own level; the rest "
               "of a round's fighters are knocked out\n");
    fprintf(f, "wins: rounds its bloodline won · fought: ladder rounds it fought in · best: the most of the "
               "living its bloodline held at a round's end\n\n");
    int written = 0;
    for (int i = 0; i < A.nrank; i++) {
        const ArenaEntry *e = &A.e[A.rank[i]];
        const ArenaEntry *par = e->parent ? find(e->parent) : NULL;
        char st[120];
        syn_arena_standing(e, st, sizeof st);
        fprintf(f, "%3d  level %-3d %-16s bloodline %-14s wins %-4d fought %-4d best %3.0f%%  %s\n"
                   "     won its first round, %d (seed %" PRId64 ", ended at time %d)%s%s\n     %s\n",
                i + 1, e->level, e->name, e->line, e->wins, e->entered, e->best * 100, st, e->cycle, e->seed,
                e->t_end, par ? ", as the offspring of " : "", par ? par->name : "", e->plan);
        if (i < 100) {
            char s[64];
            stem(e->name, s, sizeof s);
            snprintf(path, sizeof path, "%s/%03d-%s.bgg", dir, i + 1, s);
            FILE *g = fopen(path, "wb");
            if (g && fwrite(e->bgg, 1, e->bgg_n, g) == e->bgg_n)
                written++;
            if (g)
                fclose(g);
        }
    }
    bool ok = fclose(f) == 0;
    snprintf(msg, n, "%d winners printed to %s%s", written, dir, ok ? "" : " (winners.txt incomplete)");
    return written;
}

/* ---- without a window ---- */

static volatile sig_atomic_t g_stop;

static void on_signal(int s)
{
    (void)s;
    g_stop = 1;
}

/* the ladder for these settings, highest first */
static void list_board(int top)
{
    int nb = syn_arena_count();
    printf("ladder (%d for these settings%s):\n", nb, syn_arena_others() ? ", more under other settings" : "");
    for (int i = 0; i < nb && (top <= 0 || i < top); i++) {
        const ArenaEntry *e = syn_arena_at(i);
        char st[120];
        syn_arena_standing(e, st, sizeof st);
        printf("%3d  level %-3d %-16s bloodline %-14s wins %-4d fought %-4d %s\n", i + 1, e->level, e->name,
               e->line, e->wins, e->entered, st);
    }
}

/* the record, a round a line, then what it adds up to */
static void list_record(void)
{
    syn_record_init();
    int n = syn_record_count();
    int by_outcome[OUT_KINDS] = {0}, lone = 0, same = 0, known = 0, src[3] = {0};
    int64_t lone_sum = 0;
    printf("record (%d round%s):\n", n, n == 1 ? "" : "s");
    for (int i = 0; i < n; i++) {
        const RecordRound *r = syn_record_at(i);
        char where[24], ch[24] = "";
        if (r->level)
            snprintf(where, sizeof where, "level %d", r->level);
        else
            snprintf(where, sizeof where, "qualifying");
        int d = syn_record_changes(r);
        if (d >= 0) {
            snprintf(ch, sizeof ch, "%d change%s", d, d == 1 ? "" : "s");
            known++;
            same += d == 0;
        }
        char lone_s[16] = "-";
        if (r->t_lone >= 0) {
            snprintf(lone_s, sizeof lone_s, "%d", r->t_lone);
            lone++;
            lone_sum += r->t_lone;
        }
        if (r->outcome >= 0 && r->outcome < OUT_KINDS)
            by_outcome[r->outcome]++;
        if (r->source >= 0 && r->source < 3)
            src[r->source]++;
        printf("round %5d  %-10s  %-20s  lone at %-5s t %-5d  %-16s -> %-16s %-11s %3.0f%% of %d%s\n", r->cycle,
               where, syn_outcome_name(r->outcome), lone_s, r->t_end, r->gname[0] ? r->gname : "?",
               r->final ? r->name : "none", ch, r->share * 100, r->pop,
               r->source == REC_REPLAYED ? "  (replayed)" : r->source == REC_LADDER ? "  (from the ladder)" : "");
    }
    printf("%d recorded as fought, %d replayed from their seeds, %d from the ladder alone\n", src[REC_LIVE],
           src[REC_REPLAYED], src[REC_LADDER]);
    for (int o = 0; o < OUT_KINDS; o++)
        if (by_outcome[o])
            printf("  %-20s %d\n", syn_outcome_name(o), by_outcome[o]);
    if (lone)
        printf("one bloodline left in %d round%s, at time %.1f on average\n", lone, lone == 1 ? "" : "s",
               (double)lone_sum / lone);
    if (known)
        printf("the final being the genesis being unchanged in %d of %d\n", same, known);
}

static int parse_map(const char *s)
{
    for (int m = 0; m < MAP_COUNT; m++)
        if (!strcasecmp(s, syn_arena_map_name(m)))
            return m;
    return !strcasecmp(s, "params") ? MAP_PARAMS : -1;
}

int syn_arena_main(int argc, char **argv)
{
    A.cli = true;
    const char *out = NULL, *board = NULL;
    bool no_board = false, list = false, record = false;
    const char *enter[16];
    int nenter = 0;
    ArenaSettings o = {0};
    bool given[8] = {0};
    int64_t seed = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NUM(flag, field, k)                                                                       \
    if (!strcmp(a, flag) && v) {                                                                  \
        o.field = atoi(v);                                                                        \
        given[k] = true;                                                                          \
        i++;                                                                                      \
        continue;                                                                                 \
    }
        if (!strcmp(a, "--arena"))
            continue;
        NUM("--cycles", target, 0)
        NUM("--rounds", target, 0)
        NUM("--units", units, 1)
        NUM("--workers", workers, 2)
        NUM("--wild", wild_pct, 3)
        NUM("--seats", seats, 4)
#undef NUM
        if (!strcmp(a, "--map") && v && parse_map(v) >= 0) {
            o.map = parse_map(v);
            given[5] = true;
            i++;
        } else if (!strcmp(a, "--seed") && v) {
            seed = strtoll(v, NULL, 10);
            given[6] = true;
            i++;
        } else if (!strcmp(a, "--no-knockout")) {
            given[7] = true;
        } else if (!strcmp(a, "--board") && v) {
            board = v;
            i++;
        } else if (!strcmp(a, "--no-board")) {
            no_board = true;
        } else if (!strcmp(a, "--out") && v) {
            out = v;
            i++;
        } else if (!strcmp(a, "--quiet")) {
            A.quiet = true;
        } else if (!strcmp(a, "--list")) {
            list = true;
        } else if (!strcmp(a, "--record")) {
            record = true;
        } else if (!strcmp(a, "--enter") && v && nenter < 16) {
            enter[nenter++] = v;
            i++;
        } else {
            printf("usage: syn-biogenesis-3d --arena [options]\n"
                   "  rounds of the arena's ladder without a window (" SIM_NAME "), each printed as it\n"
                   "  ends: newcomers qualify for level 1, then beings of a level fight each other, the\n"
                   "  winner going up a level and the rest knocked out\n"
                   "  --rounds N       rounds to run (default 10; 0 until interrupted)\n"
                   "  --units U        time units a round, 256 frames each (default: five\n"
                   "                   lifetimes, 5 x max_age)\n"
                   "  --workers W      rounds at once (default: one a core, %d)\n"
                   "  --map M          small (600 x 600 x 300, the default box), medium and large\n"
                   "                   (4 and 9 times its surface), pond (1000 x 1000 x 64), cube\n"
                   "                   (400 x 400 x 400), or params: the world as the parameters have\n"
                   "                   it (default small)\n"
                   "  --seats K        fighters in a ladder round, 2 to 8 (default 2); 0: qualifying\n"
                   "                   rounds only\n"
                   "  --wild PCT       of a ladder round's first organisms, newcomers (default 0)\n"
                   "  --seed S         round c runs with seed S + c\n"
                   "  --no-knockout    no sudden death: every round runs to the end, even with one\n"
                   "                   bloodline left\n"
                   "  --board FILE     the ladder (default: the window's, " BOARD_FILE "\n"
                   "                   in the configuration folder); --no-board: none\n"
                   "  --out DIR        print the ladder there at the end: winners.txt and each\n"
                   "                   being's .bgg\n"
                   "  --list           no rounds: the ladder for these settings, all of it\n"
                   "                   (with --out, printed there too)\n"
                   "  --record         no rounds: every round in the record, each with its\n"
                   "                   bloodline's genesis and final beings\n"
                   "  --enter FILE     put the being in FILE (.bgg) on the ladder at level 1, named\n"
                   "                   after the file, before any rounds (up to 16 times)\n"
                   "  --quiet          only the ladder at the end\n",
                   syn_arena_cores());
            return strcmp(a, "--help") && strcmp(a, "-h") ? 2 : 0;
        }
    }
    params_set_defaults(&P);
    params_load(&P);
    if (no_board)
        A.path[0] = '\0';
    else if (board)
        snprintf(A.path, sizeof A.path, "%s", board);
    else
        snprintf(A.path, sizeof A.path, "%s/%s", params_config_dir(), BOARD_FILE);
    syn_arena_init();
    if (A.foreign) {
        syn_arena_free();
        return 2;
    }
    ArenaSettings *s = &A.set;
    s->target = given[0] ? o.target : 10;
    s->units = given[1] ? o.units : 5 * (P.max_age > 0 ? P.max_age : 1);
    s->workers = given[2] ? o.workers : syn_arena_cores();
    s->wild_pct = given[3] ? o.wild_pct : 0;
    s->seats = given[4] ? o.seats : 2;
    s->map = given[5] ? o.map : MAP_SMALL;
    if (given[6])
        s->seed = seed;
    s->knockout = !given[7];
    clamp_settings(s);
    /* a ladder begun here keeps these settings */
    if (!A.loaded)
        A.saved = *s;

    for (int k = 0; k < nenter; k++) {
        char err[200], name[40];
        GeneticCode *gc = bgg_load(enter[k], err, (int)sizeof err);
        const char *base = strrchr(enter[k], '/');
        snprintf(name, sizeof name, "%s", base ? base + 1 : enter[k]);
        char *dot = strrchr(name, '.');
        if (dot && !strcasecmp(dot, ".bgg"))
            *dot = '\0';
        const ArenaEntry *e = gc ? syn_arena_enter(gc, name, err, sizeof err) : NULL;
        if (e)
            printf("entered %s at level 1\n", e->name);
        else
            fprintf(stderr, "%s: %s\n", enter[k], err);
        drop(gc);
        if (!e) {
            syn_arena_free();
            return 1;
        }
    }

    if (record) {
        list_record();
        syn_arena_free();
        return 0;
    }
    if (list) {
        list_board(0);
        if (out) {
            char msg[1400];
            syn_arena_print(out, msg, sizeof msg);
            printf("%s\n", msg);
        }
        syn_arena_free();
        return 0;
    }

    int w, h, d, orgs;
    syn_arena_world(&w, &h, &d, &orgs);
    printf("arena: %s, map %s (%d x %d x %d, %d first organisms), %d time units a round, %d workers, "
           "%d fighters a ladder round with %d%% newcomers; %d on the ladder for these settings\n",
           SIM_NAME, syn_arena_map_name(s->map), w, h, d, orgs, s->units, s->workers, s->seats, s->wild_pct,
           syn_arena_count());
    fflush(stdout);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    char err[256];
    if (!syn_arena_start(err, sizeof err)) {
        fprintf(stderr, "arena: %s\n", err);
        return 1;
    }
    while (syn_arena_running() && !g_stop) {
        int from[ARENA_LANES], np = 0;
        for (int i = 0; i < A.nlanes; i++)
            if (A.wk[i].up)
                from[np++] = A.wk[i].proc.from;
        arena_os_wait(from, np, 500);
        syn_arena_poll();
    }
    int cycles;
    double secs, frames;
    syn_arena_run_stats(&cycles, &secs, &frames);
    if (g_stop)
        printf("stopped\n");
    syn_arena_stop();
    printf("%d rounds in %.1f s (%.0f frames a second, all workers)\n", cycles, secs,
           secs > 0 ? frames / secs : 0);
    list_board(10);
    if (out) {
        char msg[1400];
        syn_arena_print(out, msg, sizeof msg);
        printf("%s\n", msg);
    }
    syn_arena_free();
    return 0;
}
