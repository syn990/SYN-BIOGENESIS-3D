/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/crack.c, the SYN edition of its C
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

#include "syn/crack.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "sim/dmath.h"
#include "sim/genetic_code.h"
#include "sim/jrandom.h"
#include "sim/organism.h"
#include "sim/params.h"
#include "sim/stats.h"
#include "sim/world.h"
#include "syn/arena_os.h"
#include "syn/telescope.h"

/* draws from org_new's start to its founder's gene j's length, m of the
 * genes before it bent: org_new's turn (2), the mirroring, arms and genes'
 * number (3), six per gene and two more for a bend */
#define GENE_AT(j, m) (5 + 6 * (int64_t)(j) + 2 * (int64_t)(m))
/* the most draws one founder takes: 8 genes all bent, dispersal, six
 * drives, its height, plan and cone, twelve tries at a place; a colour's,
 * a bend's or a plan's draw is retried about once in 2^24 */
#define FOUNDER_MAX (GENE_AT(CRACK_FOUNDER_GENES, CRACK_FOUNDER_GENES) + 1 + 12 + 5 + 36)
/* a later draw further than this from the ancestor isn't of its stream: a
 * world run for years makes fewer, a stranger lands nearer once in 4096 */
#define STREAM 36

static uint64_t dbits(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    return u;
}

GeneticCode *crack_founder_at(uint64_t start)
{
    uint64_t keep = g_rng.seed;
    g_rng.seed = start & TEL_MASK;
    (void)jr_next_double(&g_rng); /* org_new's turn */
    GeneticCode *gc = gc_new_random();
    g_rng.seed = keep;
    return gc;
}

bool crack_same(const GeneticCode *g, const GeneticCode *h)
{
    if (g->ngenes != h->ngenes || g->symmetry != h->symmetry || g->mirror != h->mirror ||
        g->disperse_children != h->disperse_children)
        return false;
    for (int j = 0; j < g->ngenes; j++)
        if (dbits(g->genes[j].length) != dbits(h->genes[j].length) ||
            dbits(g->genes[j].theta) != dbits(h->genes[j].theta) ||
            dbits(g->genes[j].pitch) != dbits(h->genes[j].pitch) || g->genes[j].color != h->genes[j].color)
            return false;
    for (int d = 0; d < DRIVE_COUNT; d++)
        if (dbits(g->drive[d]) != dbits(h->drive[d]))
            return false;
    return dbits(g->height) == dbits(h->height) && g->plan == h->plan && dbits(g->cone) == dbits(h->cone);
}

/* --- the ancestor --- */

enum { B_LENGTH, B_THETA, B_PITCH };

static bool has_bits(const GeneticCode *gc, uint64_t b, int what)
{
    for (int j = 0; j < gc->ngenes; j++) {
        const Gene *g = &gc->genes[j];
        double v = what == B_LENGTH ? g->length : what == B_THETA ? g->theta : g->pitch;
        if (dbits(v) == b)
            return true;
    }
    return false;
}

static bool same_gene(const Gene *a, const Gene *b)
{
    return dbits(a->length) == dbits(b->length) && dbits(a->theta) == dbits(b->theta) &&
           dbits(a->pitch) == dbits(b->pitch) && a->color == b->color;
}

/* What of the being the founder proves: its lengths, turns, bends and
 * drives found in it (one of them the length cracked), and its genes it
 * has whole. A bend of 0 is no draw, so it proves nothing. */
static void proof(const GeneticCode *being, const GeneticCode *f, int *draws, int *kept)
{
    *draws = *kept = 0;
    for (int j = 0; j < being->ngenes; j++) {
        const Gene *g = &being->genes[j];
        *draws += has_bits(f, dbits(g->length), B_LENGTH) + has_bits(f, dbits(g->theta), B_THETA) +
                  (g->pitch != 0 && has_bits(f, dbits(g->pitch), B_PITCH));
        for (int k = 0; k < f->ngenes; k++)
            if (same_gene(&f->genes[k], g)) {
                (*kept)++;
                break;
            }
    }
    for (int d = 0; d < DRIVE_COUNT; d++)
        *draws += dbits(being->drive[d]) == dbits(f->drive[d]);
    *draws += (dbits(being->height) == dbits(f->height)) + (dbits(being->cone) == dbits(f->cone));
}

