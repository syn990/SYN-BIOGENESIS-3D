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

#include "app/history.h"

#include <stddef.h>
#include <string.h>

static void now(const World *w, const WorldCounts *last, Sample *s)
{
    memset(s, 0, sizeof *s);
    s->t = (double)(w->frame / 256);
    s->population = w->population;
    s->corpses = world_n_corpses(w);
    s->o2 = w->o2;
    s->co2 = w->co2;
    s->births = (double)(w->counts.births - last->births);
    s->deaths = (double)(w->counts.deaths - last->deaths);
    s->kills = (double)(w->counts.kills - last->kills);
    s->infections = (double)(w->counts.infections - last->infections);
    s->bites = (double)(w->counts.bites - last->bites);
    int alive = 0;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        if (!o->alive)
            continue;
        alive++;
        for (int k = 0; k < o->segments; k++)
            s->colour[o->seg_color[k]] += o->m[k];
        s->flat += gc_is_flat(o->gc);
        s->level += org_levelness(o);
    }
    if (alive) {
        s->flat /= alive;
        s->level /= alive;
    }
}

/* the mean of the levels and the sum of the events */
static void merge(Sample *into, const Sample *a, const Sample *b)
{
    Sample m;
    m.t = b->t;
    m.population = (a->population + b->population) / 2;
    m.corpses = (a->corpses + b->corpses) / 2;
    m.o2 = (a->o2 + b->o2) / 2;
    m.co2 = (a->co2 + b->co2) / 2;
    m.births = (a->births + b->births) / 2;
    m.deaths = (a->deaths + b->deaths) / 2;
    m.kills = (a->kills + b->kills) / 2;
    m.infections = (a->infections + b->infections) / 2;
    m.bites = (a->bites + b->bites) / 2;
    for (int c = 0; c < COL_COUNT; c++)
        m.colour[c] = (a->colour[c] + b->colour[c]) / 2;
    m.flat = (a->flat + b->flat) / 2;
    m.level = (a->level + b->level) / 2;
    *into = m;
}

void history_reset(History *h, const World *w)
{
    h->n = 0;
    h->stride = 1;
    h->pending = 0;
    h->last = w->counts;
    now(w, &h->last, &h->s[0]);
    h->n = 1;
}

static void add(Sample *sum, const Sample *s)
{
    double *d = &sum->population;
    const double *v = &s->population;
    for (size_t i = 0; i < (sizeof *s - offsetof(Sample, population)) / sizeof(double); i++)
        d[i] += v[i];
    sum->t = s->t;
}

void history_frame(History *h, const World *w)
{
    if (w->frame % 256)
        return;
    Sample s;
    now(w, &h->last, &s);
    h->last = w->counts;
    if (h->pending == 0)
        memset(&h->acc, 0, sizeof h->acc);
    add(&h->acc, &s);
    if (++h->pending < h->stride)
        return;
    /* the units it covers, as their mean */
    double *d = &h->acc.population;
    for (size_t i = 0; i < (sizeof h->acc - offsetof(Sample, population)) / sizeof(double); i++)
        d[i] /= h->pending;
    h->pending = 0;
    if (h->n == HISTORY_MAX) {
        for (int i = 0; i < HISTORY_MAX / 2; i++)
            merge(&h->s[i], &h->s[2 * i], &h->s[2 * i + 1]);
        h->n = HISTORY_MAX / 2;
        h->stride *= 2;
    }
    h->s[h->n++] = h->acc;
}
