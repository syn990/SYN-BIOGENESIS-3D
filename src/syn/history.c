/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/history.c, the SYN edition of its C port
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

#include "syn/history.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "syn/family.h"

#ifndef HIST_CAP
#define HIST_CAP 4096 /* samples; then pairs merge and a sample covers twice the time */
#endif
#ifndef EPOCHS_CAP
#define EPOCHS_CAP 1024 /* base epochs; then pairs merge and an epoch is twice as long */
#endif
#define EPOCH_BASE0 16   /* time units in a base epoch to start with */
#define MERGE_UNITS 32 /* records this close together are one event, climbing */
#define HOLD 4         /* samples a new leader must hold the lead to count */


static struct {
    HistSample *s;
    int n, stride;
    int64_t commit_t;                      /* time of the last kept sample */
    int64_t birth0, death0, inf0;          /* the world's totals then */
    int64_t ubirth0, udeath0, uinf0;       /* and at the last time unit */
    int64_t kill0, bite0;                  /* the world's kills and bites at the last sample */
    Epoch *epochs;
    int nepochs, epoch_base;
    int64_t epoch_k0; /* epochs[i] is epoch number epoch_k0 + i */
    Event *ev;
    int nev, cap_ev;
    Line *lines;
    int nlines, cap_lines;
    uint32_t *map; /* line key -> index + 1, open addressing */
    int map_cap;
    int64_t t_start, seen_t;
    bool started;
    void (*on_unit)(const World *w); /* syn_history_on_unit */

    /* what the detectors last saw */
    int extinctions;
    int lead_col, col_cand, col_hold;
    int col_zero[HIST_COLOURS], col_peak[HIST_COLOURS];
    bool col_seen[HIST_COLOURS], col_gone[HIST_COLOURS];
    uint32_t lead_line, line_cand;
    int line_hold;
    int pop_record;
    int rec_children, rec_kills, rec_infected;
    bool co2_low, o2_low;
    History view;
} H;

/* ---- small helpers ---- */

/* Holding a code: 0.9 counts references; Color Mod's collector is told
 * what the history holds (history_mark). */
#define keep_code gc_assign


static void *grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap)
        return p;
    int c = *cap ? *cap : 64;
    while (c < need)
        c *= 2;
    void *q = realloc(p, (size_t)c * size);
    if (!q)
        abort();
    *cap = c;
    return q;
}

static int commonest_colour(const Organism *o)
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

/* The colours that make events: genes' own (0.9's seven, Color Mod's 46),
 * not the states Color Mod's segments pass through */
static bool gene_colour(int slot)
{
    Role r = syn_col_role(slot);
    return r != ROLE_INACTIVE && r != ROLE_DEAD && strcmp(syn_col_name(slot), "other") != 0;
}

static void num(char *out, size_t n, double v)
{
    /* as fmt_num (ui/app.c), which the history doesn't link against */
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.1f", fabs(v));
    char *dot = strchr(tmp, '.');
    if (dot && dot[1] == '0')
        *dot = '\0';
    dot = strchr(tmp, '.');
    int ilen = dot ? (int)(dot - tmp) : (int)strlen(tmp);
    char buf[96];
    int k = 0;
    if (v < 0 && strcmp(tmp, "0") != 0)
        buf[k++] = '-';
    for (int i = 0; i < ilen; i++) {
        buf[k++] = tmp[i];
        int left = ilen - i - 1;
        if (left > 0 && left % 3 == 0)
            buf[k++] = ',';
    }
    snprintf(buf + k, sizeof buf - (size_t)k, "%s", dot ? dot : "");
    snprintf(out, n, "%s", buf);
}

static void capital(char *s)
{
    if (s[0] >= 'a' && s[0] <= 'z')
        s[0] = (char)(s[0] - 'a' + 'A');
}

/* ---- lines ---- */

static unsigned map_hash(uint32_t k)
{
    k ^= k >> 16;
    k *= 0x7feb352du;
    k ^= k >> 15;
    return k;
}

static Line *line_of(uint32_t id)
{
    if (!id || !H.map_cap)
        return NULL;
    unsigned m = (unsigned)H.map_cap - 1, h = map_hash(id) & m;
    while (H.map[h]) {
        Line *l = &H.lines[H.map[h] - 1];
        if (l->id == id)
            return l;
        h = (h + 1) & m;
    }
    return NULL;
}

static void map_put(uint32_t id, int index)
{
    if ((H.nlines + 1) * 2 > H.map_cap) {
        int cap = H.map_cap ? H.map_cap * 2 : 1024;
        uint32_t *map = calloc((size_t)cap, sizeof *map);
        if (!map)
            abort();
        free(H.map);
        H.map = map;
        H.map_cap = cap;
        for (int i = 0; i < H.nlines; i++)
            if (i != index)
                map_put(H.lines[i].id, i);
    }
    unsigned m = (unsigned)H.map_cap - 1, h = map_hash(id) & m;
    while (H.map[h])
        h = (h + 1) & m;
    H.map[h] = (uint32_t)index + 1;
}

static Line *new_line(uint32_t id, const Organism *o, int64_t t)
{
    H.lines = grow(H.lines, &H.cap_lines, H.nlines + 1, sizeof *H.lines);
    int index = H.nlines++;
    Line *l = &H.lines[index];
    memset(l, 0, sizeof *l);
    l->id = id;
    l->founder = (int)id;
    l->founded = l->last_seen = t;
    l->colour = (uint8_t)commonest_colour(o);
    /* 0.9's lines are the roots of the species' trees: named after the
     * organism the root began with */
    const Species *root = syn_family_get(id);
    if (root)
        l->founder = root->founder;
    map_put(id, index);
    return line_of(id);
}

/* The line an organism belongs to, founding it if new. */
static Line *line_for(const Organism *o, int64_t t)
{
    uint32_t key = syn_line_key(o);
    Line *l = line_of(key);
    if (!l && key)
        l = new_line(key, o, t);
    return l;
}

/* ---- events ---- */

static Event *add_event(int64_t t, EventKind kind, int key, const char *text)
{
    /* the same key a short while ago: that event climbs to here instead */
    if (key) {
        for (int i = H.nev - 1; i >= 0 && i >= H.nev - 64; i--) {
            if (H.ev[i].key != key)
                continue;
            if (t - H.ev[i].t <= MERGE_UNITS) {
                Event e = H.ev[i];
                memmove(&H.ev[i], &H.ev[i + 1], (size_t)(H.nev - i - 1) * sizeof *H.ev);
                e.t = t;
                snprintf(e.text, sizeof e.text, "%s", text);
                int at = H.nev - 1;
                while (at > 0 && H.ev[at - 1].t > t)
                    at--;
                memmove(&H.ev[at + 1], &H.ev[at], (size_t)(H.nev - 1 - at) * sizeof *H.ev);
                H.ev[at] = e;
                return &H.ev[at];
            }
            break;
        }
    }
    H.ev = grow(H.ev, &H.cap_ev, H.nev + 1, sizeof *H.ev);
    /* in time order (a record's time can be a unit back) */
    int at = H.nev;
    while (at > 0 && H.ev[at - 1].t > t)
        at--;
    memmove(&H.ev[at + 1], &H.ev[at], (size_t)(H.nev - at) * sizeof *H.ev);
    H.nev++;
    Event *e = &H.ev[at];
    memset(e, 0, sizeof *e);
    e->t = t;
    e->kind = kind;
    e->key = key;
    e->colour = -1;
    snprintf(e->text, sizeof e->text, "%s", text);
    return e;
}