int crack_changes(const GeneticCode *from, const GeneticCode *to)
{
    /* an alignment of the genes in order: a gene kept costs what of it was
     * drawn again, one dropped or added costs one */
    int n = from->ngenes, m = to->ngenes;
    int *row = malloc((size_t)(m + 1) * 2 * sizeof *row);
    if (!row)
        abort();
    int *prev = row, *cur = row + m + 1;
    for (int b = 0; b <= m; b++)
        prev[b] = b;
    for (int a = 1; a <= n; a++) {
        cur[0] = a;
        const Gene *x = &from->genes[a - 1];
        for (int b = 1; b <= m; b++) {
            const Gene *y = &to->genes[b - 1];
            int keep = (dbits(x->length) != dbits(y->length)) + (dbits(x->theta) != dbits(y->theta)) +
                       (dbits(x->pitch) != dbits(y->pitch)) + (x->color != y->color);
            int best = prev[b - 1] + keep;
            if (prev[b] + 1 < best)
                best = prev[b] + 1;
            if (cur[b - 1] + 1 < best)
                best = cur[b - 1] + 1;
            cur[b] = best;
        }
        int *t = prev;
        prev = cur;
        cur = t;
    }
    int d = prev[m];
    free(row);
    for (int k = 0; k < DRIVE_COUNT; k++)
        d += dbits(from->drive[k]) != dbits(to->drive[k]);
    d += (dbits(from->height) != dbits(to->height)) + (from->plan != to->plan) +
         (dbits(from->cone) != dbits(to->cone));
    return d + (from->symmetry != to->symmetry) + (from->mirror != to->mirror) +
           (from->disperse_children != to->disperse_children);
}

int crack_ancestors(const GeneticCode *being, CrackAncestor *out, int max)
{
    int n = 0;
    for (int j = 0; j < being->ngenes; j++) {
        uint64_t st[2];
        int k = tel_crack_length(being->genes[j].length, st, 2);
        for (int c = 0; c < k && c < 2; c++)
            for (int at = 0; at < CRACK_FOUNDER_GENES; at++)
                for (int bent = 0; bent <= at; bent++) {
                    uint64_t start = tel_jump(st[c], -GENE_AT(at, bent));
                    bool seen = false;
                    for (int q = 0; q < n && !seen; q++)
                        seen = out[q].start == start;
                    if (seen)
                        continue;
                    GeneticCode *f = crack_founder_at(start);
                    /* the founder drawn from there has the length where it
                     * was cracked from only if as many genes before it bent */
                    if (at >= f->ngenes || dbits(f->genes[at].length) != dbits(being->genes[j].length)) {
                        gc_unref(f);
                        continue;
                    }
                    int draws, kept;
                    proof(being, f, &draws, &kept);
                    /* the cracked length is in it by construction: anything
                     * more is a 53-bit coincidence or the founder itself */
                    if (draws >= 2 && n < max) {
                        out[n++] = (CrackAncestor){start, f, at, draws - 1, kept, crack_changes(f, being)};
                        f = NULL;
                    }
                    gc_unref(f);
                }
    }
    /* the fewest mutations from it first, then the best proved */
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0 && (out[b].changes < out[b - 1].changes ||
                                  (out[b].changes == out[b - 1].changes && out[b].proof > out[b - 1].proof));
             b--) {
            CrackAncestor t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return n;
}

void crack_ancestors_free(CrackAncestor *a, int n)
{
    for (int i = 0; i < n; i++) {
        gc_unref(a[i].gc);
        a[i].gc = NULL;
    }
}

/* the turn gene_randomize_theta draws from this state */
static uint64_t angle_from(uint64_t state)
{
    uint64_t s1 = tel_jump(state, 1), s2 = tel_jump(s1, 1);
    uint64_t k = (s1 >> 22) << 27 | s2 >> 21;
    return dbits((double)k * 0x1.0p-53 * 2.0 * D_PI);
}

/* the draw placed in the ancestor's stream */
static void place_draw(CrackDraw *d, const CrackAncestor *a, int k, const uint64_t *st)
{
    if (k <= 0)
        return;
    d->state = st[0];
    if (a) {
        uint64_t at = tel_draws(a->start, d->state);
        d->place = at < (1ULL << STREAM) ? (int64_t)at : -1;
    }
}

