/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/winner.c, the SYN edition of its C
 * port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "syn/winner.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compat.h"
#include "io/bgg.h"
#include "io/worldfile.h"
#include "sim/jrandom.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/world.h"
#include "syn/arena_os.h"
#include "syn/crack.h"
#include "syn/history.h"
#include "syn/library.h"

#define SIM_NAME "SYN-BIOGENESIS-3D"

/* what a run came to */
enum { W_FAILED = -2, W_NOPLACE = 0, W_DEAD = 1, W_ALIVE = 2, W_OWNS = 3 };

/* ================= shared ================= */

static bool load_world(World *w, const char *path, char *err, size_t n)
{
    world_init(w);
    return worldfile_load(w, path, err, (int)n);
}

/* as the window pastes a code (app_paste) */
static Organism *paste_at(World *w, GeneticCode *gc, V3 p)
{
    Organism *o = org_new(w);
    if (!org_place_at(o, gc, p)) {
        org_free(o);
        return NULL;
    }
    world_add_organism(w, o, NULL);
    return o;
}

/* the pasted organism's bloodline among the living: the codes marked 1 */
static void tally(const World *w, int *mine, int *alive)
{
    *mine = *alive = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (o->removed || !o->alive)
            continue;
        (*alive)++;
        *mine += o->gc && o->gc->lineage == 1;
    }
}

/* A code as its .bgg text. */
static char *code_text(const GeneticCode *gc, size_t *n)
{
    return (char *)bgg_bytes(gc, n);
}

static bool make_dir(const char *path)
{
    char p[1100];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = '\0';
            mkdir(p, 0755);
            *s = '/';
        }
    return mkdir(p, 0755) == 0 || errno == EEXIST;
}

static void winner_dir(char *out, size_t n)
{
    snprintf(out, n, "%s/winner", params_config_dir());
}

static bool write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += k;
        n -= (size_t)k;
    }
    return true;
}

static bool sayf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static bool sayf(int fd, const char *fmt, ...)
{
    char s[1400];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    return k > 0 && (size_t)k < sizeof s && write_all(fd, s, (size_t)k);
}

/* numbers as the window writes them: 1,130 */
static const char *num(long long v, char *buf, size_t n)
{
    char d[32];
    int k = snprintf(d, sizeof d, "%lld", v < 0 ? -v : v), o = 0;
    if (v < 0 && o + 1 < (int)n)
        buf[o++] = '-';
    for (int i = 0; i < k && o + 2 < (int)n; i++) {
        buf[o++] = d[i];
        if ((k - 1 - i) % 3 == 0 && i < k - 1)
            buf[o++] = ',';
    }
    buf[o] = '\0';
    return buf;
}

/* ================= the worker ================= */

static bool read_line(int fd, char *s, size_t n)
{
    size_t k = 0;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        if (c == '\n')
            break;
        if (k + 1 < n && c != '\r')
            s[k++] = c;
    }
    s[k] = '\0';
    return true;
}

/* One candidate at one place: the world as saved (its parameters and dice
 * with it), the code pasted, and the world run until time unit until or
 * the bloodline dies out. Once it is all that lives, the run goes on to
 * time unit hold: the last of a dying world is no winner, so it owns the
 * world only if it is still alive then. */
static void run_job(int fd, const char *world_path, int id, V3 p, int64_t until, int64_t hold, const char *code_path)
{
    char err[256];
    World w;
    if (!load_world(&w, world_path, err, sizeof err)) {
        fprintf(stderr, "winner worker: %s: %s\n", world_path, err);
        world_free(&w);
        sayf(fd, "R %d %d 0 0 0 0 0 0 0\n", id, W_FAILED);
        return;
    }
    GeneticCode *gc = bgg_load(code_path, err, sizeof err);
    if (!gc) {
        fprintf(stderr, "winner worker: %s: %s\n", code_path, err);
        world_free(&w);
        sayf(fd, "R %d %d 0 0 0 0 0 0 0\n", id, W_FAILED);
        return;
    }
    for (int i = 0; i < w.norgs; i++) {
        Organism *o = w.orgs[i];
        if (o->gc)
            o->gc->lineage = 0;
        if (o->infected_gc)
            o->infected_gc->lineage = 0;
    }
    gc->lineage = 1;
    Organism *o = paste_at(&w, gc, p);
    gc_unref(gc);
    int status = W_NOPLACE, mine = 0, alive = 0, own_n = 0;
    int64_t own_t = -1;
    uint64_t own_h = 0;
    long frames = 0;
    if (o) {
        status = W_ALIVE;
        int64_t seen = world_time_units(&w);
        tally(&w, &mine, &alive);
        while (world_time_units(&w) < (own_t >= 0 ? hold : until)) {
            world_time(&w);
            frames++;
            if (world_time_units(&w) == seen)
                continue;
            seen = world_time_units(&w);
            tally(&w, &mine, &alive);
            if (mine == 0) {
                status = W_DEAD;
                break;
            }
            if (own_t < 0 && mine == alive) {
                own_t = seen;
                own_n = alive;
                own_h = world_hash(&w);
            }
        }
        if (status == W_ALIVE && own_t >= 0)
            status = W_OWNS;
    }
    /* owned: when it began, the living then and the world hashed then;
     * otherwise as the run ended. Both with the run's end. */
    int64_t end_t = world_time_units(&w);
    uint64_t h = status == W_OWNS ? own_h : world_hash(&w);
    world_free(&w);
    if (status == W_OWNS)
        sayf(fd, "R %d %d %lld %d %d %016" PRIx64 " %ld %lld %d\n", id, status, (long long)own_t, own_n, own_n, h,
             frames, (long long)end_t, mine);
    else
        sayf(fd, "R %d %d %lld %d %d %016" PRIx64 " %ld %lld %d\n", id, status, (long long)end_t, mine, alive, h,
             frames, (long long)end_t, mine);
}