static void event_gc(Event *e, GeneticCode *gc)
{
    keep_code(&e->gc, gc);
}

/* ---- epochs ---- */

static void champion_clear(Champion *c)
{
    memset(c, 0, sizeof *c);
    c->value = -1;
}

static void epoch_init(Epoch *a, int64_t k)
{
    memset(a, 0, sizeof *a);
    a->t0 = k * H.epoch_base;
    a->t1 = a->t0 + H.epoch_base;
    a->pop_min = INT_MAX;
    for (int c = 0; c < CH_COUNT; c++)
        champion_clear(&a->ch[c]);
}

/* The species with the most living at once in it: held by a pin. */
static void epoch_best(Epoch *a, uint32_t id, int n, int64_t t, bool hold)
{
    if (n <= a->best_n)
        return;
    if (hold && id != a->best) {
        syn_family_pin(id, 1);
        syn_family_pin(a->best, -1);
    }
    a->best = id;
    a->best_n = n;
    a->best_t = t;
}

/* src into dst. hold: dst owns its champions' codes (src's are given to it
 * or released); otherwise both only borrow. */
static void epoch_merge(Epoch *dst, Epoch *src, bool hold)
{
    if (src->t0 < dst->t0)
        dst->t0 = src->t0;
    if (src->t1 > dst->t1)
        dst->t1 = src->t1;
    dst->samples += src->samples;
    if (src->pop_min < dst->pop_min)
        dst->pop_min = src->pop_min;
    if (src->pop_max > dst->pop_max)
        dst->pop_max = src->pop_max;
    dst->pop_sum += src->pop_sum;
    dst->births += src->births;
    dst->deaths += src->deaths;
    dst->infections += src->infections;
    dst->extinctions += src->extinctions;
    for (int c = 0; c < HIST_COLOURS; c++)
        dst->seg[c] += src->seg[c];
    if (src->lead_n > dst->lead_n) {
        dst->lead_n = src->lead_n;
        dst->lead_line = src->lead_line;
    }
    /* src's pin goes with its best, or is let go */
    if (src->best_n > dst->best_n) {
        if (hold)
            syn_family_pin(dst->best, -1);
        dst->best = src->best;
        dst->best_n = src->best_n;
        dst->best_t = src->best_t;
    } else if (hold) {
        syn_family_pin(src->best, -1);
    }
    for (int k = 0; k < CH_COUNT; k++) {
        Champion *d = &dst->ch[k], *s = &src->ch[k];
        if (s->value > d->value) {
            if (hold)
                keep_code(&d->gc, NULL);
            *d = *s;
            if (hold)
                s->gc = NULL;
        } else if (hold) {
            keep_code(&s->gc, NULL);
        }
    }
}

/* Twice as long epochs: pairs merge. */
static void epochs_compact(void)
{
    int64_t k0 = H.epoch_k0 / 2;
    int n = (int)((H.epoch_k0 + H.nepochs - 1) / 2 - k0) + 1;
    Epoch *na = calloc((size_t)EPOCHS_CAP, sizeof *na);
    if (!na)
        abort();
    H.epoch_base *= 2;
    for (int i = 0; i < n; i++)
        epoch_init(&na[i], k0 + i);
    for (int i = 0; i < H.nepochs; i++)
        epoch_merge(&na[(H.epoch_k0 + i) / 2 - k0], &H.epochs[i], true);
    free(H.epochs);
    H.epochs = na;
    H.nepochs = n;
    H.epoch_k0 = k0;
}

static Epoch *epoch_at(int64_t t)
{
    if (!H.epochs) {
        H.epochs = calloc((size_t)EPOCHS_CAP, sizeof *H.epochs);
        if (!H.epochs)
            abort();
    }
    int64_t k = t / H.epoch_base;
    if (H.nepochs == 0) {
        H.epoch_k0 = k;
        epoch_init(&H.epochs[0], k);
        H.nepochs = 1;
    }
    while (k >= H.epoch_k0 + H.nepochs) {
        if (H.nepochs == EPOCHS_CAP) {
            epochs_compact();
            k = t / H.epoch_base;
            continue;
        }
        epoch_init(&H.epochs[H.nepochs], H.epoch_k0 + H.nepochs);
        H.nepochs++;
    }
    if (k < H.epoch_k0)
        k = H.epoch_k0;
    return &H.epochs[k - H.epoch_k0];
}

static void consider(Champion *c, double v, const Organism *o, int64_t t, bool died, bool hold)
{
    if (v <= c->value)
        return;
    c->value = v;
    c->id = o->id;
    c->generation = o->generation;
    c->line = syn_line_key(o);
    c->t = t;
    c->died = died;
    c->colour = (uint8_t)commonest_colour(o);
    if (hold)
        keep_code(&c->gc, o->gc);
    else
        c->gc = o->gc;
}

static void observe(Champion ch[CH_COUNT], const Organism *o, int64_t t, bool died, bool hold)
{
    consider(&ch[CH_CHILDREN], o->total_children, o, t, died, hold);
    consider(&ch[CH_KILLS], o->total_kills, o, t, died, hold);
    consider(&ch[CH_INFECTED], o->total_infected, o, t, died, hold);
    consider(&ch[CH_GENERATION], o->generation, o, t, died, hold);
    /* a corpse's energy and mass are what is left of it: those count while alive */
    if (!died) {
        consider(&ch[CH_RICHEST], o->energy, o, t, died, hold);
        consider(&ch[CH_HEAVIEST], o->mass, o, t, died, hold);
    }
}

/* ---- samples ---- */

static void samples_compact(void)
{
    int m = H.n / 2;
    for (int i = 0; i < m; i++) {
        const HistSample *a = &H.s[2 * i], *b = &H.s[2 * i + 1];
        /* the later one's time and lines; everything else averaged, the
         * maximum generation the larger */
        HistSample r = *b;
#define AVG(f) r.f = (a->f + b->f) * 0.5f
        AVG(pop);
        AVG(corpses);
        AVG(births);
        AVG(deaths);
        AVG(infections);
        AVG(o2);
        AVG(co2);
        AVG(ch4);
        for (int c = 0; c < HIST_COLOURS; c++)
            AVG(seg[c]);
        AVG(gen_mean);
        AVG(energy_mean);
        AVG(mass_total);
        AVG(size_mean);
        AVG(age_mean);
        AVG(lines);
        AVG(kills);
        AVG(bites);
        AVG(flat);
        AVG(level);
        AVG(leaves_depth);
        AVG(hunters_depth);
#undef AVG
        r.gen_max = fmaxf(a->gen_max, b->gen_max);
        H.s[i] = r;
    }
    H.n = m;
    H.stride *= 2;
}