int crack_draws(const GeneticCode *being, const CrackAncestor *a, CrackDraw *out, int max)
{
    int n = 0;
    for (int j = 0; j < being->ngenes && n < max; j++) {
        const Gene *g = &being->genes[j];
        bool kept_len = a && has_bits(a->gc, dbits(g->length), B_LENGTH);
        uint64_t st[2];
        if (!kept_len || !has_bits(a->gc, dbits(g->theta), B_THETA)) {
            CrackDraw d = {j, kept_len ? 'a' : 'l', false, 0, dbits(kept_len ? g->theta : g->length), -1};
            int k = kept_len ? tel_crack_theta(g->theta, st, 2) : tel_crack_length(g->length, st, 2);
            place_draw(&d, a, k, st);
            /* gene_randomize draws the turn straight after the length */
            d.whole = k > 0 && !kept_len && angle_from(tel_jump(st[0], 2)) == dbits(g->theta);
            out[n++] = d;
        }
        /* a bend drawn again (a gene added whole drew its own: the same
         * stream, so it says nothing more) */
        if (a && g->pitch != 0 && !has_bits(a->gc, dbits(g->pitch), B_PITCH) && n < max &&
            !(n && out[n - 1].gene == j && out[n - 1].whole)) {
            CrackDraw d = {j, 'b', false, 0, dbits(g->pitch), -1};
            place_draw(&d, a, tel_crack_pitch(g->pitch, st, 2), st);
            out[n++] = d;
        }
    }
    /* drives drawn again */
    for (int k = 0; a && k < DRIVE_COUNT && n < max; k++)
        if (dbits(being->drive[k]) != dbits(a->gc->drive[k])) {
            uint64_t st[2];
            CrackDraw d = {k, 'd', false, 0, dbits(being->drive[k]), -1};
            place_draw(&d, a, tel_crack_drive(being->drive[k], st, 2), st);
            out[n++] = d;
        }
    /* its height and cone drawn again */
    uint64_t st[2];
    if (a && dbits(being->height) != dbits(a->gc->height) && n < max) {
        CrackDraw d = {0, 'h', false, 0, dbits(being->height), -1};
        place_draw(&d, a, tel_crack_unit(being->height, st, 2), st);
        out[n++] = d;
    }
    if (a && dbits(being->cone) != dbits(a->gc->cone) && n < max) {
        CrackDraw d = {0, 'k', false, 0, dbits(being->cone), -1};
        place_draw(&d, a, tel_crack_pitch(being->cone, st, 2), st);
        out[n++] = d;
    }
    /* With no ancestor, the stream has no known start, but its draws still
     * have an order: each placed after the earliest of them, the one the
     * most of the others follow closely. */
    if (!a) {
        int ref = -1, best = 0;
        for (int i = 0; i < n; i++) {
            int near = 0;
            for (int k = 0; k < n && out[i].state; k++)
                near += out[k].state && tel_draws(out[i].state, out[k].state) < (1ULL << STREAM);
            if (near > best) {
                best = near;
                ref = i;
            }
        }
        for (int k = 0; k < n && ref >= 0; k++) {
            uint64_t at = out[k].state ? tel_draws(out[ref].state, out[k].state) : TEL_MASK;
            out[k].place = at < (1ULL << STREAM) ? (int64_t)at : -1;
        }
    }
    /* the soonest first; those of no stream last */
    for (int x = 1; x < n; x++)
        for (int y = x; y > 0; y--) {
            int64_t p = out[y - 1].place < 0 ? INT64_MAX : out[y - 1].place;
            int64_t q = out[y].place < 0 ? INT64_MAX : out[y].place;
            if (q >= p)
                break;
            CrackDraw t = out[y];
            out[y] = out[y - 1];
            out[y - 1] = t;
        }
    return n;
}

/* --- the fitness, and the chance of leading --- */

/* A conditional logit fitted on 300 replayed worlds (docs/crack,
 * tools/founders.c and tools/crack-fit.py): which founder's bloodline has
 * the most living at time 300, from the founders' codes. Held out, the
 * fittest founder's leads in 33% of worlds, against 3% by chance. Lengths
 * are a colour's segments in all arms. */
