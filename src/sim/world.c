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

#include "sim/dmath.h"
#include "sim/params.h"

/* the water's cells are at most this many units a side */
#define WATER_CELL 32

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
    world_reset_water(w, 0);
    w->daylight = 1;
}

void world_clear(World *w)
{
    for (int i = 0; i < w->norgs; i++) {
        if (w->on_remove)
            w->on_remove(w->ud, w->orgs[i]);
        org_free(w->orgs[i]);
    }
    w->norgs = 0;
    w->grid_valid = false;
}

void world_free(World *w)
{
    world_clear(w);
    stats_release(&w->stats);
    free(w->orgs);
    free(w->cell_start);
    free(w->cell_fill);
    free(w->cell_items);
    free(w->item_cell);
    free(w->co2_cell);
    free(w->co2_flux);
    free(w->ch4_cell);
    free(w->shade_cell);
    free(w->light_cell);
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

/* ---- the water ---- */

static int sides(int len)
{
    int n = (int)ceil(len / (double)WATER_CELL);
    return n < 1 ? 1 : n;
}

void world_reset_water(World *w, double density)
{
    w->fx = sides(w->width);
    w->fy = sides(w->height);
    w->fz = sides(w->depth);
    w->fsx = w->width / (double)w->fx;
    w->fsy = w->height / (double)w->fy;
    w->fsz = w->depth / (double)w->fz;
    size_t n = (size_t)world_n_cells(w);
    free(w->co2_cell);
    free(w->co2_flux);
    free(w->ch4_cell);
    free(w->shade_cell);
    free(w->light_cell);
    w->co2_cell = calloc(n, sizeof *w->co2_cell);
    w->co2_flux = calloc(n, sizeof *w->co2_flux);
    w->ch4_cell = calloc(n, sizeof *w->ch4_cell);
    w->shade_cell = calloc(n, sizeof *w->shade_cell);
    w->light_cell = calloc(n, sizeof *w->light_cell);
    if (!w->co2_cell || !w->co2_flux || !w->ch4_cell || !w->shade_cell || !w->light_cell)
        abort();
    double co2_per_cell = density * world_cell_volume(w) / 1e6;
    w->co2 = 0;
    w->ch4 = 0;
    for (size_t i = 0; i < n; i++) {
        w->co2_cell[i] = co2_per_cell;
        w->light_cell[i] = 1;
        w->co2 += co2_per_cell;
    }
}

int world_n_cells(const World *w)
{
    return w->fx * w->fy * w->fz;
}

double world_cell_volume(const World *w)
{
    return w->fsx * w->fsy * w->fsz;
}

/* (int) truncates where floor would round down, but only below 0, where
 * both are clamped to the first cell. */
static int axis_cell(double v, double side, int n)
{
    int c = (int)(v / side);
    return c < 0 ? 0 : c >= n ? n - 1 : c;
}

int world_cell_of(const World *w, V3 p)
{
    return (axis_cell(p.z, w->fsz, w->fz) * w->fy + axis_cell(p.y, w->fsy, w->fy)) * w->fx +
           axis_cell(p.x, w->fsx, w->fx);
}

double world_light_at(const World *w, V3 p)
{
    double below = bio_max(0, w->depth - p.z);
    double water = P.light_depth > 0 ? d_exp(-below / P.light_depth) : 1;
    return w->daylight * water * w->light_cell[world_cell_of(w, p)];
}

/* A world begins at sunrise: half light and brightening, noon a quarter
 * of a day later, midnight three quarters. */
static double daylight(const World *w)
{
    if (!(P.day_length > 0))
        return 1;
    double day = P.day_length * 256;
    double phase = fmod((double)w->frame, day) / day;
    return 0.5 + 0.5 * d_sin(2 * D_PI * phase);
}

V3 world_co2_gradient(const World *w, V3 p)
{
    int x = axis_cell(p.x, w->fsx, w->fx), y = axis_cell(p.y, w->fsy, w->fy),
        z = axis_cell(p.z, w->fsz, w->fz);
    const double *c = w->co2_cell;
    int sx = 1, sy = w->fx, sz = w->fx * w->fy, i = (z * w->fy + y) * w->fx + x;
    V3 g = v3((x + 1 < w->fx ? c[i + sx] : c[i]) - (x > 0 ? c[i - sx] : c[i]),
              (y + 1 < w->fy ? c[i + sy] : c[i]) - (y > 0 ? c[i - sy] : c[i]),
              (z + 1 < w->fz ? c[i + sz] : c[i]) - (z > 0 ? c[i - sz] : c[i]));
    return v3_unit_or(g, v3(0, 0, 0));
}

V3 world_light_gradient(const World *w, V3 p)
{
    int x = axis_cell(p.x, w->fsx, w->fx), y = axis_cell(p.y, w->fsy, w->fy),
        z = axis_cell(p.z, w->fsz, w->fz);
    const double *c = w->light_cell;
    int sx = 1, sy = w->fx, i = (z * w->fy + y) * w->fx + x;
    V3 g = v3((x + 1 < w->fx ? c[i + sx] : c[i]) - (x > 0 ? c[i - sx] : c[i]),
              (y + 1 < w->fy ? c[i + sy] : c[i]) - (y > 0 ? c[i - sy] : c[i]), 0);
    return v3_unit_or(g, v3(0, 0, 0));
}

/* The shade every body casts this frame: a segment's shadow is the area it
 * covers seen from above (its capsule's), spread over its cell's floor.
 * Then, down each column from the surface, the light reaching the middle of
 * each cell. */
static void build_light(World *w)
{
    int n = world_n_cells(w);
    memset(w->shade_cell, 0, (size_t)n * sizeof *w->shade_cell);
    double r = P.segment_radius, floor_area = w->fsx * w->fsy;
    for (int i = 0; i < w->norgs; i++) {
        const Organism *o = w->orgs[i];
        for (int s = 0; s < o->segments; s++) {
            V3 a = o->ws[s], b = o->we[s];
            double h = sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            double area = 2 * r * h + D_PI * r * r;
            w->shade_cell[world_cell_of(w, v3_scale(v3_add(a, b), 0.5))] +=
                P.shade * area / floor_area;
        }
    }
    int layer = w->fx * w->fy;
    for (int c = 0; c < layer; c++) {
        /* an empty cell lets through what reached it: e^-above, kept from
         * the last cell that cast shade */
        double above = 0, through = 1;
        for (int z = w->fz - 1; z >= 0; z--) {
            int i = z * layer + c;
            double t = w->shade_cell[i];
            if (t == 0) {
                w->light_cell[i] = through;
                continue;
            }
            w->light_cell[i] = d_exp(-(above + t / 2));
            above += t;
            through = d_exp(-above);
        }
    }
    for (int i = 0; i < w->norgs; i++)
        w->orgs[i]->lit = world_light_at(w, w->orgs[i]->pos);
}

/* A dissolved gas (CO2, methane) spreads from each cell to its six
 * neighbours by the difference between them, every frame, as fast as CO2
 * does. What one cell gives, the other takes, so the total only changes by
 * rounding. */
static void spread(World *w, double *c)
{
    if (!(P.co2_diffusion > 0))
        return;
    int n = world_n_cells(w);
    double *f = w->co2_flux;
    memset(f, 0, (size_t)n * sizeof *f);
    double kx = bio_min(0.15, P.co2_diffusion / (w->fsx * w->fsx));
    double ky = bio_min(0.15, P.co2_diffusion / (w->fsy * w->fsy));
    double kz = bio_min(0.15, P.co2_diffusion / (w->fsz * w->fsz));
    int sy = w->fx, sz = w->fx * w->fy;
    for (int z = 0, i = 0; z < w->fz; z++)
        for (int y = 0; y < w->fy; y++)
            for (int x = 0; x < w->fx; x++, i++) {
                double d;
                if (x + 1 < w->fx) {
                    d = kx * (c[i] - c[i + 1]);
                    f[i] -= d;
                    f[i + 1] += d;
                }
                if (y + 1 < w->fy) {
                    d = ky * (c[i] - c[i + sy]);
                    f[i] -= d;
                    f[i + sy] += d;
                }
                if (z + 1 < w->fz) {
                    d = kz * (c[i] - c[i + sz]);
                    f[i] -= d;
                    f[i + sz] += d;
                }
            }
    for (int i = 0; i < n; i++)
        c[i] += f[i];
}

/* ---- the carbon: 0.9's closed cycle, in the water ---- */

void world_add_o2(World *w, double q)
{
    w->o2 += q;
}

void world_add_co2(World *w, double q)
{
    int n = world_n_cells(w);
    if (!(q > 0) || n <= 0)
        return;
    for (int i = 0; i < n; i++)
        w->co2_cell[i] += q / n;
    w->co2 += q;
}

void world_kill_all(World *w)
{
    for (int i = 0; i < w->norgs; i++) {
        Organism *b = w->orgs[i];
        if (!b->removed && b->alive)
            org_die(b, NULL);
    }
}

void world_disperse_all(World *w)
{
    for (int i = 0; i < w->norgs; i++) {
        Organism *b = w->orgs[i];
        if (!b->removed && !b->alive)
            org_use_energy(b, b->energy);
    }
}

void world_take_co2(World *w, double q)
{
    if (!(w->co2 > 0))
        return;
    double share = bio_min(1, q / w->co2);
    int n = world_n_cells(w);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        w->co2_cell[i] -= w->co2_cell[i] * share;
        sum += w->co2_cell[i];
    }
    w->co2 = sum;
}