/* Once a time unit: the world read, the epochs and detectors fed, and every
 * stride units a sample kept. */
static void unit(World *w)
{
    int64_t t = w->stats.time;
    WorldStats *st = &w->stats;
    int pop = 0, corpses = 0;
    double seg[HIST_COLOURS] = {0}, gen_sum = 0, energy = 0, mass = 0, size = 0, age = 0;
    double flat = 0, level = 0, leaves = 0, leaves_at = 0, hunters = 0, hunters_at = 0;
    int gen_max = 0;
    for (int i = 0; i < H.nlines; i++)
        H.lines[i].alive = 0;
    syn_family_unit(w, t);
    Epoch *ep = epoch_at(t);
    int top_n;
    uint32_t top = syn_family_top(&top_n);
    if (top)
        epoch_best(ep, top, top_n, t, true);
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (o->removed)
            continue;
        if (!o->alive) {
            corpses++;
            continue;
        }
        pop++;
        for (int s = 0; s < o->segments; s++) {
            int slot = o->seg_color[s];
            if (slot >= 0 && slot < HIST_COLOURS)
                seg[slot]++;
        }
        int gen = o->generation;
        gen_sum += gen;
        if (gen > gen_max)
            gen_max = gen;
        energy += o->energy;
        mass += o->mass;
        size += o->segments;
        age += o->age >> 8;
        flat += gc_is_flat(o->gc);
        level += org_levelness(o);
        double below = w->depth > 0 ? (w->depth - o->pos.z) / w->depth : 0;
        for (int s = 0; s < o->segments; s++) {
            int c = o->seg_color[s];
            if (c == COL_GREEN) {
                leaves += o->m[s];
                leaves_at += below * o->m[s];
            } else if (c == COL_RED || c == COL_GRAY || c == COL_WHITE) {
                hunters += o->m[s];
                hunters_at += below * o->m[s];
            }
        }
        Line *l = line_for(o, t);
        if (l)
            l->alive++;
        observe(ep->ch, o, t, false, true);
    }

    /* the lines: who leads, who is new, who is gone */
    HistSample smp;
    memset(&smp, 0, sizeof smp);
    int living = 0;
    for (int i = 0; i < H.nlines; i++) {
        Line *l = &H.lines[i];
        if (l->alive > 0) {
            living++;
            l->last_seen = t;
            if (l->alive > l->peak) {
                l->peak = l->alive;
                l->peak_t = t;
            }
            if (l->peak >= 10 && l->peak * 20 >= pop)
                l->notable = true;
            /* the top lines, biggest first */
            int j = HIST_TOP - 1;
            if (l->alive > smp.top_n[j]) {
                while (j > 0 && l->alive > smp.top_n[j - 1]) {
                    smp.top_n[j] = smp.top_n[j - 1];
                    smp.top_line[j] = smp.top_line[j - 1];
                    j--;
                }
                smp.top_n[j] = (float)l->alive;
                smp.top_line[j] = l->id;
            }
        } else if (!l->extinct && l->peak > 0) {
            l->extinct = true;
            if (l->notable) {
                char text[96], pk[24], pt[24], lasted[24];
                snprintf(text, sizeof text, "The line of #%d dies out", l->founder);
                Event *e = add_event(t, EV_EXTINCTION, 0, text);
                num(pk, sizeof pk, l->peak);
                num(pt, sizeof pt, (double)l->peak_t);
                num(lasted, sizeof lasted, (double)(t - l->founded));
                snprintf(e->detail, sizeof e->detail, "after %s units; at its peak %s living (t %s)",
                         lasted, pk, pt);
                e->line = l->id;
                e->colour = l->colour;
            }
        }
    }
    if (smp.top_n[0] > ep->lead_n) {
        ep->lead_n = (int)smp.top_n[0];
        ep->lead_line = smp.top_line[0];
    }
    uint32_t lead = smp.top_n[0] >= 5 ? smp.top_line[0] : 0;
    if (lead && lead != H.lead_line) {
        if (lead == H.line_cand) {
            if (++H.line_hold >= HOLD) {
                H.lead_line = lead;
                Line *l = line_of(lead);
                if (l) {
                    l->led = l->notable = true;
                    char text[96], n[32], all[32], ft[32];
                    snprintf(text, sizeof text, "The line of #%d takes the lead", l->founder);
                    Event *e = add_event(t, EV_LINE, 0, text);
                    num(n, sizeof n, l->alive);
                    num(all, sizeof all, pop);
                    num(ft, sizeof ft, (double)l->founded);
                    snprintf(e->detail, sizeof e->detail, "%s living of %s; founded at t %s", n, all, ft);
                    e->line = l->id;
                    e->colour = l->colour;
                }
            }
        } else {
            H.line_cand = lead;
            H.line_hold = 1;
        }
    }

    /* the colours */
    int col = -1;
    for (int c = 0; c < HIST_COLOURS; c++) {
        ep->seg[c] += seg[c];
        if (seg[c] > 0) {
            if (seg[c] > H.col_peak[c])
                H.col_peak[c] = (int)seg[c];
            if (H.col_gone[c]) {
                H.col_gone[c] = false;
                char text[96];
                snprintf(text, sizeof text, "%s returns", syn_col_name(c));
                capital(text);
                Event *e = add_event(t, EV_COLOUR, 0, text);
                e->colour = c;
                snprintf(e->detail, sizeof e->detail, "%.0f segments", seg[c]);
            }
            /* a gene colour that was ever common enough to notice */
            H.col_seen[c] = gene_colour(c) && H.col_peak[c] >= 20;
            H.col_zero[c] = 0;
            if (gene_colour(c) && (col < 0 || seg[c] > seg[col]))
                col = c;
        } else if (H.col_seen[c] && !H.col_gone[c] && ++H.col_zero[c] >= 2) {
            H.col_gone[c] = true;
            char text[96];
            snprintf(text, sizeof text, "The last %s segment is gone", syn_col_name(c));
            Event *e = add_event(t, EV_EXTINCTION, 0, text);
            e->colour = c;
            snprintf(e->detail, sizeof e->detail, "no living organism has %s", syn_col_name(c));
        }
    }
    if (col >= 0 && col != H.lead_col) {
        if (col == H.col_cand) {
            if (++H.col_hold >= HOLD) {
                H.lead_col = col;
                char text[96];
                snprintf(text, sizeof text, "%s takes the lead", syn_col_name(col));
                capital(text);
                Event *e = add_event(t, EV_COLOUR, 0, text);
                e->colour = col;
                double all = 0;
                for (int c = 0; c < HIST_COLOURS; c++)
                    all += seg[c];
                snprintf(e->detail, sizeof e->detail, "%.0f%% of the living segments",
                         all > 0 ? 100 * seg[col] / all : 0);
            }
        } else {
            H.col_cand = col;
            H.col_hold = 1;
        }
    }

    /* the world's own counters */
    int births = (int)(st->birth_sum - H.ubirth0), deaths = (int)(st->death_sum - H.udeath0);
    int infections = (int)(st->infections_sum - H.uinf0);
    H.ubirth0 = st->birth_sum;
    H.udeath0 = st->death_sum;
    H.uinf0 = st->infections_sum;
    ep->samples++;
    ep->pop_sum += pop;
    if (pop < ep->pop_min)
        ep->pop_min = pop;
    if (pop > ep->pop_max)
        ep->pop_max = pop;
    ep->births += births;
    ep->deaths += deaths;
    ep->infections += infections;
    if (st->mass_extinctions > H.extinctions) {
        H.extinctions = st->mass_extinctions;
        ep->extinctions++;
        Event *e = add_event(t, EV_EXTINCTION, 0, "Mass extinction");
        char dn[24], pn[24];
        num(dn, sizeof dn, deaths);
        num(pn, sizeof pn, pop);
        snprintf(e->detail, sizeof e->detail, "%s deaths in the last unit, %s living", dn, pn);
    }
    /* the records as the world keeps them, at the times it gives */
    if (st->max_population > H.pop_record) {
        H.pop_record = st->max_population;
        char text[96], n[32];
        num(n, sizeof n, st->max_population);
        snprintf(text, sizeof text, "Population record: %s", n);
        Event *e = add_event(st->max_population_time, EV_RECORD, 'P', text);
        e->value = st->max_population;
        char ln[24];
        num(ln, sizeof ln, living);
        snprintf(e->detail, sizeof e->detail, "the most alive at once so far; %s lines living now",
                 ln);
    }
    struct {
        int n;
        int64_t t;
        int *seen;
        GeneticCode *gc;
        int key;
        const char *what;
    } rec[] = {{st->most_children_n, st->most_children_time, &H.rec_children, st->most_children, 'C',
                "children"},
               {st->most_kills_n, st->most_kills_time, &H.rec_kills, st->most_kills, 'K', "victims"},
               {st->most_infections_n, st->most_infections_time, &H.rec_infected, st->most_infections,
                'I', "infections"}};
    for (int i = 0; i < 3; i++) {
        if (rec[i].n <= *rec[i].seen)
            continue;
        *rec[i].seen = rec[i].n;
        char text[96], rn[24];
        num(rn, sizeof rn, rec[i].n);
        snprintf(text, sizeof text, "Record: %s %s", rn, rec[i].what);
        Event *e = add_event(rec[i].t, EV_RECORD, rec[i].key, text);
        e->value = rec[i].n;
        snprintf(e->detail, sizeof e->detail, "the most any organism has had");
        event_gc(e, rec[i].gc);
    }
    double gas = w->o2 + w->co2;
    struct {
        double v;
        bool *low;
        const char *name;
    } gases[] = {{w->co2, &H.co2_low, "CO2"}, {w->o2, &H.o2_low, "O2"}};
    for (int i = 0; i < 2 && gas > 0; i++) {
        char text[96], v[32];
        num(v, sizeof v, gases[i].v);
        if (!*gases[i].low && gases[i].v < 0.02 * gas) {
            *gases[i].low = true;
            snprintf(text, sizeof text, "%s runs out", gases[i].name);
            Event *e = add_event(t, EV_ATMOSPHERE, 0, text);
            snprintf(e->detail, sizeof e->detail, "%s left, under 2%% of the air", v);
        } else if (*gases[i].low && gases[i].v > 0.05 * gas) {
            *gases[i].low = false;
            snprintf(text, sizeof text, "%s recovers", gases[i].name);
            Event *e = add_event(t, EV_ATMOSPHERE, 0, text);
            snprintf(e->detail, sizeof e->detail, "%s, over 5%% of the air", v);
        }
    }

    /* a sample every stride units */
    if (H.n > 0 && t - H.commit_t < H.stride)
        return;
    if (H.n == HIST_CAP)
        samples_compact();
    double span = H.n > 0 && t > H.commit_t ? (double)(t - H.commit_t) : 1;
    smp.t = t;
    smp.pop = (float)pop;
    smp.corpses = (float)corpses;
    smp.births = (float)((st->birth_sum - H.birth0) / span);
    smp.deaths = (float)((st->death_sum - H.death0) / span);
    smp.infections = (float)((st->infections_sum - H.inf0) / span);
    smp.o2 = (float)w->o2;
    smp.co2 = (float)w->co2;
    smp.ch4 = (float)w->ch4;
    for (int c = 0; c < HIST_COLOURS; c++)
        smp.seg[c] = (float)seg[c];
    smp.gen_mean = pop ? (float)(gen_sum / pop) : 0;
    smp.gen_max = (float)gen_max;
    smp.energy_mean = pop ? (float)(energy / pop) : 0;
    smp.mass_total = (float)mass;
    smp.size_mean = pop ? (float)(size / pop) : 0;
    smp.age_mean = pop ? (float)(age / pop) : 0;
    smp.lines = (float)living;
    smp.kills = (float)((w->counts.kills - H.kill0) / span);
    smp.bites = (float)((w->counts.bites - H.bite0) / span);
    smp.flat = pop ? (float)(flat / pop) : 0;
    smp.level = pop ? (float)(level / pop) : 0;
    smp.leaves_depth = leaves > 0 ? (float)(leaves_at / leaves) : 0;
    smp.hunters_depth = hunters > 0 ? (float)(hunters_at / hunters) : 0;
    H.kill0 = w->counts.kills;
    H.bite0 = w->counts.bites;
    H.s[H.n++] = smp;
    H.commit_t = t;
    H.birth0 = st->birth_sum;
    H.death0 = st->death_sum;
    H.inf0 = st->infections_sum;
}