static const double k_fit[] = {
    +1.03, /* log(1 + green length) */
    -0.16, /* log(1 + red) */
    -0.08, /* log(1 + cyan) */
    -0.35, /* log(1 + blue) */
    -0.13, /* log(1 + white) */
    -0.30, /* log(1 + gray) */
    +0.06, /* log(1 + yellow) */
    -1.10, /* log(reproduction energy) */
    -1.29, /* arms / 8 */
    -0.14, /* the share of genes that bend */
    +0.19, +0.15, +0.03, -0.08, -0.04, -0.26, /* the drives: light, carbon, plants, hunters, dead, crowd */
    +0.25, /* the light drive x log(1 + green) / 5 */
    -0.09, /* log(1 + purple) */
    +1.44, /* the height its line keeps to */
    -0.03, -0.10, /* a cone, an orb */
};

double crack_fitness(const GeneticCode *gc)
{
    double len[COL_COUNT] = {0};
    int bent = 0;
    for (int j = 0; j < gc->ngenes; j++) {
        len[gc->genes[j].color] += gc->genes[j].length * gc->symmetry;
        bent += gc->genes[j].pitch != 0;
    }
    double lg = log1p(len[COL_GREEN]);
    double f[] = {lg,
                  log1p(len[COL_RED]),
                  log1p(len[COL_CYAN]),
                  log1p(len[COL_BLUE]),
                  log1p(len[COL_WHITE]),
                  log1p(len[COL_GRAY]),
                  log1p(len[COL_YELLOW]),
                  log(gc->reproduce_energy > 0 ? gc->reproduce_energy : 1),
                  gc->symmetry / 8.0,
                  gc->ngenes ? (double)bent / gc->ngenes : 0,
                  gc->drive[DRIVE_LIGHT],
                  gc->drive[DRIVE_CARBON],
                  gc->drive[DRIVE_PLANTS],
                  gc->drive[DRIVE_HUNTERS],
                  gc->drive[DRIVE_DEAD],
                  gc->drive[DRIVE_CROWD],
                  gc->drive[DRIVE_LIGHT] * lg / 5,
                  log1p(len[COL_PURPLE]),
                  gc->height,
                  gc->plan == PLAN_CONE,
                  gc->plan == PLAN_ORB};
    _Static_assert(sizeof f / sizeof *f == sizeof k_fit / sizeof *k_fit, "a coefficient for every feature");
    double s = 0;
    for (size_t k = 0; k < sizeof f / sizeof *f; k++)
        s += k_fit[k] * f[k];
    return s;
}

double crack_win_chance(const double *fit, int n, int i)
{
    double m = -1e300, sum = 0;
    for (int k = 0; k < n; k++)
        m = fit[k] > m ? fit[k] : m;
    for (int k = 0; k < n; k++)
        sum += exp(fit[k] - m);
    return i >= 0 && i < n && sum > 0 ? exp(fit[i] - m) / sum : 0;
}

/* --- genesis, a founder at a time --- */

/* world_genesis (sim/world.c) up to its loop; tests/telescope_check.c
 * holds the two to the same world */
void crack_genesis_begin(World *w)
{
    world_clear(w);
    w->frame = 0;
    w->o2 = P.initial_o2;
    w->lost = 0;
    w->next_id = 0;
    w->population = 0;
    memset(&w->counts, 0, sizeof w->counts);
    w->width = P.world_width;
    w->height = P.world_height;
    w->depth = P.world_depth;
    world_reset_water(w, P.co2_density);
    stats_release(&w->stats);
    stats_init(&w->stats, P.initial_organisms, w->o2, w->co2);
}

/* one turn of its loop */
bool crack_genesis_next(World *w, int *made)
{
    if (*made >= P.initial_organisms)
        return false;
    Organism *b = org_new(w);
    if (org_random_create(b))
        world_add_organism(w, b, NULL);
    else
        org_free(b);
    (*made)++;
    return *made < P.initial_organisms;
}

/* --- the seeds that make the ancestor --- */

/* The world's genesis from state s0 (P's), up to the founder begun at
 * `start`: its slot, or -1 once past it. */
static int slot_from(World *w, uint64_t s0, uint64_t start, uint64_t within)
{
    g_rng.seed = s0;
    crack_genesis_begin(w);
    int made = 0;
    for (int i = 0; i < P.initial_organisms; i++) {
        if (g_rng.seed == start)
            return i;
        if (tel_draws(s0, g_rng.seed) > within)
            return -1;
        crack_genesis_next(w, &made);
    }
    return -1;
}