int syn_winner_worker(int argc, char **argv)
{
    /* WORLD */
    if (argc < 2)
        return 2;
    int fd = arena_os_worker_begin(true);
    if (fd < 0)
        return 0;
    char line[1400], path[1100];
    while (read_line(0, line, sizeof line)) {
        int id;
        double x, y, z;
        long long until, hold;
        if (sscanf(line, "J %d %lf %lf %lf %lld %lld %1099[^\n]", &id, &x, &y, &z, &until, &hold, path) != 7)
            break;
        run_job(fd, argv[1], id, v3(x, y, z), until, hold, path);
    }
    return 0;
}

/* ================= the search ================= */

#define MAX_WORKERS 32
#define MAX_SPOTS 8

typedef struct {
    char path[1100];
    char label[200];
    char *text; /* its .bgg, to tell codes apart */
    size_t ntext;
    int spot; /* bred: its parent's best place; -1: every place */
} Cand;

typedef struct {
    int cand, spot;
    int64_t until;
    int status; /* W_*, or -1 not run */
    int64_t t; /* owned: when it began; otherwise the run's end */
    int mine, alive;
    uint64_t hash; /* the world at t */
    int64_t end_t; /* the run's end, and its bloodline then */
    int end_n;
} Eval;

typedef struct {
    ArenaProc proc;
    bool up;
    int job; /* the eval it runs, -1 none */
    char part[512];
    size_t npart;
} Worker;

static struct {
    Cand *c;
    int nc, capc;
    Eval *e;
    int ne, cape;
    V3 spot[MAX_SPOTS];
    char spot_label[MAX_SPOTS][48];
    int nspot;
    char dir[1100], world[1100];
    int64_t t0;
    int units;
    double started, seconds;
    bool progress, out_of_time;
    Worker wk[MAX_WORKERS];
    int nwk;
    int best; /* the best eval so far, -1 */
    long runs;
} D;

static double elapsed(void)
{
    return arena_os_now() - D.started;
}

static bool add_cand(const GeneticCode *gc, const char *label, int spot)
{
    size_t n = 0;
    char *t = code_text(gc, &n);
    if (!t)
        return false;
    for (int i = 0; i < D.nc; i++)
        if (D.c[i].ntext == n && !memcmp(D.c[i].text, t, n)) {
            free(t);
            return false;
        }
    if (D.nc == D.capc) {
        D.capc = D.capc ? D.capc * 2 : 64;
        D.c = realloc(D.c, (size_t)D.capc * sizeof *D.c);
        if (!D.c)
            abort();
    }
    Cand *c = &D.c[D.nc];
    snprintf(c->path, sizeof c->path, "%.1000s/candidate-%04d.bgg", D.dir, D.nc + 1);
    FILE *f = fopen(c->path, "wb");
    if (!f || fwrite(t, 1, n, f) != n) {
        if (f)
            fclose(f);
        free(t);
        return false;
    }
    fclose(f);
    snprintf(c->label, sizeof c->label, "%s", label);
    c->text = t;
    c->ntext = n;
    c->spot = spot;
    D.nc++;
    return true;
}

/* ---- what the world holds ---- */

typedef struct {
    GeneticCode *gc;
    char *text;
    size_t ntext;
    int n;
} Kind;

static int cmp_kind(const void *a, const void *b)
{
    const Kind *x = a, *y = b;
    return y->n - x->n;
}

/* the living codes, alike ones together, the commonest first */
static int census(const World *w, Kind **out, int *alive)
{
    Kind *k = NULL;
    int nk = 0, cap = 0;
    *alive = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (o->removed || !o->alive || !o->gc)
            continue;
        (*alive)++;
        int j;
        for (j = 0; j < nk && k[j].gc != o->gc; j++)
            ;
        if (j < nk) {
            k[j].n++;
            continue;
        }
        if (nk == cap) {
            cap = cap ? cap * 2 : 256;
            k = realloc(k, (size_t)cap * sizeof *k);
            if (!k)
                abort();
        }
        k[nk++] = (Kind){o->gc, NULL, 0, 1};
    }
    /* codes written alike are one kind */
    int m = 0;
    for (int i = 0; i < nk; i++) {
        k[i].text = code_text(k[i].gc, &k[i].ntext);
        if (!k[i].text)
            continue;
        int j;
        for (j = 0; j < m && !(k[j].ntext == k[i].ntext && !memcmp(k[j].text, k[i].text, k[i].ntext)); j++)
            ;
        if (j < m) {
            k[j].n += k[i].n;
            free(k[i].text);
        } else {
            k[m++] = k[i];
        }
    }
    qsort(k, (size_t)m, sizeof *k, cmp_kind);
    *out = k;
    return m;
}

/* ---- places ---- */

static void add_spot(V3 p, const char *label)
{
    if (D.nspot >= MAX_SPOTS)
        return;
    D.spot[D.nspot] = p;
    snprintf(D.spot_label[D.nspot], sizeof D.spot_label[0], "%s", label);
    D.nspot++;
}

/* The most open places: on a grid through the water, how far each point is
 * from every organism's bounds (and the box's walls), the clearest first,
 * each at least a quarter of the box from those already taken. */