/* ---- the public side ---- */


static void release_all(void)
{
    for (int i = 0; i < H.nepochs; i++)
        for (int k = 0; k < CH_COUNT; k++)
            keep_code(&H.epochs[i].ch[k].gc, NULL);
    for (int i = 0; i < H.nev; i++)
        keep_code(&H.ev[i].gc, NULL);
    H.nepochs = 0;
    H.nev = 0;
    H.nlines = 0;
    if (H.map)
        memset(H.map, 0, (size_t)H.map_cap * sizeof *H.map);
    H.n = 0;
}

static void removed(void *ud, Organism *o)
{
    (void)ud;
    syn_history_removed(o->world, o);
}

void syn_history_reset(World *w, bool loaded)
{
    release_all();
    if (!H.s) {
        H.s = malloc((size_t)HIST_CAP * sizeof *H.s);
        if (!H.s)
            abort();
    }
    WorldStats *st = &w->stats;
    H.stride = 1;
    H.epoch_base = EPOCH_BASE0;
    H.t_start = H.seen_t = st->time;
    H.birth0 = H.ubirth0 = st->birth_sum;
    H.death0 = H.udeath0 = st->death_sum;
    H.inf0 = H.uinf0 = st->infections_sum;
    H.kill0 = w->counts.kills;
    H.bite0 = w->counts.bites;
    H.extinctions = st->mass_extinctions;
    H.lead_col = H.col_cand = -1;
    H.col_hold = 0;
    memset(H.col_zero, 0, sizeof H.col_zero);
    memset(H.col_peak, 0, sizeof H.col_peak);
    memset(H.col_seen, 0, sizeof H.col_seen);
    memset(H.col_gone, 0, sizeof H.col_gone);
    H.lead_line = H.line_cand = 0;
    H.line_hold = 0;
    H.pop_record = loaded ? st->max_population : 0;
    H.rec_children = st->most_children_n;
    H.rec_kills = st->most_kills_n;
    H.rec_infected = st->most_infections_n;
    H.co2_low = H.o2_low = false;
    H.started = true;
    /* what leaves the world is seen as it goes, for the champions */
    w->on_remove = removed;
    w->ud = NULL;
    syn_family_reset(w);
    /* every line there is from the start (Color Mod's come from its clades) */
    for (int i = 0; i < w->norgs; i++)
        if (!w->orgs[i]->removed && w->orgs[i]->alive)
            line_for(w->orgs[i], st->time);

    char text[96], n[32], nl[24];
    num(n, sizeof n, w->population);
    num(nl, sizeof nl, H.nlines);
    if (loaded) {
        char tt[32];
        num(tt, sizeof tt, (double)st->time);
        snprintf(text, sizeof text, "World opened at t %s", tt);
        Event *e = add_event(st->time, EV_START, 0, text);
        snprintf(e->detail, sizeof e->detail,
                 "%s living, %s lines from here; what happened before is in its records", n,
                 nl);
    } else {
        snprintf(text, sizeof text, "Genesis: %s organisms", n);
        Event *e = add_event(st->time, EV_START, 0, text);
        snprintf(e->detail, sizeof e->detail, "%d x %d x %d of water, each organism founding a line",
                 w->width, w->height, w->depth);
    }
    unit(w);
}

