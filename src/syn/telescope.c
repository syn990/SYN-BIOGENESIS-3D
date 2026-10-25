/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/telescope.c, the SYN edition of its
 * C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "syn/telescope.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "io/worldfile.h"
#include "sim/dmath.h"
#include "sim/genetic_code.h"
#include "sim/jrandom.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/world.h"
#include "syn/arena.h"
#include "syn/arena_os.h"
#include "syn/crack.h"
#include "syn/record.h"
#include "syn/round.h"

#define TEL_A 0x5DEECE66DULL
#define TEL_B 0xBULL

/* x -> a x + b (mod 2^48) */
typedef struct {
    uint64_t a, b;
} Affine;

/* f after g */
static Affine compose(Affine f, Affine g)
{
    return (Affine){(f.a * g.a) & TEL_MASK, (f.a * g.b + f.b) & TEL_MASK};
}

/* a^-1 mod 2^64 for odd a, by Newton's iteration: a is its own inverse to
 * 3 bits, and each step doubles the bits that are right. */
static uint64_t inverse_odd(uint64_t a)
{
    uint64_t x = a;
    for (int i = 0; i < 5; i++)
        x *= 2 - a * x;
    return x;
}

/* One draw back: x = A^-1 (x' - B). */
static Affine step_back(void)
{
    uint64_t ai = inverse_odd(TEL_A) & TEL_MASK;
    return (Affine){ai, (0 - ai * TEL_B) & TEL_MASK};
}

uint64_t tel_jump(uint64_t state, int64_t n)
{
    Affine step = n < 0 ? step_back() : (Affine){TEL_A, TEL_B};
    uint64_t k = n < 0 ? 0 - (uint64_t)n : (uint64_t)n;
    Affine r = {1, 0};
    for (; k; k >>= 1) {
        if (k & 1)
            r = compose(step, r);
        step = compose(step, step);
    }
    return (r.a * (state & TEL_MASK) + r.b) & TEL_MASK;
}

/* Bit k of the count: once `x`, the state after the bits below it, agrees
 * with `to` in its low k bits, 2^k draws more keep those bits and flip bit
 * k (the low k + 1 bits go round every 2^(k+1) draws, all values in turn),
 * so they are needed exactly when bit k still disagrees. */
uint64_t tel_draws(uint64_t from, uint64_t to)
{
    Affine step = {TEL_A, TEL_B}; /* 2^k draws */
    uint64_t x = from & TEL_MASK, n = 0;
    to &= TEL_MASK;
    for (int k = 0; k < 48; k++) {
        if ((x ^ to) >> k & 1) {
            x = (step.a * x + step.b) & TEL_MASK;
            n |= 1ULL << k;
        }
        step = compose(step, step);
    }
    return n;
}

uint64_t tel_seed_state(int64_t seed)
{
    return ((uint64_t)seed ^ TEL_A) & TEL_MASK;
}

int64_t tel_state_seed(uint64_t state)
{
    return (int64_t)((state ^ TEL_A) & TEL_MASK);
}

/* nextDouble() is (next(26) << 27 | next(27)) / 2^53: the top 26 bits of
 * one state, then the top 27 bits of the next. The 22 bits below the first
 * are tried one by one (4 million, some milliseconds), and the next state
 * must agree; each state that does is returned as the one before the draw. */
static int crack_double(uint64_t k, uint64_t *out, int n, int max)
{
    uint64_t hi = k >> 27, lo = k & ((1ULL << 27) - 1);
    uint64_t s1 = hi << 22;
    uint64_t s2 = (s1 * TEL_A + TEL_B) & TEL_MASK;
    for (uint64_t x = 0; x < 1ULL << 22; x++) {
        if (s2 >> 21 == lo) {
            if (n < max)
                out[n] = tel_jump(s1, -1);
            n++;
        }
        s1++;
        s2 = (s2 + TEL_A) & TEL_MASK;
    }
    return n;
}

/* gene_randomize_length and gene_randomize_theta, from u = k / 2^53 */
static double length_of(uint64_t k)
{
    return 2.0 + (double)k * 0x1.0p-53 * 16.0;
}

static double theta_of(uint64_t k)
{
    return (double)k * 0x1.0p-53 * 2.0 * D_PI;
}

int tel_crack_length(double length, uint64_t *out, int max)
{
    if (!(length >= 2.0 && length < 18.0))
        return 0;
    /* exact: 2 is a multiple of the length's last place, and the rest is
     * powers of two; from 16 up the length has lost a bit, hence the
     * neighbours */
    int64_t k0 = (int64_t)((length - 2.0) / 16.0 * 0x1.0p53);
    int n = 0;
    for (int64_t k = k0 - 4; k <= k0 + 4; k++)
        if (k >= 0 && k < (1LL << 53) && length_of((uint64_t)k) == length)
            n = crack_double((uint64_t)k, out, n, max);
    return n;
}

int tel_crack_theta(double theta, uint64_t *out, int max)
{
    if (!(theta >= 0.0 && theta < 2.0 * D_PI))
        return 0;
    /* times pi rounds, so a few neighbours can make the same turn */
    int64_t k0 = (int64_t)(theta / (2.0 * D_PI) * 0x1.0p53);
    int n = 0;
    for (int64_t k = k0 - 16; k <= k0 + 16; k++)
        if (k >= 0 && k < (1LL << 53) && theta_of((uint64_t)k) == theta)
            n = crack_double((uint64_t)k, out, n, max);
    return n;
}

static double pitch_of(uint64_t k)
{
    return ((double)k * 0x1.0p-53 - 0.5) * D_PI;
}

static double drive_of(uint64_t k)
{
    return (double)k * 0x1.0p-53 * 2.0 - 1.0;
}

int tel_crack_pitch(double pitch, uint64_t *out, int max)
{
    if (!(pitch != 0 && pitch >= -0.5 * D_PI && pitch < 0.5 * D_PI))
        return 0;
    /* times pi rounds, as with a turn */
    int64_t k0 = (int64_t)((pitch / D_PI + 0.5) * 0x1.0p53);
    int n = 0;
    for (int64_t k = k0 - 16; k <= k0 + 16; k++)
        if (k >= 0 && k < (1LL << 53) && pitch_of((uint64_t)k) == pitch)
            n = crack_double((uint64_t)k, out, n, max);
    return n;
}

int tel_crack_unit(double u, uint64_t *out, int max)
{
    if (!(u >= 0 && u < 1))
        return 0;
    double k = u * 0x1.0p53;
    if (k != (double)(uint64_t)k)
        return 0;
    return crack_double((uint64_t)k, out, 0, max);
}

int tel_crack_drive(double drive, uint64_t *out, int max)
{
    if (!(drive >= -1.0 && drive < 1.0))
        return 0;
    /* less one, a drive below 0 has lost its lowest bits, hence the
     * neighbours */
    int64_t k0 = (int64_t)((drive + 1.0) * 0.5 * 0x1.0p53);
    int n = 0;
    for (int64_t k = k0 - 8; k <= k0 + 8; k++)
        if (k >= 0 && k < (1LL << 53) && drive_of((uint64_t)k) == drive)
            n = crack_double((uint64_t)k, out, n, max);
    return n;
}