static void open_spots(const World *w, int want)
{
    enum { G = 20 };
    static double clear[G * G * G];
    double W = w->width, H = w->height, Dp = w->depth;
    for (int i = 0; i < G * G * G; i++) {
        V3 p = v3((i % G + 0.5) * W / G, (i / G % G + 0.5) * H / G, (i / (G * G) + 0.5) * Dp / G);
        double c = fmin(fmin(fmin(p.x, W - p.x), fmin(p.y, H - p.y)), fmin(p.z, Dp - p.z));
        for (int k = 0; k < w->norgs && c > 0; k++) {
            const Organism *o = w->orgs[k];
            if (o->removed)
                continue;
            V3 mid = v3_scale(v3_add(o->lo, o->hi), 0.5);
            double d = v3_len(v3_sub(p, mid)) - v3_len(v3_sub(o->hi, o->lo)) / 2;
            if (d < c)
                c = d;
        }
        clear[i] = c;
    }
    double sep = fmin(fmin(W, H), Dp) / 4.0;
    for (int got = 0; got < want && D.nspot < MAX_SPOTS; got++) {
        int bi = -1;
        for (int i = 0; i < G * G * G; i++) {
            V3 p = v3((i % G + 0.5) * W / G, (i / G % G + 0.5) * H / G, (i / (G * G) + 0.5) * Dp / G);
            bool near = false;
            for (int s = 0; s < D.nspot && !near; s++)
                near = v3_len(v3_sub(p, D.spot[s])) < sep;
            if (!near && (bi < 0 || clear[i] > clear[bi]))
                bi = i;
        }
        if (bi < 0 || clear[bi] <= 0)
            break;
        char label[48];
        snprintf(label, sizeof label, "open place %d", got + 1);
        add_spot(v3((bi % G + 0.5) * W / G, (bi / G % G + 0.5) * H / G, (bi / (G * G) + 0.5) * Dp / G), label);
    }
}

/* ---- candidates ---- */

/* The simulation's own mutation, drawn until the code is a new one. */
static GeneticCode *mutant(GeneticCode *parent)
{
    size_t pn = 0;
    char *pt = code_text(parent, &pn);
    for (int tries = 0; pt && tries < 40; tries++) {
        GeneticCode *m = gc_new_mutated(parent);
        size_t n = 0;
        char *t = code_text(m, &n);
        bool same = !t || (n == pn && !memcmp(t, pt, n));
        free(t);
        if (!same) {
            free(pt);
            return m;
        }
        gc_unref(m);
    }
    free(pt);
    return NULL;
}

static void library_cands(int cap)
{
    static char folders[256][LIB_PATH];
    int nf = syn_library_folders(folders, 256), got = 0;
    for (int f = 0; f < nf && got < cap; f++) {
        if (!strcmp(folders[f], LIB_BIN) || !strncmp(folders[f], LIB_BIN "/", strlen(LIB_BIN) + 1))
            continue;
        static LibEntry e[256];
        int ne = syn_library_list(folders[f], e, 256);
        for (int i = 0; i < ne && got < cap; i++) {
            char err[200], label[200];
            GeneticCode *gc = syn_library_read(folders[f], e[i].name, err, sizeof err);
            if (!gc)
                continue;
            snprintf(label, sizeof label, "from the library: %.80s%s%.60s", folders[f], folders[f][0] ? "/" : "",
                     e[i].name);
            got += add_cand(gc, label, -1);
            gc_unref(gc);
        }
    }
}

typedef struct {
    GeneticCode *gc;
    double fit;
} Fit;

static int cmp_fit(const void *a, const void *b)
{
    const Fit *x = a, *y = b;
    return (x->fit < y->fit) - (x->fit > y->fit);
}

/* random codes: the fittest of many by syn/crack.h's fitness */
static void random_cands(int keep)
{
    enum { DRAWN = 600 };
    static Fit f[DRAWN];
    for (int i = 0; i < DRAWN; i++) {
        f[i].gc = gc_new_random();
        f[i].fit = crack_fitness(f[i].gc);
    }
    qsort(f, DRAWN, sizeof *f, cmp_fit);
    int got = 0;
    for (int i = 0; i < DRAWN; i++) {
        if (got < keep) {
            char label[200];
            snprintf(label, sizeof label, "random, fitness %.3g (the best of %d drawn)", f[i].fit, DRAWN);
            got += add_cand(f[i].gc, label, -1);
        }
        gc_unref(f[i].gc);
    }
}

/* ---- the workers ---- */

static bool start_workers(int n)
{
    const char *args[] = {"--winner-worker", D.world};
    for (int i = 0; i < n && i < MAX_WORKERS; i++) {
        char err[300];
        Worker *k = &D.wk[D.nwk];
        memset(k, 0, sizeof *k);
        if (!arena_os_spawn(&k->proc, args, 2, err, sizeof err)) {
            fprintf(stderr, "winner: a worker didn't start: %s\n", err);
            break;
        }
        k->up = true;
        k->job = -1;
        D.nwk++;
    }
    return D.nwk > 0;
}

static void stop_workers(void)
{
    for (int i = 0; i < D.nwk; i++)
        if (D.wk[i].up) {
            arena_os_end(&D.wk[i].proc);
            D.wk[i].up = false;
        }
}

static double share(const Eval *e)
{
    return e->alive > 0 ? (double)e->mine / e->alive : 0;
}

static int rank_class(const Eval *e)
{
    return e->status == W_OWNS ? 3 : e->status == W_ALIVE ? 2 : e->status == W_DEAD ? 1 : 0;
}

/* all that lives soonest (and the most of it at the end); then alive after
 * the longest run, with the most of the living; then dead latest */