int crack_basin(uint64_t start, int64_t back, CrackSeed *out, int max)
{
    uint64_t keep = g_rng.seed;
    if (back <= 0)
        back = (int64_t)P.initial_organisms * FOUNDER_MAX;
    World w;
    world_init(&w);
    int n = 0;
    uint64_t s0 = start & TEL_MASK;
    GeneticCode *f = crack_founder_at(start);
    for (int64_t k = 0; k <= back; k++, s0 = tel_jump(s0, -1)) {
        int slot = slot_from(&w, s0, start, (uint64_t)k);
        if (slot < 0)
            continue;
        /* the whole genesis: which organism it is, the one drawn from start
         * (the slot counts founders that found no place too; if it found
         * none itself, nothing in this world comes of it) */
        g_rng.seed = s0;
        world_genesis(&w);
        int me = -1;
        for (int i = 0; i < w.norgs && me < 0; i++)
            if (crack_same(w.orgs[i]->gc, f))
                me = i;
        if (me < 0)
            continue;
        if (out && n < max)
            out[n] = (CrackSeed){s0, tel_state_seed(s0), slot, me, w.norgs};
        n++;
    }
    gc_unref(f);
    world_free(&w);
    g_rng.seed = keep;
    return n;
}

/* --- the commands --- */

static bool parse_i64(const char *s, int64_t *v)
{
    char *end;
    *v = strtoll(s, &end, 0);
    return *s && !*end;
}

/* the parameters as saved (or the defaults), in a world of its own if given */
static void params_for(bool defaults, int w, int h, int d, int n)
{
    params_set_defaults(&P);
    if (!defaults)
        params_load(&P);
    if (n >= 0) {
        P.world_width = w;
        P.world_height = h;
        P.world_depth = d;
        P.initial_organisms = n;
    }
}

void crack_say_code(const char *head, const GeneticCode *gc)
{
    int bent = 0;
    for (int j = 0; j < gc->ngenes; j++)
        bent += gc->genes[j].pitch != 0;
    printf("%s%d gene%s (%d bent), %d arm%s%s:", head, gc->ngenes, gc->ngenes == 1 ? "" : "s", bent,
           gc->symmetry, gc->symmetry == 1 ? "" : "s", gc->mirror ? ", mirrored" : "");
    for (int j = 0; j < gc->ngenes; j++)
        printf(" %s %.2f", bio_color_name(gc->genes[j].color), gc->genes[j].length);
    printf("\n");
}

/* syn-biogenesis-3d --telescope crack BEING.bgg: the algebra alone, no
 * world run: its ancestor, its later draws, and the seeds that make it. */