void syn_history_frame(World *w)
{
    if (!H.started)
        syn_history_reset(w, true);
    syn_family_frame(w);
    if (w->stats.time != H.seen_t) {
        H.seen_t = w->stats.time;
        unit(w);
        if (H.on_unit)
            H.on_unit(w);
    }
}

void syn_history_on_unit(void (*fn)(const World *w))
{
    H.on_unit = fn;
}

void syn_history_removed(const World *w, const Organism *o)
{
    if (!H.started || !H.epochs)
        return;
    observe(epoch_at(w->stats.time)->ch, o, w->stats.time, true, true);
}

void syn_history_user(const World *w, int key, double amount, const char *text)
{
    if (!H.started)
        return;
    int64_t t = w->stats.time;
    /* repeats within a few units are one event */
    for (int i = H.nev - 1; key && i >= 0 && i >= H.nev - 8; i--) {
        Event *e = &H.ev[i];
        if (e->key == key && e->kind == EV_USER && t - e->t <= 4) {
            e->value += amount;
            e->count++;
            char v[32];
            num(v, sizeof v, e->value);
            if (amount != 0)
                snprintf(e->detail, sizeof e->detail, "%s in all, %d times", v, e->count);
            else
                snprintf(e->detail, sizeof e->detail, "%d times", e->count);
            return;
        }
    }
    Event *e = add_event(t, EV_USER, 0, text);
    e->key = key;
    e->count = 1;
    e->value = amount;
    if (amount != 0) {
        char v[32];
        num(v, sizeof v, amount);
        snprintf(e->detail, sizeof e->detail, "%s", v);
    }
}

void syn_history_free(void)
{
    release_all();
    syn_family_free();
    free(H.s);
    free(H.epochs);
    free(H.ev);
    free(H.lines);
    free(H.map);
    H.map = NULL;
    H.map_cap = 0;
    H.s = NULL;
    H.epochs = NULL;
    H.ev = NULL;
    H.lines = NULL;
    H.cap_ev = H.cap_lines = 0;
    H.started = false;
}

const History *syn_history(void)
{
    History *v = &H.view;
    v->s = H.s;
    v->n = H.n;
    v->stride = H.stride;
    v->epochs = H.epochs;
    v->nepochs = H.nepochs;
    v->epoch_base = H.epoch_base ? H.epoch_base : EPOCH_BASE0;
    v->ev = H.ev;
    v->nev = H.nev;
    v->t_start = H.t_start;
    v->t_now = H.seen_t;
    return v;
}

const Line *syn_history_line(uint32_t id)
{
    return line_of(id);
}

void syn_history_alive_best(const World *w, Champion out[CH_COUNT])
{
    for (int k = 0; k < CH_COUNT; k++)
        champion_clear(&out[k]);
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (!o->removed && o->alive)
            observe(out, o, w->stats.time, false, false);
    }
}

bool syn_history_merge(int64_t t0, int64_t t1, Epoch *out)
{
    memset(out, 0, sizeof *out);
    out->t0 = t0;
    out->t1 = t1;
    out->pop_min = INT_MAX;
    for (int k = 0; k < CH_COUNT; k++)
        champion_clear(&out->ch[k]);
    bool any = false;
    for (int i = 0; i < H.nepochs; i++) {
        Epoch *a = &H.epochs[i];
        if (a->t1 <= t0 || a->t0 >= t1 || a->samples == 0)
            continue;
        epoch_merge(out, a, false);
        any = true;
    }
    out->t0 = t0;
    out->t1 = t1;
    return any;
}

const char *syn_champion_name(ChampionKind k)
{
    static const char *const names[CH_COUNT] = {"CHILDREN", "VICTIMS",    "INFECTED",
                                                "RICHEST",  "GENERATION", "HEAVIEST"};
    return k >= 0 && k < CH_COUNT ? names[k] : "";
}

void syn_champion_value(ChampionKind k, double v, char *out, size_t n)
{
    if (v < 0) {
        snprintf(out, n, "-");
        return;
    }
    (void)k;
    num(out, n, v);
}

/* ---- saved with the world (syn/sidecar.h) ----
 *
 * Every field, little-endian, floats by their bits; the codes the
 * champions and events hold once each, as .bgg text, and referred to by
 * number. The world's statistics are saved in the world file, and the
 * totals and detectors here are read against them, so a world saved and
 * opened again records on as if it had never stopped. */

#define HSAVE_MAGIC "SYNHIST1"
#define HSAVE_FORMAT 2 /* 1: before methane */
#define HSAVE_SIM 3 /* this program's */

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

static void putf(Out *b, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put32(b, u);
}

static void putd(Out *b, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    put64(b, u);
}