static uint64_t dbits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static bool parse_u64(const char *s, uint64_t *v)
{
    char *end;
    *v = strtoull(s, &end, 0);
    return *s && !*end;
}

static bool parse_i64(const char *s, int64_t *v)
{
    char *end;
    *v = strtoll(s, &end, 0);
    return *s && !*end;
}

static bool parse_double(const char *s, double *v)
{
    char *end;
    *v = strtod(s, &end);
    return *s && !*end;
}

int tel_code_distance(const GeneticCode *a, const GeneticCode *b)
{
    int d = (a->symmetry != b->symmetry) + (a->mirror != b->mirror);
    int n = a->ngenes < b->ngenes ? a->ngenes : b->ngenes;
    d += a->ngenes > b->ngenes ? a->ngenes - b->ngenes : b->ngenes - a->ngenes;
    for (int j = 0; j < n; j++)
        d += dbits(a->genes[j].length) != dbits(b->genes[j].length) ||
             dbits(a->genes[j].theta) != dbits(b->genes[j].theta) ||
             dbits(a->genes[j].pitch) != dbits(b->genes[j].pitch) || a->genes[j].color != b->genes[j].color;
    return d;
}

/* the parameters as saved (or the defaults), in a world of its own if given */
typedef struct {
    int w, h, d, n; /* n < 0: the parameters' own */
} FindSet;

static void set_params(const FindSet *s, bool defaults)
{
    params_set_defaults(&P);
    if (!defaults)
        params_load(&P);
    if (s->n >= 0) {
        P.world_width = s->w;
        P.world_height = s->h;
        P.world_depth = s->d;
        P.initial_organisms = s->n;
    }
}

static void progress_on(void)
{
    arena_os_child_begin();
    setvbuf(stdout, NULL, _IOLBF, 0);
}

/* --- the seed of a saved world --- */

/* A gene length in the saved world, and how many genes have it. */
typedef struct {
    uint64_t bits;
    int count;
} Fossil;

static int cmp_fossil(const void *a, const void *b)
{
    const Fossil *x = a, *y = b;
    if (x->count != y->count)
        return y->count - x->count;
    return x->bits < y->bits ? -1 : x->bits > y->bits;
}

static void add_genes(const GeneticCode *gc, uint64_t **v, int *n, int *cap)
{
    if (!gc)
        return;
    for (int j = 0; j < gc->ngenes; j++) {
        if (*n == *cap) {
            *cap = *cap ? *cap * 2 : 1024;
            *v = realloc(*v, (size_t)*cap * sizeof **v);
            if (!*v)
                abort();
        }
        (*v)[(*n)++] = dbits(gc->genes[j].length);
    }
}

/* How many of the founders' gene lengths genesis makes from this state are
 * in the saved world (of how many). */
static void founders_have(World *scratch, uint64_t state, const uint64_t *have, int nhave, int *found, int *total)
{
    g_rng.seed = state;
    world_genesis(scratch);
    *found = *total = 0;
    for (int i = 0; i < scratch->norgs; i++) {
        const GeneticCode *gc = scratch->orgs[i]->gc;
        for (int j = 0; j < gc->ngenes; j++) {
            uint64_t b = dbits(gc->genes[j].length);
            (*total)++;
            if (bsearch(&b, have, (size_t)nhave, sizeof *have, cmp_u64))
                (*found)++;
        }
    }
}

/* A seed whose founders have the gene: the state it starts at, and how
 * many of its founders' genes have lengths still in the saved world. */
typedef struct {
    uint64_t state;
    int slot;
    int found, total;
    int fossil;
} Candidate;

/* most founder genes still in the world first, then the soonest slot */
static int cmp_candidate(const void *a, const void *b)
{
    const Candidate *x = a, *y = b;
    if (x->found != y->found)
        return y->found - x->found;
    return x->slot - y->slot;
}

/* A saved world's statistics keep records with the time unit each was set
 * in: the highest and lowest O2, CO2 and population, and the organisms
 * with the most children, kills and infections. Once a replay is past that
 * time its record must be the saved one exactly, and a different world
 * almost never matches them, so a wrong seed is turned away at the
 * earliest record instead of at the saved frame. */