static int cmd_crack(int argc, char **argv)
{
    const char *path = NULL;
    bool defaults = false;
    int w = 0, h = 0, dp = 0, n = -1;
    int64_t back = 0;
    int top = 20;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--defaults"))
            defaults = true;
        else if (!strcmp(argv[i], "--world") && i + 4 < argc) {
            w = atoi(argv[i + 1]);
            h = atoi(argv[i + 2]);
            dp = atoi(argv[i + 3]);
            n = atoi(argv[i + 4]);
            i += 4;
        } else if (!strcmp(argv[i], "--back") && i + 1 < argc && parse_i64(argv[i + 1], &back))
            i++;
        else if (!strcmp(argv[i], "--seeds") && i + 1 < argc)
            top = atoi(argv[++i]);
        else if (!path && argv[i][0] != '-')
            path = argv[i];
        else
            return 2;
    }
    if (!path)
        return 2;
    params_for(defaults, w, h, dp, n);
    char err[256];
    GeneticCode *gc = bgg_load(path, err, sizeof err);
    if (!gc) {
        fprintf(stderr, "%s: %s\n", path, err);
        return 1;
    }
    crack_say_code("the being: ", gc);
    CrackAncestor anc[8];
    int na = crack_ancestors(gc, anc, 8);
    if (!na)
        printf("no genesis ancestor: none of its genes is a founder's gene it still has with another draw to "
               "prove it (its seed has faded, or it was made by hand)\n");
    for (int a = 0; a < na; a++) {
        printf("@ancestor %d 0x%012" PRIx64 " %d %d %d %d\n", a, anc[a].start, anc[a].gene, anc[a].proof,
               anc[a].kept, anc[a].changes);
        if (a == 0)
            printf("its genesis ancestor, begun at state 0x%012" PRIx64 " (%d more of its draws prove it):\n",
                   anc[a].start, anc[a].proof);
        else
            printf("or, begun at state 0x%012" PRIx64 ":\n", anc[a].start);
        crack_say_code("  ", anc[a].gc);
        printf("  %d of its %d genes unchanged since, %d mutation%s at the fewest\n", anc[a].kept, gc->ngenes,
               anc[a].changes, anc[a].changes == 1 ? "" : "s");
    }
    CrackDraw dr[256];
    int nd = crack_draws(gc, na ? &anc[0] : NULL, dr, 256);
    if (!na && nd && dr[0].place == 0)
        printf("its draws in the order they were made, counted from the earliest (gene %d), in a stream whose"
               " start is unknown:\n", dr[0].gene + 1);
    for (int d = 0; d < nd; d++) {
        char c = dr[d].whole ? 'w' : dr[d].what;
        const char *what = c == 'a'   ? "its turn drawn again"
                           : c == 'b' ? "its bend drawn again"
                           : c == 'd' || c == 'h' || c == 'k' ? "drawn again"
                           : c == 'w' ? "added whole"
                                      : "its length drawn again";
        char who[48];
        if (c == 'd')
            snprintf(who, sizeof who, "its %s drive", gc_drive_name(dr[d].gene));
        else if (c == 'h' || c == 'k')
            snprintf(who, sizeof who, "its %s", c == 'h' ? "height" : "cone");
        else
            snprintf(who, sizeof who, "gene %d", dr[d].gene + 1);
        printf("@draw %d %" PRId64 " %c\n", dr[d].gene + 1, dr[d].place, c);
        if (!dr[d].state)
            printf("  %s: a number the generator never draws (edited by hand)\n", who);
        else if (!na && dr[d].place == 0)
            printf("  %s: %s, the earliest\n", who, what);
        else if (dr[d].place >= 0)
            printf("  %s: %s %" PRId64 " draws after %s\n", who, what, dr[d].place,
                   na ? "its ancestor was begun" : "the earliest");
        else
            printf("  %s: %s, in another stream%s\n", who, what,
                   na ? " than its ancestor's (an infection, or a being put together)" : "");
    }
    for (int a = 0; a < na && a < 1; a++) {
        CrackSeed *s = malloc(4096 * sizeof *s);
        if (!s)
            abort();
        double t0 = arena_os_now();
        int ns = crack_basin(anc[a].start, back, s, 4096);
        printf("@basin %d %d %d %d %d\n", P.world_width, P.world_height, P.world_depth, P.initial_organisms, ns);
        printf("%d seed%s make it a founder, in %d x %d x %d with %d first organisms (%.2f s):\n", ns,
               ns == 1 ? "" : "s", P.world_width, P.world_height, P.world_depth, P.initial_organisms,
               arena_os_now() - t0);
        for (int i = 0; i < ns && i < 4096; i++) {
            printf("@seed %" PRId64 " %d %d\n", s[i].seed, s[i].slot, s[i].founders);
            if (i < top)
                printf("  seed %" PRId64 ": founder %d of %d\n", s[i].seed, s[i].slot + 1, s[i].founders);
        }
        free(s);
    }
    crack_ancestors_free(anc, na);
    gc_unref(gc);
    return 0;
}

/* A world run, its founders' bloodlines marked (gc->lineage, which the
 * simulation only copies), to time unit `units` or until one bloodline is
 * left: the founder leading (the most living), or -1 if all died; *lone
 * whether it was alone, *t the time unit it ended, *share its part of the
 * living. */
static int leading_bloodline(World *w, int64_t units, bool progress, bool *lone, int *t, double *share)
{
    int nf = w->norgs < 1024 ? w->norgs : 1024;
    for (int i = 0; i < nf; i++)
        w->orgs[i]->gc->lineage = (uint32_t)i + 1;
    static int count[1024];
    *lone = false;
    for (;;) {
        bool end = world_time_units(w) >= units;
        if (!end) {
            world_time(w);
            if (w->frame % 256)
                continue;
            if (progress && world_time_units(w) % 25 == 0)
                printf("@frame %" PRId64 " %" PRId64 "\n", w->frame, units * 256);
        }
        memset(count, 0, sizeof(int) * (size_t)nf);
        int alive = 0, lines = 0, lead = -1;
        for (int i = 0; i < w->norgs; i++) {
            const Organism *q = w->orgs[i];
            if (q->removed || !q->alive || !q->gc->lineage || q->gc->lineage > (uint32_t)nf)
                continue;
            alive++;
            int k = (int)q->gc->lineage - 1;
            lines += count[k]++ == 0;
            if (lead < 0 || count[k] > count[lead])
                lead = k;
        }
        *t = (int)world_time_units(w);
        if (!alive)
            return -1;
        *share = (double)count[lead] / alive;
        if (lines == 1) {
            *lone = true;
            return lead;
        }
        if (end)
            return lead;
    }
}