static bool better(const Eval *a, const Eval *b)
{
    int ca = rank_class(a), cb = rank_class(b);
    if (ca != cb)
        return ca > cb;
    if (ca == 3)
        return a->t != b->t ? a->t < b->t : a->end_n > b->end_n;
    if (ca == 2) {
        if (a->until != b->until)
            return a->until > b->until;
        if (share(a) != share(b))
            return share(a) > share(b);
        return a->mine > b->mine;
    }
    return ca == 1 && a->t > b->t;
}

static void describe(const Eval *e, char *out, size_t n)
{
    char a[32], b[32], t[32];
    const Cand *c = &D.c[e->cand];
    V3 p = D.spot[e->spot];
    switch (e->status) {
    case W_OWNS: {
        char et[32];
        snprintf(out, n, "#%d (%s) at %s (%.0f, %.0f, %.0f deep): all that lives from time %s (%s organisms), %s at time %s",
                 e->cand + 1, c->label, D.spot_label[e->spot], p.x, p.y, p.z, num(e->t, t, sizeof t),
                 num(e->alive, a, sizeof a), num(e->end_n, b, sizeof b), num(e->end_t, et, sizeof et));
        break;
    }
    case W_ALIVE:
        snprintf(out, n, "#%d (%s) at %s (%.0f, %.0f, %.0f deep): %s of the %s living at time %s", e->cand + 1, c->label,
                 D.spot_label[e->spot], p.x, p.y, p.z, num(e->mine, a, sizeof a), num(e->alive, b, sizeof b),
                 num(e->t, t, sizeof t));
        break;
    case W_DEAD:
        snprintf(out, n, "#%d (%s) at %s: died out by time %s", e->cand + 1, c->label, D.spot_label[e->spot],
                 num(e->t, t, sizeof t));
        break;
    default:
        snprintf(out, n, "#%d (%s) at %s: no room there", e->cand + 1, c->label, D.spot_label[e->spot]);
    }
}

static int add_eval(int cand, int spot, int64_t until)
{
    if (D.ne == D.cape) {
        D.cape = D.cape ? D.cape * 2 : 256;
        D.e = realloc(D.e, (size_t)D.cape * sizeof *D.e);
        if (!D.e)
            abort();
    }
    D.e[D.ne] = (Eval){cand, spot, until, -1, 0, 0, 0, 0, 0, 0};
    return D.ne++;
}

static void got_line(Worker *k, const char *s)
{
    int id, status, mine, alive;
    long long t;
    unsigned long long h;
    long frames;
    long long end_t;
    int end_n;
    if (sscanf(s, "R %d %d %lld %d %d %llx %ld %lld %d", &id, &status, &t, &mine, &alive, &h, &frames, &end_t,
               &end_n) != 9 ||
        id < 0 || id >= D.ne)
        return;
    Eval *e = &D.e[id];
    e->status = status;
    e->t = t;
    e->mine = mine;
    e->alive = alive;
    e->hash = h;
    e->end_t = end_t;
    e->end_n = end_n;
    D.runs++;
    if (status >= W_DEAD && (D.best < 0 || better(e, &D.e[D.best])))
        D.best = id;
    k->job = -1;
}

/* the evals idx run on the workers; false if time ran out first */
static bool run_batch(const int *idx, int n, int rung, int nrungs, int gen)
{
    int next = 0, done = 0;
    double last = 0;
    while (done < n) {
        if (elapsed() > D.seconds) {
            D.out_of_time = true;
            stop_workers();
            return false;
        }
        int live = 0;
        for (int i = 0; i < D.nwk; i++) {
            Worker *k = &D.wk[i];
            if (!k->up)
                continue;
            live++;
            if (k->job < 0 && next < n) {
                const Eval *e = &D.e[idx[next]];
                V3 p = D.spot[e->spot];
                char job[1300];
                int len = snprintf(job, sizeof job, "J %d %.17g %.17g %.17g %lld %lld %s\n", idx[next], p.x, p.y, p.z,
                                   (long long)e->until, (long long)(D.t0 + D.units), D.c[e->cand].path);
                if (len > 0 && (size_t)len < sizeof job && write_all(k->proc.to, job, (size_t)len)) {
                    k->job = idx[next];
                    next++;
                } else {
                    arena_os_end(&k->proc);
                    k->up = false;
                }
            }
        }
        if (!live) {
            printf("winner: every worker has stopped\n");
            return false;
        }
        int fds[MAX_WORKERS], nf = 0;
        for (int i = 0; i < D.nwk; i++)
            if (D.wk[i].up)
                fds[nf++] = D.wk[i].proc.from;
        arena_os_wait(fds, nf, 250);
        for (int i = 0; i < D.nwk; i++) {
            Worker *k = &D.wk[i];
            if (!k->up)
                continue;
            char buf[2048];
            long m;
            while ((m = arena_os_read(&k->proc, buf, sizeof buf)) > 0)
                for (long j = 0; j < m; j++) {
                    if (buf[j] == '\n' || k->npart + 1 >= sizeof k->part) {
                        k->part[k->npart] = '\0';
                        int had = k->job;
                        got_line(k, k->part);
                        if (had >= 0 && k->job < 0)
                            done++;
                        k->npart = 0;
                    } else if (buf[j] != '\r') {
                        k->part[k->npart++] = buf[j];
                    }
                }
            if (m < 0) {
                /* it has gone: its run with it */
                if (k->job >= 0) {
                    D.e[k->job].status = W_FAILED;
                    k->job = -1;
                    done++;
                }
                arena_os_end(&k->proc);
                k->up = false;
            }
        }
        if (D.progress && elapsed() - last >= 0.5) {
            last = elapsed();
            printf("@progress %d %d %d %d %d %.0f %.4f %d\n", gen, rung + 1, nrungs, done, n, elapsed(),
                   D.best >= 0 ? share(&D.e[D.best]) : 0.0, D.best >= 0 && D.e[D.best].status == W_OWNS);
        }
    }
    return true;
}