static const char *record_difference(const WorldStats *r, const WorldStats *s)
{
    int64_t t = r->time;
#define AT_END(field, name)                                                                       \
    if (s->field##_time <= t &&                                                                   \
        (dbits(r->field) != dbits(s->field) || r->field##_time != s->field##_time))               \
        return name;
#define DURING(field, value, name)                                                                \
    if (s->field##_time < t && (r->value != s->value || r->field##_time != s->field##_time))      \
        return name;
    AT_END(max_oxygen, "the highest O2")
    AT_END(min_oxygen, "the lowest O2")
    AT_END(max_co2, "the highest CO2")
    AT_END(min_co2, "the lowest CO2")
    DURING(max_population, max_population, "the highest population")
    DURING(min_population, min_population, "the lowest population")
    DURING(most_children, most_children_n, "the most children")
    DURING(most_kills, most_kills_n, "the most kills")
    DURING(most_infections, most_infections_n, "the most infections")
    return NULL;
#undef AT_END
#undef DURING
}

/* The first time unit at which some record can turn a wrong seed away.
 * Records of time unit 0 are left out: most are the values every world
 * starts with. */
static int64_t first_record(const WorldStats *s)
{
    int64_t at_end[] = {s->max_oxygen_time, s->min_oxygen_time, s->max_co2_time, s->min_co2_time};
    int64_t during[] = {s->max_population_time, s->min_population_time, s->most_children_time,
                        s->most_kills_time, s->most_infections_time};
    int64_t m = INT64_MAX;
    for (size_t i = 0; i < sizeof at_end / sizeof *at_end; i++)
        if (at_end[i] >= 1 && at_end[i] < m)
            m = at_end[i];
    for (size_t i = 0; i < sizeof during / sizeof *during; i++)
        if (during[i] >= 1 && during[i] + 1 < m)
            m = during[i] + 1;
    return m == INT64_MAX ? 0 : m;
}

/* The saved world's oldest living organism: its age gives the frame it was
 * born at, so a replay that is the saved world has an organism with its
 * id, its genes and exactly the age it should have from a few frames after
 * that birth on. Returns the frame to check at (0 for none). */
static int64_t oldest_check(const World *saved, int64_t frames, const Organism **oldest)
{
    *oldest = NULL;
    for (int i = 0; i < saved->norgs; i++) {
        const Organism *o = saved->orgs[i];
        if (o->alive && (!*oldest || o->age > (*oldest)->age))
            *oldest = o;
    }
    if (!*oldest || (*oldest)->age < 16)
        return 0;
    return frames - (*oldest)->age + 8;
}

/* Replays the world from this state to the saved frame, checking the saved
 * records and the oldest organism's birth as it passes them, then the
 * whole world (world_hash, the dice with it): whether it is the saved
 * world. */
static bool replay_matches(uint64_t state, const World *saved, uint64_t saved_hash, bool progress)
{
    int64_t frames = saved->frame;
    const Organism *oldest;
    int64_t born_check = oldest_check(saved, frames, &oldest);
    World r;
    world_init(&r);
    g_rng.seed = state;
    world_genesis(&r);
    double t1 = arena_os_now();
    const char *d = NULL;
    char why[160];
    while (r.frame < frames && !d) {
        int64_t units = r.stats.time;
        world_time(&r);
        if (r.stats.time != units) {
            d = record_difference(&r.stats, &saved->stats);
            if (progress && r.stats.time % 25 == 0)
                printf("@replay %" PRId64 " %" PRId64 "\n", r.frame, frames);
        }
        if (r.frame == born_check) {
            const Organism *o = NULL;
            for (int i = 0; i < r.norgs && !o; i++)
                if (r.orgs[i]->id == oldest->id)
                    o = r.orgs[i];
            if (!o || !o->alive || o->age != oldest->age - (frames - r.frame) || !crack_same(o->gc, oldest->gc)) {
                snprintf(why, sizeof why, "organism %d, the oldest alive at the save,", oldest->id);
                d = why;
            }
        }
    }
    if (!d && world_hash(&r) != saved_hash)
        d = "the world as a whole";
    if (d && r.frame < frames) {
        printf("    turned away at frame %" PRId64 ": %s is not the saved world's\n", r.frame, d);
    } else if (d) {
        printf("    replayed to frame %" PRId64 ": not this world (%s differs)\n", frames, d);
    } else {
        printf("    replayed to frame %" PRId64 " in %.1f s: identical to the saved world\n", frames,
               arena_os_now() - t1);
        printf("seed %" PRId64 " (and every seed equal to it mod 2^48)\n", tel_state_seed(state));
        printf("the generator at the saved frame: 0x%012" PRIx64 "\n", g_rng.seed);
    }
    world_free(&r);
    return !d;
}

static int cmd_world(int argc, char **argv)
{
    const char *path = NULL;
    bool replay = true, strong = false, given_seed = false, progress = false;
    int64_t only_seed = 0;
    int max_fossils = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--no-replay")) {
            replay = false;
        } else if (!strcmp(a, "--strong")) {
            strong = true;
        } else if (!strcmp(a, "--progress")) {
            progress = true;
        } else if (!strcmp(a, "--seed") && v && parse_i64(v, &only_seed)) {
            given_seed = true;
            i++;
        } else if (!strcmp(a, "--lengths") && v) {
            max_fossils = atoi(v);
            i++;
        } else if (!path && a[0] != '-') {
            path = a;
        } else {
            return 2;
        }
    }
    if (!path)
        return 2;
    /* for the window (app/telescope.c): each line as it is printed, the
     * answer on a line of its own, and gone with the window */
    if (progress)
        progress_on();

    World saved;
    world_init(&saved);
    char err[256];
    /* the parameters are the save's, as are the dice */
    if (!worldfile_load(&saved, path, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        printf("@none\n");
        return 1;
    }
    uint64_t saved_hash = world_hash(&saved);
    Params saved_p = P;

    /* every gene length in the world, alive and dead, and the codes waiting
     * to infect */
    uint64_t *all = NULL;
    int nall = 0, cap = 0;
    for (int i = 0; i < saved.norgs; i++) {
        add_genes(saved.orgs[i]->gc, &all, &nall, &cap);
        add_genes(saved.orgs[i]->infected_gc, &all, &nall, &cap);
    }
    qsort(all, (size_t)nall, sizeof *all, cmp_u64);
    Fossil *fos = malloc((size_t)(nall ? nall : 1) * sizeof *fos);
    uint64_t *have = malloc((size_t)(nall ? nall : 1) * sizeof *have);
    if (!fos || !have)
        abort();
    int nfos = 0;
    for (int i = 0; i < nall; i++) {
        if (nfos && fos[nfos - 1].bits == all[i]) {
            fos[nfos - 1].count++;
        } else {
            have[nfos] = all[i];
            fos[nfos++] = (Fossil){all[i], 1};
        }
    }
    free(all);
    qsort(fos, (size_t)nfos, sizeof *fos, cmp_fossil);
    printf("%s: %d x %d x %d, frame %" PRId64 " (time %" PRId64 "), %d organisms (%d alive), %d different gene"
           " lengths\n",
           path, saved.width, saved.height, saved.depth, saved.frame, saved.frame / 256, saved.norgs,
           saved.population, nfos);

    World scratch;
    world_init(&scratch);
    if (given_seed) {
        uint64_t x = tel_seed_state(only_seed);
        int found, total;
        founders_have(&scratch, x, have, nfos, &found, &total);
        printf("  seed %" PRId64 ": %d of its founders' %d genes still in the world\n", tel_state_seed(x), found,
               total);
        P = saved_p;
        int r = replay && !replay_matches(x, &saved, saved_hash, progress);
        if (progress)
            printf(r ? "@none\n" : "@found %" PRId64 " 0x%012" PRIx64 "\n", only_seed, g_rng.seed);
        world_free(&scratch);
        world_free(&saved);
        free(fos);
        free(have);
        return r;
    }

    /* A founder's gene still in the world was drawn at genesis: crack its
     * length, step back to the founders it can be gene j of (crack.h), and
     * find every seed whose genesis begins one of them. Those are ranked by
     * how many of their founders' genes are still in the saved world and
     * replayed in that order until one is the world. A seed whose founders
     * share no other gene with the world has nothing but the gene itself
     * for it: those wait for a second pass. */
    int64_t early = first_record(&saved.stats) * 256;
    const Organism *oldest;
    int64_t born = oldest_check(&saved, saved.frame, &oldest);
    if (early > 0 && early < saved.frame)
        printf("the save's records go back to frame %" PRId64 "\n", early);
    if (born > 0)
        printf("its oldest organism, %d, was born %d frames before the save\n", oldest->id, oldest->age);
    double t0 = arena_os_now();
    int tried = 0, replays = 0, listed = 0;
    int64_t found_seed = 0;
    uint64_t found_state = 0;
    Candidate *cand = NULL, *weak = NULL;
    int ncand = 0, candcap = 0, nweak = 0, weakcap = 0;
    int64_t *tested = NULL;
    int ntested = 0, testedcap = 0;
    int status = 1;
    if (max_fossils <= 0 || max_fossils > nfos)
        max_fossils = nfos;
    CrackSeed *bs = malloc(4096 * sizeof *bs);
    if (!bs)
        abort();
    for (int pass = 0; pass < 2 && status; pass++) {
        if (pass == 1) {
            if (strong || !nweak || !replay)
                break;
            printf("%d seed%s whose founders share only the gene they were found from:\n", nweak,
                   nweak == 1 ? "" : "s");
        }
        for (int f = 0; f < (pass ? 1 : max_fossils) && status; f++) {
            if (pass == 0) {
                double length;
                memcpy(&length, &fos[f].bits, sizeof length);
                uint64_t st[4];
                int nst = tel_crack_length(length, st, 4);
                tried++;
                if (progress)
                    printf("@length %d %d\n", f + 1, max_fossils);
                ncand = 0;
                for (int k = 0; k < nst && k < 4; k++)
                    for (int at = 0; at < CRACK_FOUNDER_GENES; at++)
                        for (int bent = 0; bent <= at; bent++) {
                            uint64_t start = tel_jump(st[k], -(5 + 6 * (int64_t)at + 2 * (int64_t)bent));
                            GeneticCode *fd = crack_founder_at(start);
                            bool fits = at < fd->ngenes && dbits(fd->genes[at].length) == fos[f].bits;
                            gc_unref(fd);
                            if (!fits)
                                continue;
                            P = saved_p;
                            int nb = crack_basin(start, 0, bs, 4096);
                            for (int s = 0; s < nb && s < 4096; s++) {
                                bool seen = false;
                                for (int c = 0; c < ncand && !seen; c++)
                                    seen = cand[c].state == bs[s].state;
                                for (int c = 0; c < nweak && !seen; c++)
                                    seen = weak[c].state == bs[s].state;
                                if (seen)
                                    continue;
                                int found, total;
                                P = saved_p;
                                founders_have(&scratch, bs[s].state, have, nfos, &found, &total);
                                Candidate c = {bs[s].state, bs[s].slot, found, total, f};
                                if (found < 2) {
                                    if (nweak == weakcap) {
                                        weakcap = weakcap ? weakcap * 2 : 64;
                                        weak = realloc(weak, (size_t)weakcap * sizeof *weak);
                                        if (!weak)
                                            abort();
                                    }
                                    weak[nweak++] = c;
                                    continue;
                                }
                                if (ncand == candcap) {
                                    candcap = candcap ? candcap * 2 : 64;
                                    cand = realloc(cand, (size_t)candcap * sizeof *cand);
                                    if (!cand)
                                        abort();
                                }
                                cand[ncand++] = c;
                            }
                        }
                if (!ncand)
                    continue;
                qsort(cand, (size_t)ncand, sizeof *cand, cmp_candidate);
                printf("gene length %.17g (%d in the world): %d seed%s whose founders have it and more\n", length,
                       fos[f].count, ncand, ncand == 1 ? "" : "s");
            }
            Candidate *list = pass ? weak : cand;
            int n = pass ? nweak : ncand;
            for (int c = 0; c < n && status; c++) {
                int64_t seed = tel_state_seed(list[c].state);
                bool seen = false;
                for (int k = 0; k < ntested && !seen; k++)
                    seen = tested[k] == seed;
                if (seen)
                    continue;
                printf("  seed %" PRId64 ": the gene in founder %d; %d of its founders' %d genes in the world\n",
                       seed, list[c].slot + 1, list[c].found, list[c].total);
                if (!replay) {
                    listed++;
                    continue;
                }
                if (ntested == testedcap) {
                    testedcap = testedcap ? testedcap * 2 : 64;
                    tested = realloc(tested, (size_t)testedcap * sizeof *tested);
                    if (!tested)
                        abort();
                }
                tested[ntested++] = seed;
                replays++;
                P = saved_p;
                if (progress)
                    printf("@try %" PRId64 "\n", seed);
                if (replay_matches(list[c].state, &saved, saved_hash, progress)) {
                    status = 0;
                    found_seed = seed;
                    found_state = g_rng.seed;
                }
            }
        }
    }
    free(bs);
    if (progress)
        printf(status ? "@none\n" : "@found %" PRId64 " 0x%012" PRIx64 "\n", found_seed, found_state);
    if (!replay) {
        printf("%d seed%s whose founders share two genes or more with the world, %d sharing only one, from %d"
               " gene lengths in %.1f s (none replayed)\n",
               listed, listed == 1 ? "" : "s", nweak, tried, arena_os_now() - t0);
        status = listed ? 0 : 1;
    } else if (status) {
        printf("no seed found: %d gene lengths tried, %d replay%s, in %.1f s\n", tried, replays,
               replays == 1 ? "" : "s", arena_os_now() - t0);
        if (strong && nweak)
            printf("%d seeds whose founders share only one gene with the world were not replayed (--strong)\n",
                   nweak);
    } else {
        printf("%d gene length%s, %d replay%s, in %.1f s\n", tried, tried == 1 ? "" : "s", replays,
               replays == 1 ? "" : "s", arena_os_now() - t0);
    }
    free(weak);
    free(cand);
    free(tested);
    world_free(&scratch);
    world_free(&saved);
    free(fos);
    free(have);
    return status;
}

/* --- where a being lives --- */

#define ROUND_ARC 32

/* What a run shows of the being: a frame and what is there. */
typedef struct {
    int64_t frame;
    const char *kind; /* ancestor, gene, alive, most, lone, reigns, gone, ... */
    int n, d;
} Scene;

#define SCENES 24

/* A round's world watched for the being, once a time unit: when it is
 * alive (the first time, and when most), else its nearest relative; each
 * of its genes drawn in the round where the stream passes it. */
static bool watch(World *w, uint64_t s0, const int64_t *place, const int *gene, int nplace, const GeneticCode *gc,
                  int64_t frames_max, Scene *sc, int *nsc, int64_t *to)
{
    int64_t genesis = (int64_t)tel_draws(s0, g_rng.seed);
    int half = gc->ngenes / 2;
    *nsc = 0;
    bool drawn[256] = {0};
    for (int k = 0; k < nplace && k < 256; k++)
        if (place[k] < genesis && *nsc < SCENES) {
            sc[(*nsc)++] = (Scene){0, "founder", gene[k], 0};
            drawn[k] = true;
        }
    int64_t alive_at = -1, near_at = -1, peak_at = -1;
    int near = half + 1, copies = 0, peak = 0;
    int64_t f;
    for (f = 1; f <= frames_max; f++) {
        world_time(w);
        int64_t now = (int64_t)tel_draws(s0, g_rng.seed);
        for (int k = 0; k < nplace && k < 256; k++)
            if (!drawn[k] && now > place[k]) {
                drawn[k] = true;
                if (*nsc < SCENES - 3)
                    sc[(*nsc)++] = (Scene){f, "gene", gene[k], 0};
            }
        if (w->frame % 256)
            continue;
        int here = 0, best = 1 << 30;
        for (int i = 0; i < w->norgs; i++) {
            const Organism *q = w->orgs[i];
            if (q->removed || !q->alive)
                continue;
            int d = tel_code_distance(q->gc, gc);
            here += d == 0;
            best = d < best ? d : best;
        }
        if (here && alive_at < 0) {
            alive_at = f;
            copies = here;
            if (*nsc < SCENES)
                sc[(*nsc)++] = (Scene){f, "alive", here, 0};
        }
        if (here > peak) {
            peak = here;
            peak_at = f;
        }
        if (!here && alive_at < 0 && best < near) {
            near = best;
            near_at = f;
        }
        if (w->population == 0)
            break;
    }
    if (peak_at >= 0 && peak > copies && *nsc < SCENES)
        sc[(*nsc)++] = (Scene){peak_at, "most", peak, 0};
    if (alive_at < 0 && near_at >= 0 && *nsc < SCENES)
        sc[(*nsc)++] = (Scene){near_at, "nearest", near, near};
    *to = f > frames_max ? frames_max : f;
    return alive_at >= 0;
}

static void say_scenes(const char *head, const Scene *sc, int n)
{
    for (int i = 0; i < n; i++)
        printf("%s %" PRId64 " %s %d %d\n", head, sc[i].frame, sc[i].kind, sc[i].n, sc[i].d);
}

#define BASIN 4096 /* seeds of an ancestor's basin kept, far more than any has */
/* a seed's first run, with later draws to check (cmd_find) */
#define SHORT_UNITS 48

/* A seed of the ancestor's basin run and watched. Each of the being's
 * later draws is checked as the stream passes it: the gene drawn there
 * must be in a child just made, or this world isn't the being's and is
 * given up at once. Then, once a time unit, the being (alive, the most of
 * it) and the bloodlines: the ancestor's left alone, and the being alive
 * while it is (it reigns: the winning state), or the bloodline gone.
 * Whether the being was seen alive; why it ended, in words. */
/* how a watched seed ended */
enum { END_RUNOUT, END_REJECTED, END_GONE, END_REIGNS, END_LONE };

static bool watch_basin(World *w, uint64_t s0, int org, const CrackDraw *dr, int nd, const GeneticCode *gc,
                        int64_t frames_max, bool progress, Scene *sc, int *nsc, int64_t *to, int *end, char *why,
                        size_t nwhy)
{
    *end = END_RUNOUT;
    int64_t place[256];
    uint64_t bits[256];
    int pg[256], np = 0;
    char what[256];
    for (int d = 0; d < nd && np < 256; d++)
        if (dr[d].place >= 0) {
            place[np] = (int64_t)tel_draws(s0, dr[d].state);
            bits[np] = dr[d].bits;
            what[np] = dr[d].what;
            pg[np++] = dr[d].gene + 1;
        }
    for (int i = 0; i < w->norgs; i++)
        w->orgs[i]->gc->lineage = (uint32_t)i + 1;
    uint32_t line = (uint32_t)org + 1;
    *nsc = 0;
    sc[(*nsc)++] = (Scene){0, "ancestor", org + 1, 0};
    int64_t alive_at = -1, lone_at = -1, peak_at = -1, f;
    int peak = 0, copies = 0, next = 0;
    snprintf(why, nwhy, "run to time %" PRId64 " without it", frames_max / 256);
    for (f = 0; f <= frames_max; f++) {
        if (f)
            world_time(w);
        if (next < np) {
            int64_t now = (int64_t)tel_draws(s0, g_rng.seed);
            for (; next < np && now > place[next]; next++) {
                bool born = false;
                for (int i = 0; i < w->norgs && !born; i++) {
                    const GeneticCode *q = w->orgs[i]->gc;
                    if (w->orgs[i]->removed || !q)
                        continue;
                    if (what[next] == 'd')
                        for (int k = 0; k < DRIVE_COUNT && !born; k++)
                            born = dbits(q->drive[k]) == bits[next];
                    if (what[next] == 'h' || what[next] == 'k')
                        born = dbits(what[next] == 'h' ? q->height : q->cone) == bits[next];
                    for (int j = 0; j < q->ngenes && !born && what[next] != 'd' && what[next] != 'h' &&
                                    what[next] != 'k';
                         j++) {
                        const Gene *g = &q->genes[j];
                        double v = what[next] == 'a' ? g->theta : what[next] == 'b' ? g->pitch : g->length;
                        born = dbits(v) == bits[next];
                    }
                }
                if (!born) {
                    if (what[next] == 'd')
                        snprintf(why, nwhy, "not its world: its %s drive's draw, at frame %" PRId64
                                 ", went to no child", gc_drive_name(pg[next] - 1), f);
                    else if (what[next] == 'h' || what[next] == 'k')
                        snprintf(why, nwhy, "not its world: its %s's draw, at frame %" PRId64 ", went to no child",
                                 what[next] == 'h' ? "height" : "cone", f);
                    else
                        snprintf(why, nwhy, "not its world: its gene %d's draw, at frame %" PRId64 ", went to no child",
                                 pg[next], f);
                    *to = f;
                    *end = END_REJECTED;
                    return false;
                }
                if (*nsc < SCENES - 4)
                    sc[(*nsc)++] = (Scene){f,
                                           what[next] == 'd'   ? "drive"
                                           : what[next] == 'b' ? "bend"
                                           : what[next] == 'h' ? "height"
                                           : what[next] == 'k' ? "cone"
                                                               : "gene",
                                           pg[next], 0};
            }
        }
        if (f % 256)
            continue;
        if (progress && f % (256 * 50) == 0)
            printf("@frame %" PRId64 " %" PRId64 "\n", f, frames_max);
        int here = 0, lines = 0;
        bool mine = false;
        uint32_t first = 0;
        for (int i = 0; i < w->norgs; i++) {
            const Organism *q = w->orgs[i];
            if (q->removed || !q->alive)
                continue;
            here += tel_code_distance(q->gc, gc) == 0;
            mine |= q->gc->lineage == line;
            if (!first)
                first = q->gc->lineage, lines = 1;
            else if (lines == 1 && q->gc->lineage != first)
                lines = 2;
        }
        if (here && alive_at < 0) {
            alive_at = f;
            copies = here;
            if (*nsc < SCENES)
                sc[(*nsc)++] = (Scene){f, "alive", here, 0};
        }
        if (here > peak) {
            peak = here;
            peak_at = f;
        }
        if (!mine) {
            if (*nsc < SCENES)
                sc[(*nsc)++] = (Scene){f, "gone", 0, 0};
            snprintf(why, nwhy, "its ancestor's bloodline died out at time %" PRId64 "%s", f / 256,
                     alive_at >= 0 ? ", after it had lived" : "");
            *end = END_GONE;
            break;
        }
        if (lines == 1 && lone_at < 0) {
            lone_at = f;
            if (*nsc < SCENES)
                sc[(*nsc)++] = (Scene){f, "lone", w->population, here};
        }
        if (lone_at >= 0 && here) {
            if (*nsc < SCENES)
                sc[(*nsc)++] = (Scene){f, "reigns", here, w->population};
            snprintf(why, nwhy, "it reigns at time %" PRId64 ": its ancestor's bloodline alone, %d of it alive",
                     f / 256, here);
            *end = END_REIGNS;
            break;
        }
        /* alone, with all its draws made and no sign of it for a while */
        if (lone_at >= 0 && next >= np && f - lone_at > 256 * 64) {
            snprintf(why, nwhy, "its ancestor's bloodline alone from time %" PRId64 ", but not as it", lone_at / 256);
            *end = END_LONE;
            break;
        }
    }
    if (peak_at >= 0 && peak > copies && *nsc < SCENES)
        sc[(*nsc)++] = (Scene){peak_at, "most", peak, 0};
    /* run out with it seen, its bloodline not yet alone */
    if (f > frames_max && alive_at >= 0)
        snprintf(why, nwhy, "it lives here from time %" PRId64 " (%d at most); its ancestor's bloodline not alone"
                 " by time %" PRId64, alive_at / 256, peak, frames_max / 256);
    *to = f > frames_max ? frames_max : f;
    return alive_at >= 0;
}

/* A round of the arena's record as it began (syn/round.h), with the
 * parameters it ran with: false if it didn't keep enough. */
static bool round_world(World *w, int i)
{
    ArenaRoundSpec s;
    if (!syn_arena_record_round(i, &s))
        return false;
    P = s.params;
    int first[ARENA_SLOTS], placed[ARENA_SLOTS];
    world_init(w);
    round_begin(w, s.seed, s.code, s.nf, s.wild, NULL, first, placed);
    syn_arena_round_spec_free(&s);
    return true;
}

static void record_open(const char *record)
{
    char rp[1200];
    if (!record) {
        snprintf(rp, sizeof rp, "%s/arena.record", params_config_dir());
        record = rp;
    }
    syn_record_set_file(record);
    syn_record_init();
}

/* syn-biogenesis-3d --telescope find CODE.bgg: where in the shape a being
 * lives: the arena's rounds that drew its genes, then its genesis ancestor
 * (crack.h) and every seed whose genesis makes it, each run with the
 * being's later draws checked as its stream passes them, and watched for
 * the being and for the ancestor's bloodline left alone. */
static int cmd_find(int argc, char **argv)
{
    const char *path = NULL, *record = NULL;
    bool defaults = false, progress = false, rounds = true;
    int64_t back = 0, units = 600;
    int top = 8, rtop = 16;
    FindSet sets[4];
    int nsets = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--defaults"))
            defaults = true;
        else if (!strcmp(a, "--progress"))
            progress = true;
        else if (!strcmp(a, "--no-record"))
            rounds = false;
        else if (!strcmp(a, "--record") && i + 1 < argc)
            record = argv[++i];
        else if (!strcmp(a, "--back") && i + 1 < argc && parse_i64(argv[i + 1], &back))
            i++;
        else if (!strcmp(a, "--units") && i + 1 < argc && parse_i64(argv[i + 1], &units))
            i++;
        else if (!strcmp(a, "--seeds") && i + 1 < argc)
            top = atoi(argv[++i]);
        else if (!strcmp(a, "--rounds") && i + 1 < argc)
            rtop = atoi(argv[++i]);
        else if (!strcmp(a, "--saved") && nsets < 4)
            sets[nsets++] = (FindSet){0, 0, 0, -1};
        else if (!strcmp(a, "--world") && i + 4 < argc && nsets < 4) {
            sets[nsets++] = (FindSet){atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4])};
            i += 4;
        } else if (!path && a[0] != '-')
            path = a;
        else
            return 2;
    }
    if (!path || back < 0 || units < 1)
        return 2;
    if (!nsets)
        sets[nsets++] = (FindSet){0, 0, 0, -1};
    if (progress)
        progress_on();
    set_params(&sets[0], defaults);
    char err[256];
    GeneticCode *gc = bgg_load(path, err, sizeof err);
    if (!gc) {
        fprintf(stderr, "%s: %s\n", path, err);
        printf("@none\n");
        return 1;
    }
    /* its genes' lengths, and the states that drew them */
    uint64_t have[256], gst[256 * 2];
    int gno[256 * 2]; /* the gene (from 1) each state drew */
    int nhave = 0, ngst = 0;
    for (int j = 0; j < gc->ngenes && nhave < 256; j++) {
        uint64_t b = dbits(gc->genes[j].length);
        bool seen = false;
        for (int k = 0; k < nhave && !seen; k++)
            seen = have[k] == b;
        if (!seen)
            have[nhave++] = b;
    }
    qsort(have, (size_t)nhave, sizeof *have, cmp_u64);
    int cracked = 0;
    for (int j = 0; j < nhave; j++) {
        double length;
        memcpy(&length, &have[j], sizeof length);
        uint64_t st[2];
        int k = tel_crack_length(length, st, 2);
        cracked += k > 0;
        int no = 0;
        for (int g = 0; g < gc->ngenes && !no; g++)
            if (dbits(gc->genes[g].length) == have[j])
                no = g + 1;
        for (int m = 0; m < k && m < 2; m++) {
            gno[ngst] = no;
            gst[ngst++] = st[m];
        }
    }
    printf("%s: %d genes, %d lengths, %d of them drawn by the generator\n", path, gc->ngenes, nhave, cracked);
    /* The arena's rounds, each an arc of the ring of states from its seed:
     * a gene whose place from a round's seed is within the draws a round
     * makes (2^ROUND_ARC is more than any) was drawn in that round, and by
     * chance in a given other one about once in 2^16. */
    if (rounds) {
        record_open(record);
        int nr = syn_record_count();
        typedef struct {
            int i, hits;
            bool final, genesis;
        } RHit;
        RHit *rh = malloc((size_t)(nr ? nr : 1) * sizeof *rh);
        if (!rh)
            abort();
        int nrh = 0;
        for (int i = 0; i < nr; i++) {
            const RecordRound *r = syn_record_at(i);
            uint64_t s0 = tel_seed_state(r->seed);
            RHit h = {i, 0, r->final && tel_code_distance(r->final, gc) == 0,
                      r->genesis && tel_code_distance(r->genesis, gc) == 0};
            for (int g = 0; g < ngst; g++)
                h.hits += tel_draws(s0, gst[g]) < (1ULL << ROUND_ARC);
            if (h.hits || h.final || h.genesis)
                rh[nrh++] = h;
        }
        /* the being itself first, then the most of its genes */
        for (int a = 1; a < nrh; a++)
            for (int b = a; b > 0; b--) {
                RHit *x = &rh[b - 1], *y = &rh[b];
                int kx = (x->final || x->genesis) * 1000 + x->hits, ky = (y->final || y->genesis) * 1000 + y->hits;
                if (ky <= kx)
                    break;
                RHit t = *x;
                *x = *y;
                *y = t;
            }
        printf("@rounds %d %d\n", nr, nrh);
        printf("%d of the arena's %d recorded round%s drew its genes or ended with it\n", nrh, nr, nr == 1 ? "" : "s");
        for (int c = 0; c < nrh && c < rtop; c++) {
            const RecordRound *r = syn_record_at(rh[c].i);
            char head[80];
            snprintf(head, sizeof head, "@rscene %d", rh[c].i);
            printf("@round %d %d %" PRId64 "\n", rh[c].i, r->cycle, r->seed);
            printf("  round %d (seed %" PRId64 "): %d of its genes drawn in it%s%s\n", r->cycle, r->seed, rh[c].hits,
                   rh[c].final ? ", it is the being the round ended with" : "",
                   rh[c].genesis ? ", it is the bloodline's genesis being" : "");
            if (rh[c].genesis)
                printf("%s 0 genesis 1 0\n", head);
            if (rh[c].final)
                printf("%s %" PRId64 " final %d 0\n", head, (int64_t)r->t_end * 256, r->pop);
            World w;
            if (!round_world(&w, rh[c].i)) {
                printf("  round %d can't be put together again: the record didn't keep enough of it\n", r->cycle);
                continue;
            }
            int64_t place[512];
            int pg[512], np = 0;
            uint64_t s0 = tel_seed_state(r->seed);
            for (int g = 0; g < ngst && np < 512; g++) {
                uint64_t at = tel_draws(s0, gst[g]);
                if (at < (1ULL << ROUND_ARC)) {
                    place[np] = (int64_t)at;
                    pg[np++] = gno[g];
                }
            }
            Scene sc[SCENES];
            int nsc;
            int64_t to;
            bool lives = watch(&w, s0, place, pg, np, gc, (int64_t)r->t_end * 256, sc, &nsc, &to);
            world_free(&w);
            say_scenes(head, sc, nsc);
            printf("  round %d: %s\n", r->cycle, lives ? "it lives here" : "a relative of it");
        }
        free(rh);
        set_params(&sets[0], defaults);
    }
    /* Its genesis ancestor, drawn again from one of its genes, and every
     * seed whose genesis makes it: each run with the being's later draws
     * checked as its stream passes them, and watched for the being and for
     * the ancestor's bloodline left alone. */
    CrackAncestor anc[8];
    int na = crack_ancestors(gc, anc, 8);
    CrackDraw dr[256];
    int nd = 0;
    bool later = false;
    if (na) {
        const CrackAncestor *a = &anc[0];
        nd = crack_draws(gc, a, dr, 256);
        printf("@ancestor 0x%012" PRIx64 " %d %d %d\n", a->start, a->proof, a->kept, a->changes);
        printf("its genesis ancestor: %d gene%s, %d arm%s, begun at state 0x%012" PRIx64
               "; %d of its genes kept since, %d mutation%s at the fewest\n",
               a->gc->ngenes, a->gc->ngenes == 1 ? "" : "s", a->gc->symmetry, a->gc->symmetry == 1 ? "" : "s",
               a->start, a->kept, a->changes, a->changes == 1 ? "" : "s");
        for (int d = 0; d < nd; d++) {
            printf("@draw %d %" PRId64 " %c\n", dr[d].gene + 1, dr[d].place, dr[d].whole ? 'w' : dr[d].what);
            later |= dr[d].place >= 0;
        }
    } else {
        printf("@ancestor none\n");
        printf("no genesis ancestor: none of its genes is a founder's it still has with another draw to prove it"
               " (its seed has faded, or it was made by hand)\n");
    }
    for (int si = 0; si < nsets && na; si++) {
        set_params(&sets[si], defaults);
        printf("@set %d %d %d %d %d\n", si, P.world_width, P.world_height, P.world_depth, P.initial_organisms);
        CrackSeed *bs = malloc(BASIN * sizeof *bs);
        if (!bs)
            abort();
        int nb = crack_basin(anc[0].start, back, bs, BASIN);
        nb = nb < BASIN ? nb : BASIN;
        printf("@basin %d %d\n", si, nb);
        printf("%d seed%s make its ancestor a founder, in %d x %d x %d with %d first organisms\n", nb,
               nb == 1 ? "" : "s", P.world_width, P.world_height, P.world_depth, P.initial_organisms);
        for (int c = 0; c < nb; c++)
            printf("@seed %d %" PRId64 " %d\n", si, bs[c].seed, bs[c].slot);
        /* With later draws it lives in one of them at most, found by its
         * draws; without, in each, and the question is where it wins.
         * With later draws, every seed is first run a short while: most
         * are turned away by a draw, or their bloodline dies, and the
         * being's own world has mostly shown it by then; those still
         * undecided are run again to the end. */
        int runs = later ? nb : nb < top ? nb : top;
        int64_t first_units = later && units > SHORT_UNITS ? SHORT_UNITS : units;
        int *again = malloc((size_t)(runs ? runs : 1) * sizeof *again), nagain = 0;
        if (!again)
            abort();
        bool lives = false;
        Scene told[SCENES];
        int ntold = 0;
        for (int pass = 0; pass < 2 && !(later && lives); pass++) {
            int n = pass ? nagain : runs;
            if (pass && n > 1)
                printf("%d seeds still undecided at time %" PRId64 ", run again to time %" PRId64 "\n", n,
                       first_units, units);
            for (int k = 0; k < n && !(later && lives); k++) {
                int c = pass ? again[k] : k;
                printf("@progress %d %d %d\n", si, k + 1, n);
                set_params(&sets[si], defaults);
                World w;
                world_init(&w);
                g_rng.seed = bs[c].state;
                world_genesis(&w);
                Scene sc[SCENES];
                int nsc, end;
                int64_t to;
                char why[200], head[80];
                bool here = watch_basin(&w, bs[c].state, bs[c].org, dr, nd, gc, (pass ? units : first_units) * 256,
                                        progress, sc, &nsc, &to, &end, why, sizeof why);
                world_free(&w);
                if (!pass && first_units < units && end == END_RUNOUT) {
                    if (here) {
                        /* its world, its scenes so far told at once: the
                         * only one to run on, to the end */
                        snprintf(head, sizeof head, "@scene %d %" PRId64, si, bs[c].seed);
                        say_scenes(head, sc, nsc);
                        memcpy(told, sc, sizeof told);
                        ntold = nsc;
                        printf("  seed %" PRId64 " (founder %d of %d): it lives here; run on to time %" PRId64 "\n",
                               bs[c].seed, bs[c].slot + 1, bs[c].founders, units);
                        again[0] = c;
                        nagain = 1;
                        break;
                    }
                    again[nagain++] = c;
                    printf("  seed %" PRId64 " (founder %d of %d): undecided at time %" PRId64 "\n", bs[c].seed,
                           bs[c].slot + 1, bs[c].founders, first_units);
                    continue;
                }
                if (here || !later) {
                    /* less those told already */
                    Scene fresh[SCENES];
                    int nf = 0;
                    for (int q = 0; q < nsc; q++) {
                        bool was = false;
                        for (int t = 0; t < ntold && pass && !was; t++)
                            was = told[t].frame == sc[q].frame && !strcmp(told[t].kind, sc[q].kind) &&
                                  told[t].n == sc[q].n;
                        if (!was)
                            fresh[nf++] = sc[q];
                    }
                    snprintf(head, sizeof head, "@scene %d %" PRId64, si, bs[c].seed);
                    say_scenes(head, fresh, nf);
                }
                printf("  seed %" PRId64 " (founder %d of %d): %s\n", bs[c].seed, bs[c].slot + 1, bs[c].founders,
                       why);
                lives |= here;
            }
        }
        free(again);
        free(bs);
    }
    printf("@done\n");
    crack_ancestors_free(anc, na);
    gc_unref(gc);
    return 0;
}