/* The commonest code alive of a bloodline (the first such), with how many
 * have it. */
static const GeneticCode *commonest(const World *w, uint32_t line, int *count)
{
    const GeneticCode *best = NULL;
    *count = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *q = w->orgs[i];
        if (q->removed || !q->alive || q->gc->lineage != line)
            continue;
        int c = 0;
        for (int k = 0; k < w->norgs; k++) {
            const Organism *r = w->orgs[k];
            c += !r->removed && r->alive && r->gc->lineage == line && crack_same(r->gc, q->gc);
        }
        if (c > *count) {
            *count = c;
            best = q->gc;
        }
    }
    return best;
}

typedef struct {
    int64_t seed;
    int win;
    double chance;
} Forged;

static int cmp_forged(const void *a, const void *b)
{
    const Forged *x = a, *y = b;
    if (x->chance != y->chance)
        return x->chance > y->chance ? -1 : 1;
    return x->seed < y->seed ? -1 : x->seed > y->seed;
}

/* syn-biogenesis-3d --telescope forge: a winner from the dice. Each seed's
 * founders are made (genesis only, no world run) and weighed; the seeds
 * whose likeliest bloodline is likeliest are run, and the being their
 * leading bloodline leads as is the winner. */
static int cmd_forge(int argc, char **argv)
{
    bool defaults = false, progress = false;
    int w = 0, h = 0, dp = 0, n = -1, top = 8;
    int64_t from = 1, scan = 20000, units = 300;
    const char *out = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--defaults"))
            defaults = true;
        else if (!strcmp(a, "--progress"))
            progress = true;
        else if (!strcmp(a, "--world") && i + 4 < argc) {
            w = atoi(argv[i + 1]);
            h = atoi(argv[i + 2]);
            dp = atoi(argv[i + 3]);
            n = atoi(argv[i + 4]);
            i += 4;
        } else if (!strcmp(a, "--from") && i + 1 < argc && parse_i64(argv[i + 1], &from))
            i++;
        else if (!strcmp(a, "--scan") && i + 1 < argc && parse_i64(argv[i + 1], &scan))
            i++;
        else if (!strcmp(a, "--units") && i + 1 < argc && parse_i64(argv[i + 1], &units))
            i++;
        else if (!strcmp(a, "--top") && i + 1 < argc)
            top = atoi(argv[++i]);
        else if (!strcmp(a, "--out") && i + 1 < argc)
            out = argv[++i];
        else
            return 2;
    }
    if (scan < 1 || top < 1 || units < 1)
        return 2;
    if (progress) {
        arena_os_child_begin();
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
    params_for(defaults, w, h, dp, n);
    printf("@set 0 %d %d %d %d\n", P.world_width, P.world_height, P.world_depth, P.initial_organisms);
    Forged *fg = malloc((size_t)scan * sizeof *fg);
    if (!fg)
        abort();
    World world;
    world_init(&world);
    double t0 = arena_os_now();
    for (int64_t s = 0; s < scan; s++) {
        jr_set_seed(&g_rng, from + s);
        world_genesis(&world);
        static double fit[1024];
        int nf = world.norgs < 1024 ? world.norgs : 1024, best = 0;
        if (!nf) {
            fg[s] = (Forged){from + s, 0, 0};
            continue;
        }
        for (int i = 0; i < nf; i++) {
            fit[i] = crack_fitness(world.orgs[i]->gc);
            if (fit[i] > fit[best])
                best = i;
        }
        fg[s] = (Forged){from + s, best, crack_win_chance(fit, nf, best)};
        if (progress && (s + 1) % 500 == 0)
            printf("@scan %" PRId64 " %" PRId64 "\n", s + 1, scan);
    }
    double t_scan = arena_os_now() - t0;
    qsort(fg, (size_t)scan, sizeof *fg, cmp_forged);
    printf("%" PRId64 " seeds weighed from their founders alone in %.1f s; the %d likeliest run to time %" PRId64 ":\n",
           scan, t_scan, top, units);
    int right = 0, ran = 0;
    for (int c = 0; c < top && c < scan; c++) {
        printf("@progress 0 %d %d\n", c + 1, top);
        jr_set_seed(&g_rng, fg[c].seed);
        world_genesis(&world);
        /* the founders as genesis made them, for the one whose bloodline
         * leads */
        int nf = world.norgs < 1024 ? world.norgs : 1024;
        static GeneticCode *founder[1024];
        for (int i = 0; i < nf; i++)
            founder[i] = gc_ref(world.orgs[i]->gc);
        int t = 0;
        bool lone = false;
        double share = 0;
        int lead = leading_bloodline(&world, units, progress, &lone, &t, &share);
        ran++;
        right += lead == fg[c].win;
        int copies = 0;
        const GeneticCode *being = lead >= 0 ? commonest(&world, (uint32_t)lead + 1, &copies) : NULL;
        int changes = being && lead < nf ? crack_changes(founder[lead], being) : -1;
        printf("@forge %" PRId64 " %d %d %d %.6f %d %d %d %.4f\n", fg[c].seed, fg[c].win, lead, t, fg[c].chance, copies,
               changes, lone, share);
        if (lead < 0)
            printf("  seed %" PRId64 ": all died by time %d\n", fg[c].seed, t);
        else
            printf("  seed %" PRId64 ": founder %d expected (%.0f%%); founder %d's bloodline %s at time %d, %.0f%% of the"
                   " living, its commonest being %d of them (%d mutation%s from that founder)\n",
                   fg[c].seed, fg[c].win + 1, 100 * fg[c].chance, lead + 1, lone ? "alone" : "leads", t, 100 * share,
                   copies, changes, changes == 1 ? "" : "s");
        if (out && being) {
            char p[1200], err[200];
            snprintf(p, sizeof p, "%s/seed-%" PRId64 ".bgg", out, fg[c].seed);
            if (bgg_save(p, being, err, sizeof err))
                printf("@file %" PRId64 " %s\n", fg[c].seed, p);
        }
        for (int i = 0; i < nf; i++)
            gc_unref(founder[i]);
    }
    printf("the expected founder's bloodline led in %d of %d\n", right, ran);
    printf("@done\n");
    world_free(&world);
    free(fg);
    return 0;
}