static int cmp_eval_idx(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (better(&D.e[x], &D.e[y]))
        return -1;
    if (better(&D.e[y], &D.e[x]))
        return 1;
    return (x > y) - (x < y);
}

/* the horizons: a few time units, then four times as many, ... up to all */
static int horizons(int *h)
{
    static const int k[] = {4, 16, 64};
    int n = 0;
    for (int i = 0; i < 3; i++)
        if (k[i] < D.units)
            h[n++] = k[i];
    h[n++] = D.units;
    return n;
}

/* (cand, spot) pairs through the horizons, the best third going on each
 * time; true when one is all that lives */
static bool run_rungs(int *cand, int *spot, int n, int gen)
{
    int h[4], nh = horizons(h);
    int *idx = malloc((size_t)(n > 0 ? n : 1) * sizeof *idx);
    if (!idx)
        abort();
    bool owned = false;
    for (int r = 0; r < nh && n > 0; r++) {
        for (int i = 0; i < n; i++)
            idx[i] = add_eval(cand[i], spot[i], D.t0 + h[r]);
        bool all = run_batch(idx, n, r, nh, gen);
        qsort(idx, (size_t)n, sizeof *idx, cmp_eval_idx);
        int dead = 0, live = 0, own = 0;
        for (int i = 0; i < n; i++) {
            int s = D.e[idx[i]].status;
            dead += s == W_DEAD || s == W_NOPLACE;
            live += s == W_ALIVE;
            own += s == W_OWNS;
        }
        char what[600] = "none lived";
        if (n > 0 && D.e[idx[0]].status >= W_ALIVE)
            describe(&D.e[idx[0]], what, sizeof what);
        printf("%s%s, %d time unit%s, %d run%s: %d died out or found no room%s; best %s\n", gen ? "bred, " : "",
               r == 0 ? "first runs" : r == nh - 1 ? "last runs" : "longer runs", h[r], h[r] == 1 ? "" : "s", n,
               n == 1 ? "" : "s", dead, all ? "" : " (time ran out)", what);
        fflush(stdout);
        if (!all)
            break;
        if (own) {
            owned = true;
            break;
        }
        /* the best third, at least three, of those still alive */
        int keep = (live + 2) / 3;
        if (keep < 3)
            keep = live < 3 ? live : 3;
        for (int i = 0; i < keep; i++) {
            cand[i] = D.e[idx[i]].cand;
            spot[i] = D.e[idx[i]].spot;
        }
        n = keep;
    }
    free(idx);
    return owned;
}

static void usage(void)
{
    printf("usage: syn-biogenesis-3d --winner WORLD.b3w [options]\n"
           "  a being worked out for WORLD as it is (its parameters and dice are in the save),\n"
           "  whose bloodline will be all that lives in it: candidates pasted as the window\n"
           "  pastes a code and the world run on from there on every core, exactly as it\n"
           "  will go\n"
           "  --at X Y Z         the place asked for (default: open places only)\n"
           "  --spots K          open places tried besides it (default 2)\n"
           "  --units U          time units a run goes on for at most (default 256)\n"
           "  --seconds S        how long the search may take (default 180)\n"
           "  --workers W        runs at once (default: one a core)\n"
           "  --out DIR          the candidates, winner.bgg and winners.txt there (default:\n"
           "                     winner in the configuration folder)\n"
           "  --progress         lines for the window as it goes\n");
}