static void putstr(Out *b, const char *s)
{
    size_t n = strlen(s);
    put32(b, (uint32_t)n);
    put(b, s, n);
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

static float getf(In *b)
{
    uint32_t u = get32(b);
    float v;
    memcpy(&v, &u, 4);
    return v;
}

static double getd(In *b)
{
    uint64_t u = get64(b);
    double v;
    memcpy(&v, &u, 8);
    return v;
}

static void getstr(In *b, char *out, size_t cap)
{
    uint32_t n = get32(b);
    const uint8_t *s = n < cap ? get(b, n) : NULL;
    if (!s) {
        b->bad = true;
        out[0] = '\0';
        return;
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

/* The codes held, each once: pointer -> number + 1 */
typedef struct {
    GeneticCode **key;
    int *val;
    int cap;
    GeneticCode **list;
    int n;
} CodeSet;

static unsigned ptr_hash(const void *p)
{
    uint64_t k = (uint64_t)(uintptr_t)p;
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33;
    return (unsigned)k;
}

static int code_ref(CodeSet *c, GeneticCode *gc)
{
    if (!gc)
        return 0;
    unsigned m = (unsigned)c->cap - 1, h = ptr_hash(gc) & m;
    while (c->key[h]) {
        if (c->key[h] == gc)
            return c->val[h];
        h = (h + 1) & m;
    }
    c->key[h] = gc;
    c->list[c->n] = gc;
    c->val[h] = ++c->n;
    return c->val[h];
}

static void put_champion(Out *b, CodeSet *codes, const Champion *c)
{
    putd(b, c->value);
    put32(b, (uint32_t)c->id);
    put32(b, (uint32_t)c->generation);
    put32(b, c->line);
    put64(b, (uint64_t)c->t);
    uint8_t f[2] = {c->died, c->colour};
    put(b, f, 2);
    put32(b, (uint32_t)code_ref(codes, c->gc));
}

size_t syn_history_save(uint8_t **out)
{
    *out = NULL;
    if (!H.started)
        return 0;
    int holders = H.nepochs * CH_COUNT + H.nev;
    CodeSet codes = {0};
    codes.cap = 64;
    while (codes.cap < 2 * (holders + 1))
        codes.cap *= 2;
    codes.key = calloc((size_t)codes.cap, sizeof *codes.key);
    codes.val = calloc((size_t)codes.cap, sizeof *codes.val);
    codes.list = calloc((size_t)holders + 1, sizeof *codes.list);
    if (!codes.key || !codes.val || !codes.list)
        abort();
    for (int i = 0; i < H.nepochs; i++)
        for (int k = 0; k < CH_COUNT; k++)
            code_ref(&codes, H.epochs[i].ch[k].gc);
    for (int i = 0; i < H.nev; i++)
        code_ref(&codes, H.ev[i].gc);

    Out b = {0};
    put(&b, HSAVE_MAGIC, 8);
    put32(&b, HSAVE_FORMAT);
    put32(&b, HSAVE_SIM);
    put32(&b, HIST_CAP);
    put32(&b, EPOCHS_CAP);
    put32(&b, HIST_COLOURS);
    put32(&b, HIST_TOP);
    put32(&b, CH_COUNT);

    /* each code as its .bgg */
    put32(&b, (uint32_t)codes.n);
    bool copied = true;
    for (int i = 0; i < codes.n && copied; i++) {
        size_t len = 0;
        uint8_t *xml = bgg_bytes(codes.list[i], &len);
        copied = xml != NULL;
        put32(&b, (uint32_t)len);
        if (xml)
            put(&b, xml, len);
        free(xml);
    }
    if (!copied) {
        free(b.p);
        free(codes.key);
        free(codes.val);
        free(codes.list);
        return 0;
    }

    put32(&b, (uint32_t)H.n);
    put32(&b, (uint32_t)H.stride);
    put64(&b, (uint64_t)H.commit_t);
    int64_t totals[8] = {H.birth0, H.death0, H.inf0, H.ubirth0, H.udeath0, H.uinf0, H.kill0, H.bite0};
    for (int i = 0; i < 8; i++)
        put64(&b, (uint64_t)totals[i]);
    put32(&b, (uint32_t)H.nepochs);
    put32(&b, (uint32_t)H.epoch_base);
    put64(&b, (uint64_t)H.epoch_k0);
    put32(&b, (uint32_t)H.nev);
    put32(&b, (uint32_t)H.nlines);
    put64(&b, (uint64_t)H.t_start);
    put64(&b, (uint64_t)H.seen_t);
    put32(&b, (uint32_t)H.extinctions);
    put32(&b, (uint32_t)H.lead_col);
    put32(&b, (uint32_t)H.col_cand);
    put32(&b, (uint32_t)H.col_hold);
    for (int c = 0; c < HIST_COLOURS; c++) {
        put32(&b, (uint32_t)H.col_zero[c]);
        put32(&b, (uint32_t)H.col_peak[c]);
        uint8_t f[2] = {H.col_seen[c], H.col_gone[c]};
        put(&b, f, 2);
    }
    put32(&b, H.lead_line);
    put32(&b, H.line_cand);
    put32(&b, (uint32_t)H.line_hold);
    put32(&b, (uint32_t)H.pop_record);
    put32(&b, (uint32_t)H.rec_children);
    put32(&b, (uint32_t)H.rec_kills);
    put32(&b, (uint32_t)H.rec_infected);
    uint8_t lows[2] = {H.co2_low, H.o2_low};
    put(&b, lows, 2);

    for (int i = 0; i < H.n; i++) {
        const HistSample *s = &H.s[i];
        put64(&b, (uint64_t)s->t);
        float f[] = {s->pop, s->corpses, s->births, s->deaths, s->infections, s->o2, s->co2,
                     s->gen_mean, s->gen_max, s->energy_mean, s->mass_total, s->size_mean,
                     s->age_mean, s->lines, s->kills, s->bites, s->flat, s->level,
                     s->leaves_depth, s->hunters_depth, s->ch4};
        for (size_t k = 0; k < sizeof f / sizeof f[0]; k++)
            putf(&b, f[k]);
        for (int c = 0; c < HIST_COLOURS; c++)
            putf(&b, s->seg[c]);
        for (int k = 0; k < HIST_TOP; k++) {
            put32(&b, s->top_line[k]);
            putf(&b, s->top_n[k]);
        }
    }
    for (int i = 0; i < H.nepochs; i++) {
        const Epoch *a = &H.epochs[i];
        put64(&b, (uint64_t)a->t0);
        put64(&b, (uint64_t)a->t1);
        int32_t v[] = {a->samples, a->pop_min, a->pop_max, a->births,
                       a->deaths,  a->infections, a->extinctions, a->lead_n, a->best_n};
        for (size_t k = 0; k < sizeof v / sizeof v[0]; k++)
            put32(&b, (uint32_t)v[k]);
        putd(&b, a->pop_sum);
        for (int c = 0; c < HIST_COLOURS; c++)
            putd(&b, a->seg[c]);
        put32(&b, a->lead_line);
        put32(&b, a->best);
        put64(&b, (uint64_t)a->best_t);
        for (int k = 0; k < CH_COUNT; k++)
            put_champion(&b, &codes, &a->ch[k]);
    }
    for (int i = 0; i < H.nev; i++) {
        const Event *e = &H.ev[i];
        put64(&b, (uint64_t)e->t);
        put32(&b, (uint32_t)e->kind);
        put32(&b, (uint32_t)e->colour);
        put32(&b, e->line);
        put32(&b, (uint32_t)e->key);
        put32(&b, (uint32_t)e->count);
        putd(&b, e->value);
        putstr(&b, e->text);
        putstr(&b, e->detail);
        put32(&b, (uint32_t)code_ref(&codes, e->gc));
    }
    for (int i = 0; i < H.nlines; i++) {
        const Line *l = &H.lines[i];
        put32(&b, l->id);
        put32(&b, (uint32_t)l->founder);
        put64(&b, (uint64_t)l->founded);
        put64(&b, (uint64_t)l->peak_t);
        put64(&b, (uint64_t)l->last_seen);
        put32(&b, (uint32_t)l->peak);
        put32(&b, (uint32_t)l->alive);
        uint8_t f[4] = {l->led, l->notable, l->extinct, l->colour};
        put(&b, f, 4);
    }
    free(codes.key);
    free(codes.val);
    free(codes.list);
    *out = b.p;
    return b.n;
}

static int get_ref(In *b, int ncodes)
{
    uint32_t r = get32(b);
    if (r > (uint32_t)ncodes)
        b->bad = true;
    return b->bad ? 0 : (int)r;
}

static void get_champion(In *b, int ncodes, Champion *c, int *ref)
{
    memset(c, 0, sizeof *c);
    c->value = getd(b);
    c->id = (int)get32(b);
    c->generation = (int)get32(b);
    c->line = get32(b);
    c->t = (int64_t)get64(b);
    const uint8_t *f = get(b, 2);
    if (f) {
        c->died = f[0] != 0;
        c->colour = f[1];
        if (f[1] >= SYN_NCOL)
            b->bad = true;
    }
    *ref = get_ref(b, ncodes);
}

/* The parsed codes: given up (0.9: released) */
static void drop_codes(GeneticCode **codes, int n)
{
    for (int i = 0; i < n; i++)
        keep_code(&codes[i], NULL);
    free(codes);
}

bool syn_history_load(World *w, const uint8_t *in, size_t n, const uint8_t *family,
                      size_t family_n, bool note)
{
    In b = {in, n, 0, false};
    const uint8_t *magic = get(&b, 8);
    uint32_t format = magic ? get32(&b) : 0;
    if (!magic || memcmp(magic, HSAVE_MAGIC, 8) != 0 || format < 1 || format > HSAVE_FORMAT ||
        get32(&b) != HSAVE_SIM || get32(&b) != HIST_CAP || get32(&b) != EPOCHS_CAP)
        return false;
    /* the colours it kept: these, or the seven before purple (purple was
     * then none) */
    int ncol = (int)get32(&b);
    if ((ncol != HIST_COLOURS && ncol != HIST_COLOURS - 1) || get32(&b) != HIST_TOP || get32(&b) != CH_COUNT)
        return false;

    /* everything into these first; the history and the tree change only
     * once all of it has read well */
    uint32_t ncodes = get32(&b);
    if (b.bad || ncodes > (n - b.at) / 4)
        return false;
    GeneticCode **codes = calloc((size_t)ncodes + 1, sizeof *codes);
    if (!codes)
        abort();
    for (uint32_t i = 0; i < ncodes && !b.bad; i++) {
        uint32_t len = get32(&b);
        const uint8_t *xml = get(&b, len);
        char err[256];
        if (xml)
            codes[i] = bgg_parse(xml, len, err, (int)sizeof err);
        if (!codes[i])
            b.bad = true;
    }

    int hn = (int)get32(&b), stride = (int)get32(&b);
    int64_t commit_t = (int64_t)get64(&b);
    int64_t totals[8];
    for (int i = 0; i < 8; i++)
        totals[i] = (int64_t)get64(&b);
    int nepochs = (int)get32(&b), epoch_base = (int)get32(&b);
    int64_t epoch_k0 = (int64_t)get64(&b);
    int nev = (int)get32(&b), nlines = (int)get32(&b);
    int64_t t_start = (int64_t)get64(&b), seen_t = (int64_t)get64(&b);
    int extinctions = (int)get32(&b), lead_col = (int)get32(&b), col_cand = (int)get32(&b);
    int col_hold = (int)get32(&b);
    int col_zero[HIST_COLOURS] = {0}, col_peak[HIST_COLOURS] = {0};
    bool col_seen[HIST_COLOURS] = {0}, col_gone[HIST_COLOURS] = {0};
    for (int c = 0; c < ncol; c++) {
        col_zero[c] = (int)get32(&b);
        col_peak[c] = (int)get32(&b);
        const uint8_t *f = get(&b, 2);
        col_seen[c] = f && f[0];
        col_gone[c] = f && f[1];
    }
    uint32_t lead_line = get32(&b), line_cand = get32(&b);
    int line_hold = (int)get32(&b), pop_record = (int)get32(&b);
    int rec_children = (int)get32(&b), rec_kills = (int)get32(&b), rec_infected = (int)get32(&b);
    const uint8_t *lows = get(&b, 2);
    if (b.bad || hn < 0 || hn > HIST_CAP || stride < 1 || nepochs < 0 || nepochs > EPOCHS_CAP ||
        epoch_base < 1 || nev < 0 || nlines < 0 || (size_t)nev > n / 32 ||
        (size_t)nlines > n / 32 || lead_col < -1 || lead_col >= HIST_COLOURS || col_cand < -1 ||
        col_cand >= HIST_COLOURS) {
        drop_codes(codes, (int)ncodes);
        return false;
    }

    HistSample *s = calloc((size_t)hn + 1, sizeof *s);
    Epoch *ep = calloc((size_t)nepochs + 1, sizeof *ep);
    int *ep_ref = calloc((size_t)nepochs * CH_COUNT + 1, sizeof *ep_ref);
    Event *ev = calloc((size_t)nev + 1, sizeof *ev);
    int *ev_ref = calloc((size_t)nev + 1, sizeof *ev_ref);
    Line *ln = calloc((size_t)nlines + 1, sizeof *ln);
    if (!s || !ep || !ep_ref || !ev || !ev_ref || !ln)
        abort();
    for (int i = 0; i < hn && !b.bad; i++) {
        HistSample *x = &s[i];
        x->t = (int64_t)get64(&b);
        float *f[] = {&x->pop, &x->corpses, &x->births, &x->deaths, &x->infections, &x->o2, &x->co2,
                      &x->gen_mean, &x->gen_max, &x->energy_mean, &x->mass_total, &x->size_mean,
                      &x->age_mean, &x->lines, &x->kills, &x->bites, &x->flat, &x->level,
                      &x->leaves_depth, &x->hunters_depth, &x->ch4};
        size_t nf = sizeof f / sizeof f[0] - (format < 2); /* no methane before format 2 */
        for (size_t k = 0; k < nf; k++)
            *f[k] = getf(&b);
        for (int c = 0; c < ncol; c++)
            x->seg[c] = getf(&b);
        for (int k = 0; k < HIST_TOP; k++) {
            x->top_line[k] = get32(&b);
            x->top_n[k] = getf(&b);
        }
    }
    for (int i = 0; i < nepochs && !b.bad; i++) {
        Epoch *a = &ep[i];
        a->t0 = (int64_t)get64(&b);
        a->t1 = (int64_t)get64(&b);
        int *v[] = {&a->samples, &a->pop_min, &a->pop_max, &a->births,
                    &a->deaths,  &a->infections, &a->extinctions, &a->lead_n, &a->best_n};
        for (size_t k = 0; k < sizeof v / sizeof v[0]; k++)
            *v[k] = (int)get32(&b);
        a->pop_sum = getd(&b);
        for (int c = 0; c < ncol; c++)
            a->seg[c] = getd(&b);
        a->lead_line = get32(&b);
        a->best = get32(&b);
        a->best_t = (int64_t)get64(&b);
        for (int k = 0; k < CH_COUNT; k++)
            get_champion(&b, (int)ncodes, &a->ch[k], &ep_ref[i * CH_COUNT + k]);
    }
    for (int i = 0; i < nev && !b.bad; i++) {
        Event *e = &ev[i];
        e->t = (int64_t)get64(&b);
        uint32_t kind = get32(&b);
        e->kind = kind < EV_KINDS ? (EventKind)kind : EV_START;
        e->colour = (int)get32(&b);
        e->line = get32(&b);
        e->key = (int)get32(&b);
        e->count = (int)get32(&b);
        e->value = getd(&b);
        getstr(&b, e->text, sizeof e->text);
        getstr(&b, e->detail, sizeof e->detail);
        ev_ref[i] = get_ref(&b, (int)ncodes);
        if (kind >= EV_KINDS || e->colour < -1 || e->colour >= SYN_NCOL)
            b.bad = true;
    }
    for (int i = 0; i < nlines && !b.bad; i++) {
        Line *l = &ln[i];
        l->id = get32(&b);
        l->founder = (int)get32(&b);
        l->founded = (int64_t)get64(&b);
        l->peak_t = (int64_t)get64(&b);
        l->last_seen = (int64_t)get64(&b);
        l->peak = (int)get32(&b);
        l->alive = (int)get32(&b);
        const uint8_t *f = get(&b, 4);
        if (f) {
            l->led = f[0] != 0;
            l->notable = f[1] != 0;
            l->extinct = f[2] != 0;
            l->colour = f[3];
            if (f[3] >= SYN_NCOL)
                b.bad = true;
        }
        if (!l->id)
            b.bad = true;
    }
    /* the line map needs every id once */
    if (!b.bad && nlines) {
        int cap = 64;
        while (cap < 2 * nlines)
            cap *= 2;
        uint32_t *seen = calloc((size_t)cap, sizeof *seen);
        if (!seen)
            abort();
        for (int i = 0; i < nlines && !b.bad; i++) {
            unsigned m = (unsigned)cap - 1, h = map_hash(ln[i].id) & m;
            while (seen[h] && seen[h] != ln[i].id)
                h = (h + 1) & m;
            if (seen[h])
                b.bad = true;
            seen[h] = ln[i].id;
        }
        free(seen);
    }
    if (b.at != n)
        b.bad = true;
    /* the tree last: it changes nothing unless all of it reads well */
    if (b.bad || !syn_family_load(w, family, family_n)) {
        drop_codes(codes, (int)ncodes);
        free(s);
        free(ep);
        free(ep_ref);
        free(ev);
        free(ev_ref);
        free(ln);
        return false;
    }

    /* all of it read: the history it was */
    release_all();
    if (!H.s) {
        H.s = malloc((size_t)HIST_CAP * sizeof *H.s);
        if (!H.s)
            abort();
    }
    if (!H.epochs) {
        H.epochs = calloc((size_t)EPOCHS_CAP, sizeof *H.epochs);
        if (!H.epochs)
            abort();
    }
    memcpy(H.s, s, (size_t)hn * sizeof *s);
    H.n = hn;
    H.stride = stride;
    H.commit_t = commit_t;
    H.birth0 = totals[0];
    H.death0 = totals[1];
    H.inf0 = totals[2];
    H.ubirth0 = totals[3];
    H.udeath0 = totals[4];
    H.uinf0 = totals[5];
    H.kill0 = totals[6];
    H.bite0 = totals[7];
    /* the codes go to their holders (0.9: one reference each) */
    for (int i = 0; i < nepochs; i++) {
        H.epochs[i] = ep[i];
        for (int k = 0; k < CH_COUNT; k++) {
            H.epochs[i].ch[k].gc = NULL;
            int r = ep_ref[i * CH_COUNT + k];
            if (r)
                keep_code(&H.epochs[i].ch[k].gc, codes[r - 1]);
        }
    }
    H.nepochs = nepochs;
    H.epoch_base = epoch_base;
    H.epoch_k0 = epoch_k0;
    H.ev = grow(H.ev, &H.cap_ev, nev, sizeof *H.ev);
    for (int i = 0; i < nev; i++) {
        H.ev[i] = ev[i];
        H.ev[i].gc = NULL;
        if (ev_ref[i])
            keep_code(&H.ev[i].gc, codes[ev_ref[i] - 1]);
    }
    H.nev = nev;
    H.lines = grow(H.lines, &H.cap_lines, nlines, sizeof *H.lines);
    H.nlines = 0;
    for (int i = 0; i < nlines; i++) {
        H.lines[i] = ln[i];
        H.nlines = i + 1;
        map_put(ln[i].id, i);
    }
    H.t_start = t_start;
    H.seen_t = seen_t;
    H.started = true;
    H.extinctions = extinctions;
    H.lead_col = lead_col;
    H.col_cand = col_cand;
    H.col_hold = col_hold;
    memcpy(H.col_zero, col_zero, sizeof H.col_zero);
    memcpy(H.col_peak, col_peak, sizeof H.col_peak);
    memcpy(H.col_seen, col_seen, sizeof H.col_seen);
    memcpy(H.col_gone, col_gone, sizeof H.col_gone);
    H.lead_line = lead_line;
    H.line_cand = line_cand;
    H.line_hold = line_hold;
    H.pop_record = pop_record;
    H.rec_children = rec_children;
    H.rec_kills = rec_kills;
    H.rec_infected = rec_infected;
    H.co2_low = lows[0] != 0;
    H.o2_low = lows[1] != 0;
    drop_codes(codes, (int)ncodes);
    free(s);
    free(ep);
    free(ep_ref);
    free(ev);
    free(ev_ref);
    free(ln);

    if (note) {
        char text[96], tt[32];
        num(tt, sizeof tt, (double)w->stats.time);
        snprintf(text, sizeof text, "World opened at t %s", tt);
        Event *e = add_event(w->stats.time, EV_START, 0, text);
        snprintf(e->detail, sizeof e->detail, "its history, saved beside it, goes on from here");
    }
    return true;
}