/* syn-biogenesis-3d --telescope scene SEED FRAMES OUT.b3w: the world SEED
 * makes, run FRAMES frames and saved, with the generator's state there
 * (@at STATE). --round I: round I of the record (from 0) instead. */
static int cmd_scene(int argc, char **argv)
{
    int64_t seed, frames;
    bool round = argc > 2 && !strcmp(argv[2], "--round");
    int a0 = round ? 3 : 2;
    if (argc < a0 + 3 || !parse_i64(argv[a0], &seed) || !parse_i64(argv[a0 + 1], &frames) || frames < 0)
        return 2;
    const char *out = argv[a0 + 2], *record = NULL;
    bool defaults = false, progress = false;
    FindSet set = {0, 0, 0, -1};
    for (int i = a0 + 3; i < argc; i++) {
        if (!strcmp(argv[i], "--defaults"))
            defaults = true;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc)
            record = argv[++i];
        else if (!strcmp(argv[i], "--progress"))
            progress = true;
        else if (!strcmp(argv[i], "--world") && i + 4 < argc) {
            set = (FindSet){atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4])};
            i += 4;
        } else
            return 2;
    }
    if (progress)
        progress_on();
    World w;
    if (round) {
        record_open(record);
        if (seed < 0 || seed >= syn_record_count() || !round_world(&w, (int)seed)) {
            fprintf(stderr, "round %" PRId64 ": not in the record, or not kept whole\n", seed);
            printf("@none\n");
            return 1;
        }
    } else {
        set_params(&set, defaults);
        world_init(&w);
        jr_set_seed(&g_rng, seed);
        world_genesis(&w);
    }
    for (int64_t f = 0; f < frames; f++) {
        world_time(&w);
        if (progress && f % (256 * 25) == 0)
            printf("@frame %" PRId64 " %" PRId64 "\n", f, frames);
    }
    char err[256];
    if (!worldfile_save(&w, out, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", out, err);
        printf("@none\n");
        world_free(&w);
        return 1;
    }
    printf("@at 0x%012" PRIx64 "\n", g_rng.seed);
    world_free(&w);
    return 0;
}