int syn_winner_main(int argc, char **argv)
{
    const char *world = NULL, *out = NULL;
    bool have_at = false;
    V3 at = v3(0, 0, 0);
    int spots = 2, workers = 0;
    D.units = 256;
    D.seconds = 180;
    D.best = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--winner")) {
            continue;
        } else if (!strcmp(a, "--at") && v && i + 3 < argc) {
            at = v3(atof(v), atof(argv[i + 2]), atof(argv[i + 3]));
            have_at = true;
            i += 3;
        } else if (!strcmp(a, "--spots") && v) {
            spots = atoi(v);
            i++;
        } else if (!strcmp(a, "--units") && v) {
            D.units = atoi(v);
            i++;
        } else if (!strcmp(a, "--seconds") && v) {
            D.seconds = atof(v);
            i++;
        } else if (!strcmp(a, "--workers") && v) {
            workers = atoi(v);
            i++;
        } else if (!strcmp(a, "--out") && v) {
            out = v;
            i++;
        } else if (!strcmp(a, "--progress")) {
            D.progress = true;
        } else if (a[0] != '-' && !world) {
            world = a;
        } else {
            usage();
            return !strcmp(a, "--help") || !strcmp(a, "-h") ? 0 : 2;
        }
    }
    if (!world) {
        usage();
        return 2;
    }
    if (D.progress) {
        /* the window's: gone with it, every line as it is printed */
        arena_os_child_begin();
    }
    if (D.units < 1)
        D.units = 1;
    if (spots < 0)
        spots = 0;
    if (workers < 1)
        workers = arena_os_cores();
    if (workers > MAX_WORKERS)
        workers = MAX_WORKERS;
    D.started = arena_os_now();
    if (out)
        snprintf(D.dir, sizeof D.dir, "%s", out);
    else
        winner_dir(D.dir, sizeof D.dir);
    if (!make_dir(D.dir)) {
        printf("winner: %s: %s\n", D.dir, strerror(errno));
        return 1;
    }
    snprintf(D.world, sizeof D.world, "%s", world);

    char err[256];
    World w;
    if (!load_world(&w, world, err, sizeof err)) {
        printf("winner: %s: %s\n", world, err);
        world_free(&w);
        return 1;
    }
    uint64_t state = g_rng.seed;
    D.t0 = world_time_units(&w);
    uint64_t h0 = world_hash(&w);

    Kind *kinds = NULL;
    int alive = 0, nkinds = census(&w, &kinds, &alive);
    char a1[32], a2[32], a3[32];
    printf(SIM_NAME ", the world at time %s: %s living, %s codes, CO2 %.0f; the generator at %012" PRIx64 "\n",
           num(D.t0, a1, sizeof a1), num(alive, a2, sizeof a2), num(nkinds, a3, sizeof a3), w.co2, state);
    if (D.progress)
        printf("@begin %016" PRIx64 "\n", h0);

    /* the places */
    if (have_at)
        add_spot(at, "the place asked for");
    open_spots(&w, spots);
    printf("places:");
    for (int s = 0; s < D.nspot; s++)
        printf("%s %s (%.0f, %.0f, %.0f deep)", s ? ";" : "", D.spot_label[s], D.spot[s].x, D.spot[s].y,
               D.spot[s].z);
    printf("\n");
    if (!D.nspot) {
        printf("@none the world has no open place\n");
        world_free(&w);
        return 0;
    }

    /* the candidates, from dice of the search's own (the world's are in
     * the file and the workers) */
    jr_set_seed(&g_rng, (int64_t)(state ^ 0x5DEECE66DULL));
    static const char *const nth[] = {"", "2nd ", "3rd ", "4th ", "5th ", "6th ", "7th ", "8th "};
    int n_inc = nkinds < 8 ? nkinds : 8;
    for (int i = 0; i < n_inc; i++) {
        char label[200];
        snprintf(label, sizeof label, "the %scommonest code alive, %s of the %s living", nth[i],
                 num(kinds[i].n, a1, sizeof a1), num(alive, a2, sizeof a2));
        add_cand(kinds[i].gc, label, -1);
    }
    int from_world = D.nc;
    for (int i = 0; i < 3 && i < nkinds; i++)
        for (int m = 0; m < 4; m++) {
            GeneticCode *gc = mutant(kinds[i].gc);
            if (!gc)
                break;
            char label[200];
            snprintf(label, sizeof label, "a mutation of the %scommonest code alive", nth[i]);
            add_cand(gc, label, -1);
            gc_unref(gc);
        }
    int mutants = D.nc - from_world;
    library_cands(24);
    int library = D.nc - from_world - mutants;
    random_cands(8);
    int randoms = D.nc - from_world - mutants - library;
    for (int i = 0; i < nkinds; i++)
        free(kinds[i].text);
    free(kinds);
    printf("candidates: %d of the world's commonest codes, %d mutations of them, %d from the library, %d random: "
           "%d, each tried at %d place%s\n",
           from_world, mutants, library, randoms, D.nc, D.nspot, D.nspot == 1 ? "" : "s");
    fflush(stdout);

    if (!start_workers(workers)) {
        printf("@none no worker started\n");
        world_free(&w);
        return 1;
    }

    /* every candidate at every place */
    int n = D.nc * D.nspot;
    int *cand = malloc((size_t)(n ? n : 1) * sizeof *cand), *spot = malloc((size_t)(n ? n : 1) * sizeof *spot);
    if (!cand || !spot)
        abort();
    for (int c = 0, k = 0; c < D.nc; c++)
        for (int s = 0; s < D.nspot; s++, k++) {
            cand[k] = c;
            spot[k] = s;
        }
    bool owned = run_rungs(cand, spot, n, 0);
    free(cand);
    free(spot);

    /* bred from the best while time is left */
    for (int gen = 1; !owned && !D.out_of_time && gen <= 50; gen++) {
        int parent[4], np = 0;
        int *order = malloc((size_t)(D.ne ? D.ne : 1) * sizeof *order);
        if (!order)
            abort();
        for (int i = 0; i < D.ne; i++)
            order[i] = i;
        qsort(order, (size_t)D.ne, sizeof *order, cmp_eval_idx);
        for (int i = 0; i < D.ne && np < 4; i++) {
            const Eval *e = &D.e[order[i]];
            if (e->status != W_ALIVE)
                break;
            bool seen = false;
            for (int j = 0; j < np; j++)
                seen |= D.e[parent[j]].cand == e->cand;
            if (!seen)
                parent[np++] = order[i];
        }
        free(order);
        if (!np)
            break;
        int base = D.nc;
        int *bc = malloc((size_t)np * 5 * sizeof *bc), *bs = malloc((size_t)np * 5 * sizeof *bs), nb = 0;
        if (!bc || !bs)
            abort();
        for (int p = 0; p < np; p++) {
            const Eval *pe = &D.e[parent[p]];
            char perr[200];
            GeneticCode *pgc = bgg_load(D.c[pe->cand].path, perr, sizeof perr);
            if (!pgc)
                continue;
            for (int m = 0; m < 5; m++) {
                GeneticCode *gc = mutant(pgc);
                if (!gc)
                    break;
                char label[200];
                snprintf(label, sizeof label, "bred, generation %d: a mutation of #%d", gen, pe->cand + 1);
                if (add_cand(gc, label, pe->spot)) {
                    bc[nb] = D.nc - 1;
                    bs[nb] = pe->spot;
                    nb++;
                }
                gc_unref(gc);
            }
            gc_unref(pgc);
        }
        if (nb)
            owned = run_rungs(bc, bs, nb, gen);
        free(bc);
        free(bs);
        if (D.nc == base)
            break;
    }
    stop_workers();

    /* the ranking: each candidate's best run */
    int *order = malloc((size_t)(D.ne > 0 ? D.ne : 1) * sizeof *order);
    if (!order)
        abort();
    for (int i = 0; i < D.ne; i++)
        order[i] = i;
    qsort(order, (size_t)D.ne, sizeof *order, cmp_eval_idx);
    char path[1200];
    snprintf(path, sizeof path, "%.1100s/winners.txt", D.dir);
    FILE *wf = fopen(path, "w");
    /* what the search worked on, to be run again */
    if (wf)
        fprintf(wf, "# " SIM_NAME ": %s at time %lld, the generator at %012" PRIx64 "\n# %ld runs in %.0f s%s\n",
                D.world, (long long)D.t0, state, D.runs, elapsed(), D.out_of_time ? ", when time ran out" : "");
    printf("winners (%ld runs in %.0f s%s):\n", D.runs, elapsed(), D.out_of_time ? ", when time ran out" : "");
    bool *shown = calloc((size_t)D.nc + 1, 1);
    if (!shown)
        abort();
    for (int i = 0, k = 0; i < D.ne && k < 10; i++) {
        const Eval *e = &D.e[order[i]];
        if (e->status < W_DEAD || shown[e->cand])
            continue;
        shown[e->cand] = true;
        char what[600];
        describe(e, what, sizeof what);
        printf("  %d. %s\n", ++k, what);
        if (wf)
            fprintf(wf, "%d. %s\n   %s\n", k, what, D.c[e->cand].path);
    }
    free(shown);
    free(order);
    if (wf)
        fclose(wf);

    int rc = 0;
    if (D.best < 0 || D.e[D.best].status < W_ALIVE) {
        printf("@none no candidate's bloodline lived\n");
    } else {
        const Eval *e = &D.e[D.best];
        char dst[1200];
        snprintf(dst, sizeof dst, "%.1100s/winner.bgg", D.dir);
        FILE *f = fopen(dst, "wb");
        const Cand *c = &D.c[e->cand];
        if (!f || fwrite(c->text, 1, c->ntext, f) != c->ntext) {
            printf("@none %s: %s\n", dst, strerror(errno));
            rc = 1;
        } else {
            char what[600];
            describe(e, what, sizeof what);
            printf("the winner: %s\n", what);
            V3 p = D.spot[e->spot];
            printf("@winner %s %.17g %.17g %.17g %lld %d %d %016" PRIx64 " %lld %d %s\t%s\n",
                   e->status == W_OWNS ? "owns" : "best", p.x, p.y, p.z, (long long)e->t, e->mine, e->alive, e->hash,
                   (long long)e->end_t, e->end_n, dst, c->label);
        }
        if (f)
            fclose(f);
    }
    for (int i = 0; i < D.nc; i++)
        free(D.c[i].text);
    world_free(&w);
    return rc;
}