double world_respiration(World *w, int cell, double q)
{
    double d = bio_min(q, w->o2);
    w->o2 -= d;
    w->co2 += d;
    w->co2_cell[cell] += d;
    return d;
}

/* Methane turns back into CO2 where it is, over ch4_oxidation frames. */
static void oxidise_ch4(World *w)
{
    if (!(P.ch4_oxidation > 0) || !(w->ch4 > 0))
        return;
    double k = 1 / P.ch4_oxidation, sum = 0;
    int n = world_n_cells(w);
    for (int i = 0; i < n; i++) {
        double x = w->ch4_cell[i] * k;
        w->ch4_cell[i] -= x;
        w->co2_cell[i] += x;
        w->co2 += x;
        sum += w->ch4_cell[i];
    }
    w->ch4 = sum;
}

double world_decomposition(World *w, int cell, double q)
{
    double d = bio_min(q, w->o2);
    w->o2 -= d;
    w->ch4 += d;
    w->ch4_cell[cell] += d;
    return d;
}

double world_methanotrophy(World *w, int cell, double length)
{
    double q = length / P.methane_divisor;
    if (P.ch4_saturation > 0) {
        double conc = w->ch4_cell[cell] / world_cell_volume(w) * 1e6;
        q = bio_min(q, q * conc / P.ch4_saturation);
    }
    q = bio_min(q, w->ch4_cell[cell]);
    w->ch4_cell[cell] -= q;
    w->ch4 -= q;
    w->o2 += q;
    return q;
}

