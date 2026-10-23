/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from World.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/world.c.
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

#include "sim/world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sim/params.h"

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        abort();
    return q;
}

void world_init(World *w)
{
    memset(w, 0, sizeof *w);
    w->width = P.world_width;
    w->height = P.world_height;
    w->depth = P.world_depth;
}

void world_clear(World *w)
{
    for (int i = 0; i < w->norgs; i++)
        org_free(w->orgs[i]);
    w->norgs = 0;
    w->grid_valid = false;
}

void world_free(World *w)
{
    world_clear(w);
    free(w->orgs);
    free(w->cell_start);
    free(w->cell_fill);
    free(w->cell_items);
    free(w->item_cell);
    memset(w, 0, sizeof *w);
}

int world_new_id(World *w)
{
    return w->next_id++;
}

int world_n_corpses(const World *w)
{
    return w->norgs - w->population;
}

double world_energy_in_bodies(const World *w)
{
    double e = 0;
    for (int i = 0; i < w->norgs; i++)
        e += w->orgs[i]->energy;
    return e;
}

V3 world_life_centre_or_box(const World *w)
{
    V3 sum = v3(0, 0, 0);
    int n = 0;
    for (int i = 0; i < w->norgs; i++)
        if (w->orgs[i]->alive) {
            sum = v3_add(sum, w->orgs[i]->pos);
            n++;
        }
    return n ? v3_scale(sum, 1.0 / n) : v3(w->width / 2.0, w->height / 2.0, w->depth / 2.0);
}

/* ---- the air: 0.9's closed carbon cycle ---- */

void world_add_o2(World *w, double q)
{
    w->o2 += q;
}

void world_decrease_co2(World *w, double q)
{
    w->co2 -= bio_min(q, w->co2);
}

double world_respiration(World *w, double q)
{
    double d = bio_min(q, w->o2);
    w->o2 -= d;
    w->co2 += d;
    return d;
}

double world_photosynthesis(World *w, double q)
{
    q /= P.green_obtained_energy_divisor;
    q = bio_min(bio_min(q, q * w->co2 / P.drain_subs_divisor), w->co2);
    w->co2 -= q;
    w->o2 += q;
    return q;
}

/* ---- events ---- */

static void world_append(World *w, Organism *o)
{
    if (w->norgs == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 64;
        w->orgs = xrealloc(w->orgs, (size_t)w->cap * sizeof *w->orgs);
    }
    w->orgs[w->norgs++] = o;
}

void world_add_organism(World *w, Organism *child, Organism *parent)
{
    /* a newborn began this frame where it was born (what a window draws
     * it moving from) */
    child->prev_pos = child->pos;
    child->prev_q = child->q;
    world_append(w, child);
    if (parent)
        w->counts.births++;
    w->population++;
}

void world_organism_has_died(World *w, Organism *dying, Organism *killer)
{
    (void)dying;
    w->population--;
    w->counts.deaths++;
    if (killer)
        w->counts.kills++;
}

void world_organism_has_been_infected(World *w, Organism *infected, Organism *infector)
{
    (void)infected;
    (void)infector;
    w->counts.infections++;
}

void world_organism_bitten(World *w, Organism *eater, Organism *eaten, double energy)
{
    (void)eater;
    (void)eaten;
    w->counts.bites++;
    w->counts.eaten += energy;
}

void world_genesis(World *w)
{
    world_clear(w);
    w->frame = 0;
    w->o2 = P.initial_o2;
    w->co2 = P.initial_co2;
    w->lost = 0;
    w->next_id = 0;
    w->population = 0;
    memset(&w->counts, 0, sizeof w->counts);
    w->width = P.world_width;
    w->height = P.world_height;
    w->depth = P.world_depth;
    for (int i = 0; i < P.initial_organisms; i++) {
        Organism *b = org_new(w);
        if (org_random_create(b))
            world_add_organism(w, b, NULL);
        else
            org_free(b);
    }
}

int world_add_founders(World *w, GeneticCode *gc, int copies)
{
    int placed = 0;
    for (int i = 0; i < copies; i++) {
        Organism *b = org_new(w);
        if (org_place_with_code(b, gc)) {
            world_add_organism(w, b, NULL);
            placed++;
        } else {
            org_free(b);
        }
    }
    return placed;
}

/* ---- finding neighbours ---- */

static bool boxes_overlap(const Organism *a, const Organism *b)
{
    return a->lo.x <= b->hi.x && a->hi.x >= b->lo.x && a->lo.y <= b->hi.y &&
           a->hi.y >= b->lo.y && a->lo.z <= b->hi.z && a->hi.z >= b->lo.z;
}

Organism *world_fast_check_hit(World *w, Organism *o)
{
    for (int i = 0; i < w->norgs; i++) {
        Organism *b = w->orgs[i];
        if (b != o && !b->removed && boxes_overlap(o, b))
            return o;
    }
    return NULL;
}

static int cell_coord(double v, double cell, int n)
{
    int c = (int)floor(v / cell);
    return c < 0 ? 0 : c >= n ? n - 1 : c;
}

/* Sort the frame-start organisms into cells by centre. Speed is clamped to
 * max_vel everywhere it changes, so a centre moves at most that far in a
 * frame, and a body reaches at most `radius` from its centre (12% more after
 * a growth step). Any organism a body can touch this frame therefore has its
 * frame-start centre within `reach` of the body's box; the extra 10% of
 * speed and the unit are slack. */