/* ================= in the window ================= */

#define WN_LINES 64

static struct {
    bool running, done;
    ArenaProc proc;
    char part[2048];
    size_t npart;
    char status[200];
    uint64_t start_hash;
    WinnerResult res;
    bool told; /* @winner or @none */
    char lines[WN_LINES][300];
    int nl, rd;
    /* the calculation waited on */
    bool pending, owns;
    int64_t t0, t;
    int mine, alive;
    uint64_t hash;
    char verdict[300];
    bool has_verdict;
} WN;

static void wn_line(const char *s)
{
    if (WN.nl - WN.rd < WN_LINES) {
        snprintf(WN.lines[WN.nl % WN_LINES], sizeof WN.lines[0], "%.299s", s);
        WN.nl++;
    }
}

bool syn_winner_start(const World *w, V3 p, int seconds, int units, char *err, size_t n)
{
    if (WN.running) {
        snprintf(err, n, "one is being worked out already");
        return false;
    }
    char dir[1100], world[1200], e[256];
    winner_dir(dir, sizeof dir);
    if (!make_dir(dir)) {
        snprintf(err, n, "%s: %s", dir, strerror(errno));
        return false;
    }
    snprintf(world, sizeof world, "%.1100s/world.%s", dir, WORLDFILE_EXT);
    if (!worldfile_save(w, world, e, sizeof e)) {
        snprintf(err, n, "%s", e);
        return false;
    }
    char xs[32], ys[32], zs[32], ss[16], us[16];
    snprintf(xs, sizeof xs, "%.17g", p.x);
    snprintf(ys, sizeof ys, "%.17g", p.y);
    snprintf(zs, sizeof zs, "%.17g", p.z);
    snprintf(ss, sizeof ss, "%d", seconds);
    snprintf(us, sizeof us, "%d", units);
    const char *args[] = {"--winner", world, "--at",  xs,  ys,  zs,          "--seconds",
                          ss,         "--units", us, "--out", dir, "--progress"};
    if (!arena_os_spawn(&WN.proc, args, (int)(sizeof args / sizeof *args), e, sizeof e)) {
        snprintf(err, n, "%s", e);
        return false;
    }
    WN.running = true;
    WN.done = WN.told = false;
    WN.npart = 0;
    memset(&WN.res, 0, sizeof WN.res);
    WN.start_hash = world_hash(w);
    snprintf(WN.status, sizeof WN.status, "Winner: looking at the world");
    return true;
}

bool syn_winner_running(void)
{
    return WN.running;
}