double world_photosynthesis(World *w, int cell, double light)
{
    double q = light / P.green_obtained_energy_divisor;
    if (P.co2_saturation > 0) {
        double conc = w->co2_cell[cell] / world_cell_volume(w) * 1e6;
        q = bio_min(q, q * conc / P.co2_saturation);
    }
    q = bio_min(q, w->co2_cell[cell]);
    w->co2_cell[cell] -= q;
    w->co2 -= q;
    w->o2 += q;
    return q;
}

/* ---- events ---- */

void world_append_raw(World *w, Organism *o)
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
    world_append_raw(w, child);
    if (parent) {
        w->counts.births++;
        stats_organism_born(&w->stats, child, parent);
    }
    stats_organism_created(&w->stats);
    w->population++;
    stats_population_increase(&w->stats, w->population);
}

void world_organism_has_died(World *w, Organism *dying, Organism *killer)
{
    w->population--;
    w->counts.deaths++;
    if (killer)
        w->counts.kills++;
    stats_organism_die(&w->stats, dying, killer);
    stats_population_decrease(&w->stats, w->population);
}

void world_organism_has_been_infected(World *w, Organism *infected, Organism *infector)
{
    w->counts.infections++;
    stats_organism_infects(&w->stats, infected, infector);
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

void world_visit(World *w, V3 p, double r, void (*fn)(Organism *b, void *ud), void *ud)
{
    if (!w->grid_valid)
        return;
    int x0 = cell_coord(p.x - r, w->cell, w->gx), x1 = cell_coord(p.x + r, w->cell, w->gx);
    int y0 = cell_coord(p.y - r, w->cell, w->gy), y1 = cell_coord(p.y + r, w->cell, w->gy);
    int z0 = cell_coord(p.z - r, w->cell, w->gz), z1 = cell_coord(p.z + r, w->cell, w->gz);
    for (int z = z0; z <= z1; z++)
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int c = (z * w->gy + y) * w->gx + x;
                for (int k = w->cell_start[c]; k < w->cell_start[c + 1]; k++)
                    fn(w->cell_items[k], ud);
            }
}

/* ---- the frame ---- */

void world_prepare(World *w)
{
    build_grid(w);
    w->daylight = daylight(w);
    build_light(w);
}

void world_time(World *w)
{
    world_prepare(w);
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
            if (w->on_remove)
                w->on_remove(w->ud, b);
            org_free(b);
        } else {
            w->orgs[k++] = b;
        }
    }
    w->norgs = k;
    w->grid_valid = false;
    spread(w, w->co2_cell);
    spread(w, w->ch4_cell);
    oxidise_ch4(w);
    w->frame++;
    if (w->frame % 256 == 0)
        stats_event_time(&w->stats, w->population, w->o2, w->co2);
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
    h = fnv(h, w->co2_cell, (size_t)world_n_cells(w) * sizeof *w->co2_cell);
    h = fnv(h, w->ch4_cell, (size_t)world_n_cells(w) * sizeof *w->ch4_cell);
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
        h = fnv(h, &o->aim, sizeof o->aim);
    }
    return h;
}