/* --- the command --- */

static int usage(int r)
{
    printf("usage: syn-biogenesis-3d --telescope COMMAND ...\n"
           "  The random generator (java.util.Random, 48 bits, as Biogenesis 0.9's) as algebra.\n"
           "  STATE is the generator's 48-bit state (0x... or decimal).\n"
           "  jump STATE N         the state N draws later (N < 0: earlier)\n"
           "  seed SEED            the state a seed starts the generator at, and the seed in\n"
           "                       [0, 2^48) that makes the same world\n"
           "  gene LENGTH [TURN]   every state that draws a gene of exactly this length (and\n"
           "                       then this turn); give the numbers with all their digits\n"
           "  world FILE.b3w       the seed a saved world was made from, found from its\n"
           "                       founders' genes, then proved by replaying it from frame 0\n"
           "                       to the saved frame (with the save's parameters)\n"
           "    --lengths K        try only the K commonest gene lengths\n"
           "    --no-replay        list the seeds found, replay none\n"
           "    --strong           replay only seeds whose founders share two genes or more\n"
           "                       with the world\n"
           "    --seed S           no search: how many of seed S's founders' genes are in\n"
           "                       the world, and whether replaying S gives it\n"
           "    --progress         for the window: lines as they come, \"@found SEED STATE\"\n"
           "                       or \"@none\" at the end\n"
           "  find CODE.bgg        where a being lives: the arena's recorded rounds that drew\n"
           "                       its genes, its genesis ancestor and every seed that makes\n"
           "                       it, each run with the being's later draws checked as its\n"
           "                       stream passes them, watched for the being and its\n"
           "                       ancestor's bloodline. Lines \"@scene SET SEED FRAME KIND N D\",\n"
           "                       KIND one of ancestor, gene, bend, drive, alive, most, lone,\n"
           "                       reigns, gone\n"
           "    --saved            search the parameters as saved (the default)\n"
           "    --world W H D N    search a world W x H x D with N first organisms (up to 4)\n"
           "    --defaults         the default parameters, not the saved ones\n"
           "    --units U          run each seed at most U time units (default 600)\n"
           "    --seeds K          for a being with no later draw, run K seeds (default 8)\n"
           "    --back N           draws before the ancestor to look for its seeds (default:\n"
           "                       as far as the founders reach)\n"
           "    --rounds K         run at most K rounds of the record (default 16)\n"
           "    --record FILE      the arena's record to search (default: the configuration\n"
           "                       folder's); --no-record: none\n"
           "  crack CODE.bgg       the algebra alone, no world run: the being's genesis\n"
           "                       ancestor drawn again from one of its genes, its later draws\n"
           "                       in order, and every seed whose genesis makes the ancestor\n"
           "    --defaults, --world W H D N, --back N, --seeds K (shown)\n"
           "  forge                a winner from the dice: seeds weighed by their founders\n"
           "                       alone (docs/crack), the likeliest run, the being their\n"
           "                       leading bloodline leads as the winner\n"
           "    --from SEED        the first seed weighed (default 1)\n"
           "    --scan N           how many seeds to weigh (default 20000)\n"
           "    --top K            how many of them to run (default 8)\n"
           "    --units U          the time units each runs (default 300)\n"
           "    --out DIR          each winner's being written there as seed-SEED.bgg\n"
           "    --defaults, --world W H D N, --progress\n"
           "  basins N             how many seeds make a founder at a state, over N states;\n"
           "                       --defaults, --world W H D N\n"
           "  scene SEED FRAMES OUT.b3w   the world SEED makes at that frame, saved, and the\n"
           "                       generator's state there (\"@at STATE\"); --defaults, --world\n"
           "  scene --round I FRAMES OUT.b3w   the same for round I of the record (from 0),\n"
           "                       as the round began; --record FILE\n");
    return r;
}