uint64_t syn_winner_start_hash(void)
{
    return WN.start_hash;
}

void syn_winner_cancel(void)
{
    if (WN.running)
        arena_os_end(&WN.proc);
    WN.running = WN.done = false;
}

const char *syn_winner_status(void)
{
    return WN.running ? WN.status : NULL;
}

static void wn_parse(const char *s)
{
    if (!strncmp(s, "@begin ", 7)) {
        unsigned long long h;
        if (sscanf(s + 7, "%llx", &h) == 1 && h != WN.start_hash)
            wn_line("The world saved for the search doesn't hash as the one open: what it finds may not come true");
        return;
    }
    if (!strncmp(s, "@progress ", 10)) {
        int gen, rung, nrungs, done, n, owns;
        double secs, best;
        if (sscanf(s + 10, "%d %d %d %d %d %lf %lf %d", &gen, &rung, &nrungs, &done, &n, &secs, &best, &owns) == 8) {
            char g[32] = "";
            if (gen)
                snprintf(g, sizeof g, "bred %d, ", gen);
            snprintf(WN.status, sizeof WN.status, "Winner: %sruns %d of %d, %d of %d; best %s%.0f%%; %.0f s", g, rung,
                     nrungs, done, n, owns ? "all, " : "", best * 100, secs);
        }
        return;
    }
    if (!strncmp(s, "@winner ", 8)) {
        WinnerResult *r = &WN.res;
        char kind[8];
        double x, y, z;
        long long t, end_t;
        unsigned long long h;
        int used = 0;
        if (sscanf(s + 8, "%7s %lf %lf %lf %lld %d %d %llx %lld %d %n", kind, &x, &y, &z, &t, &r->mine, &r->alive, &h,
                   &end_t, &r->end_n, &used) == 10 &&
            used > 0) {
            r->p = v3(x, y, z);
            r->end_t = end_t;
            const char *rest = s + 8 + used, *tab = strchr(rest, '\t');
            size_t pn = tab ? (size_t)(tab - rest) : strlen(rest);
            if (pn >= sizeof r->bgg)
                pn = sizeof r->bgg - 1;
            memcpy(r->bgg, rest, pn);
            r->bgg[pn] = '\0';
            snprintf(r->label, sizeof r->label, "%s", tab ? tab + 1 : "");
            r->found = true;
            r->owns = !strcmp(kind, "owns");
            r->t = t;
            r->hash = h;
        }
        WN.told = true;
        return;
    }
    if (!strncmp(s, "@none", 5)) {
        WN.res.found = false;
        snprintf(WN.res.why, sizeof WN.res.why, "%.199s", s[5] ? s + 6 : "nothing found");
        WN.told = true;
        return;
    }
    if (s[0] != '@')
        wn_line(s);
}

bool syn_winner_poll(WinnerResult *r)
{
    if (!WN.running)
        return false;
    char buf[4096];
    for (;;) {
        long m = arena_os_read(&WN.proc, buf, sizeof buf);
        if (m == 0)
            return false;
        if (m < 0)
            break;
        for (long i = 0; i < m; i++) {
            if (buf[i] == '\n' || WN.npart + 1 >= sizeof WN.part) {
                WN.part[WN.npart] = '\0';
                wn_parse(WN.part);
                WN.npart = 0;
            } else if (buf[i] != '\r') {
                WN.part[WN.npart++] = buf[i];
            }
        }
    }
    /* it has ended */
    if (WN.npart) {
        WN.part[WN.npart] = '\0';
        wn_parse(WN.part);
        WN.npart = 0;
    }
    arena_os_end(&WN.proc);
    WN.running = false;
    if (!WN.told) {
        WN.res.found = false;
        snprintf(WN.res.why, sizeof WN.res.why, "the search stopped before it could say");
    }
    *r = WN.res;
    return true;
}

const char *syn_winner_line(void)
{
    if (WN.rd >= WN.nl)
        return NULL;
    return WN.lines[WN.rd++ % WN_LINES];
}

void syn_winner_expect(const World *w, const WinnerResult *r)
{
    syn_history_on_unit(syn_winner_unit);
    WN.pending = true;
    WN.owns = r->owns;
    WN.t0 = world_time_units(w);
    WN.t = r->t;
    WN.mine = r->mine;
    WN.alive = r->alive;
    WN.hash = r->hash;
}

void syn_winner_unit(const World *w)
{
    if (!WN.pending)
        return;
    int64_t now = world_time_units(w);
    /* another world since (a new one, or one opened) */
    if (now < WN.t0 || now > WN.t) {
        WN.pending = false;
        return;
    }
    if (now < WN.t)
        return;
    WN.pending = false;
    char t[32], a[32], b[32];
    num(WN.t, t, sizeof t);
    if (world_hash(w) != WN.hash)
        snprintf(WN.verdict, sizeof WN.verdict,
                 "Time %s: the world is not as calculated. Something else was done to it after the winner was placed",
                 t);
    else if (WN.owns)
        snprintf(WN.verdict, sizeof WN.verdict, "Time %s, as calculated: the winner's bloodline is all %s living", t,
                 num(WN.alive, a, sizeof a));
    else
        snprintf(WN.verdict, sizeof WN.verdict, "Time %s, as calculated: the winner's bloodline is %s of the %s living",
                 t, num(WN.mine, a, sizeof a), num(WN.alive, b, sizeof b));
    WN.has_verdict = true;
}

const char *syn_winner_verdict(void)
{
    if (!WN.has_verdict)
        return NULL;
    WN.has_verdict = false;
    return WN.verdict;
}