/* syn-biogenesis-3d --telescope basins N: how many seeds make a founder at
 * a state, over N states from a fixed generator */
static int cmd_basins(int argc, char **argv)
{
    int64_t count = 0;
    bool defaults = false;
    int w = 0, h = 0, dp = 0, n = -1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--defaults"))
            defaults = true;
        else if (!strcmp(argv[i], "--world") && i + 4 < argc) {
            w = atoi(argv[i + 1]);
            h = atoi(argv[i + 2]);
            dp = atoi(argv[i + 3]);
            n = atoi(argv[i + 4]);
            i += 4;
        } else if (!count && parse_i64(argv[i], &count))
            ;
        else
            return 2;
    }
    if (count < 1)
        return 2;
    params_for(defaults, w, h, dp, n);
    JRandom r;
    jr_set_seed(&r, 20261024);
    int64_t hist[65] = {0}, sum = 0, most = 0;
    double t0 = arena_os_now();
    for (int64_t i = 0; i < count; i++) {
        uint64_t start = (uint64_t)(jr_next_double(&r) * 0x1.0p48) & TEL_MASK;
        int k = crack_basin(start, 0, NULL, 0);
        hist[k < 64 ? k : 64]++;
        sum += k;
        most = k > most ? k : most;
        printf("@basin %" PRId64 " 0x%012" PRIx64 " %d\n", i, start, k);
    }
    printf("%" PRId64 " states, %d x %d x %d with %d first organisms (%.1f s): seeds making a founder there, %.2f"
           " on average, %" PRId64 " at most\n",
           count, P.world_width, P.world_height, P.world_depth, P.initial_organisms, arena_os_now() - t0,
           (double)sum / count, most);
    for (int k = 0; k < 64; k++)
        if (hist[k])
            printf("  %d: %" PRId64 "\n", k, hist[k]);
    if (hist[64])
        printf("  64 or more: %" PRId64 "\n", hist[64]);
    return 0;
}

int crack_main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "basins"))
        return cmd_basins(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "crack"))
        return cmd_crack(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "forge"))
        return cmd_forge(argc, argv);
    return 2;
}