static void build_grid(World *w)
{
    int n = w->norgs;
    double maxr = 0;
    for (int i = 0; i < n; i++)
        maxr = bio_max(maxr, w->orgs[i]->radius);
    w->reach = maxr * 1.13 + P.max_vel * 1.1 + 2 * P.segment_radius + 1;

    w->cell = 32;
    for (;;) {
        w->gx = (int)ceil(w->width / w->cell);
        w->gy = (int)ceil(w->height / w->cell);
        w->gz = (int)ceil(w->depth / w->cell);
        if ((int64_t)w->gx * w->gy * w->gz <= (1 << 20))
            break;
        w->cell *= 2;
    }
    int ncells = w->gx * w->gy * w->gz;
    if (ncells + 1 > w->ncells_cap) {
        w->ncells_cap = ncells + 1;
        w->cell_start = xrealloc(w->cell_start, (size_t)w->ncells_cap * sizeof *w->cell_start);
        w->cell_fill = xrealloc(w->cell_fill, (size_t)w->ncells_cap * sizeof *w->cell_fill);
    }
    if (n > w->items_cap) {
        w->items_cap = n * 2;
        w->cell_items = xrealloc(w->cell_items, (size_t)w->items_cap * sizeof *w->cell_items);
        w->item_cell = xrealloc(w->item_cell, (size_t)w->items_cap * sizeof *w->item_cell);
    }
    memset(w->cell_start, 0, (size_t)(ncells + 1) * sizeof *w->cell_start);
    for (int i = 0; i < n; i++) {
        const Organism *o = w->orgs[i];
        int c = (cell_coord(o->pos.z, w->cell, w->gz) * w->gy +
                 cell_coord(o->pos.y, w->cell, w->gy)) * w->gx +
                cell_coord(o->pos.x, w->cell, w->gx);
        w->item_cell[i] = c;
        w->cell_start[c + 1]++;
    }
    for (int c = 0; c < ncells; c++)
        w->cell_start[c + 1] += w->cell_start[c];
    memcpy(w->cell_fill, w->cell_start, (size_t)ncells * sizeof *w->cell_fill);
    for (int i = 0; i < n; i++)
        w->cell_items[w->cell_fill[w->item_cell[i]]++] = w->orgs[i];
    w->grid_valid = true;
}

Organism *world_check_hit(World *w, Organism *o)
{
    if (!w->grid_valid)
        return NULL;
    double r = w->reach;
    int x0 = cell_coord(o->lo.x - r, w->cell, w->gx), x1 = cell_coord(o->hi.x + r, w->cell, w->gx);
    int y0 = cell_coord(o->lo.y - r, w->cell, w->gy), y1 = cell_coord(o->hi.y + r, w->cell, w->gy);
    int z0 = cell_coord(o->lo.z - r, w->cell, w->gz), z1 = cell_coord(o->hi.z + r, w->cell, w->gz);
    for (int z = z0; z <= z1; z++)
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int c = (z * w->gy + y) * w->gx + x;
                for (int k = w->cell_start[c]; k < w->cell_start[c + 1]; k++) {
                    Organism *b = w->cell_items[k];
                    if (b == o || !boxes_overlap(o, b))
                        continue;
                    if (b->energy >= BIO_TOL && o->energy >= BIO_TOL && org_contact(o, b)) {
                        w->counts.touches++;
                        return o;
                    }
                }
            }
    return NULL;
}

/* ---- the frame ---- */

void world_time(World *w)
{
    build_grid(w);
    int l = w->norgs;
    for (int i = 0; i < l; i++) {
        Organism *b = w->orgs[i];
        if (!org_move(b))
            b->removed = true;
    }
    /* Organisms out of energy leave, the crumbs they held with them. */
    int k = 0;
    for (int i = 0; i < w->norgs; i++) {
        Organism *b = w->orgs[i];
        if (b->removed) {
            w->lost += b->energy;
            org_free(b);
        } else {
            w->orgs[k++] = b;
        }
    }
    w->norgs = k;
    w->grid_valid = false;
    w->frame++;
}

/* ---- fingerprint ---- */

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t world_hash(const World *w)
{
    uint64_t h = 14695981039346656037ULL;
    h = fnv(h, &w->frame, sizeof w->frame);
    h = fnv(h, &w->o2, sizeof w->o2);
    h = fnv(h, &w->co2, sizeof w->co2);
    h = fnv(h, &w->population, sizeof w->population);
    h = fnv(h, &w->norgs, sizeof w->norgs);
    h = fnv(h, &g_rng.seed, sizeof g_rng.seed);
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        int alive = o->alive;
        h = fnv(h, &o->id, sizeof o->id);
        h = fnv(h, &o->pos, sizeof o->pos);
        h = fnv(h, &o->q, sizeof o->q);
        h = fnv(h, &o->v, sizeof o->v);
        h = fnv(h, &o->w, sizeof o->w);
        h = fnv(h, &o->energy, sizeof o->energy);
        h = fnv(h, &o->growth_ratio, sizeof o->growth_ratio);
        h = fnv(h, &o->age, sizeof o->age);
        h = fnv(h, &alive, sizeof alive);
        h = fnv(h, &o->segments, sizeof o->segments);
    }
    return h;
}