int syn_telescope_main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "";
    uint64_t s;
    int64_t n;
    if (!strcmp(cmd, "jump") && argc == 4 && parse_u64(argv[2], &s) && parse_i64(argv[3], &n)) {
        uint64_t t = tel_jump(s, n);
        printf("0x%012" PRIx64 " (%" PRIu64 ")\n", t, t);
        return 0;
    }
    if (!strcmp(cmd, "seed") && argc == 3 && parse_i64(argv[2], &n)) {
        uint64_t x = tel_seed_state(n);
        printf("seed %" PRId64 " starts the generator at 0x%012" PRIx64 "; the seed in [0, 2^48) that makes the"
               " same world is %" PRId64 "\n",
               n, x, tel_state_seed(x));
        return 0;
    }
    if (!strcmp(cmd, "gene") && (argc == 3 || argc == 4)) {
        double length, theta = -1;
        if (!parse_double(argv[2], &length) || (argc == 4 && !parse_double(argv[3], &theta)))
            return usage(2);
        uint64_t st[16];
        int k = tel_crack_length(length, st, 16);
        printf("%d state%s draw a length of %.17g\n", k, k == 1 ? "" : "s", length);
        for (int i = 0; i < k && i < 16; i++) {
            bool ok = true;
            if (theta >= 0) {
                /* the turn is drawn straight after */
                uint64_t s1 = tel_jump(st[i], 3), s2 = tel_jump(s1, 1);
                uint64_t kk = (s1 >> 22) << 27 | s2 >> 21;
                ok = theta_of(kk) == theta;
            }
            printf("  0x%012" PRIx64 "%s\n", st[i], theta < 0 ? "" : ok ? ", and then this turn" : ", but another turn");
        }
        return k ? 0 : 1;
    }
    int r = 2;
    if (!strcmp(cmd, "world"))
        r = cmd_world(argc, argv);
    else if (!strcmp(cmd, "find"))
        r = cmd_find(argc, argv);
    else if (!strcmp(cmd, "scene"))
        r = cmd_scene(argc, argv);
    else if (!strcmp(cmd, "crack") || !strcmp(cmd, "basins") || !strcmp(cmd, "forge"))
        r = crack_main(argc, argv);
    return r == 2 ? usage(2) : r;
}
