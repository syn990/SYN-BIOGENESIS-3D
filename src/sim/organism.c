/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from Organism.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/organism.c.
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

#include "sim/organism.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "sim/params.h"
#include "sim/world.h"

Organism *org_new(World *w)
{
    Organism *o = calloc(1, sizeof *o);
    if (!o)
        abort();
    o->world = w;
    /* 0.9 gave every new organism a random theta; here that turn is about
     * the body's own up axis, and a founder's up axis is the world's. */
    o->q = quat_axis_angle(v3(0, 0, 1), jr_next_double(&g_rng) * D_PI * 2.0);
    o->has_moved = true;
    o->alive = true;
    o->flash_color = FLASH_NONE;
    return o;
}

void org_free(Organism *o)
{
    if (!o)
        return;
    gc_unref(o->gc);
    gc_unref(o->infected_gc);
    free(o->bs);
    free(o);
}

/* Organism.create: the per-segment arrays, in one block that bs owns. */
static void org_create(Organism *o)
{
    int n = o->gc->ngenes * o->gc->symmetry;
    o->segments = n;
    free(o->bs);
    size_t v3s = (size_t)n * 4 * sizeof(V3);
    size_t dbls = (size_t)n * sizeof(double);
    char *block = calloc(1, v3s + dbls + (size_t)n);
    if (!block)
        abort();
    o->bs = (V3 *)block;
    o->be = o->bs + n;
    o->ws = o->be + n;
    o->we = o->ws + n;
    o->m = (double *)(block + v3s);
    o->seg_color = (uint8_t *)(block + v3s + dbls);
    for (int i = 0; i < n; i++)
        o->seg_color[i] = o->gc->genes[i % o->gc->ngenes].color;
}

Organism *org_new_bare(World *w, GeneticCode *gc)
{
    Organism *o = calloc(1, sizeof *o);
    if (!o)
        abort();
    o->world = w;
    o->gc = gc_ref(gc);
    o->alive = true;
    o->flash_color = FLASH_NONE;
    org_create(o);
    return o;
}

/* ---- the body ----
 *
 * 0.9 drew each arm as a turtle on the plane: the first gene's theta is the
 * arm's heading (plus the arm's share of the circle), every later gene
 * turns the heading by its theta, and each segment is the gene's length
 * along it. Here the turtle carries an up vector too. theta turns the
 * heading about up (in the turtle's current plane) and pitch bends it
 * toward up, carrying up along. With every pitch 0, up stays the world's
 * z, every turn is 0.9's turn and the body is 0.9's body, lying flat. */

static void turtle_yaw(V3 *h, V3 u, double a)
{
    if (a == 0)
        return;
    double c = d_cos(a), s = d_sin(a);
    *h = v3_add(v3_scale(*h, c), v3_scale(v3_cross(u, *h), s));
}

static void turtle_pitch(V3 *h, V3 *u, double b)
{
    if (b == 0)
        return;
    double c = d_cos(b), s = d_sin(b);
    V3 nh = v3_add(v3_scale(*h, c), v3_scale(*u, s));
    V3 nu = v3_sub(v3_scale(*u, c), v3_scale(*h, s));
    *h = nh;
    *u = nu;
}

void gc_body_shape(const GeneticCode *gc, double scale, V3 *bs, V3 *be)
{
    int symmetry = gc->symmetry;
    int sequence = gc->ngenes;
    int seg = 0;

    for (int i = 0; i < symmetry; i++) {
        /* A mirrored arm is the reflection of its neighbour in a vertical
         * plane: its turns go the other way, its bends the same way. */
        bool mirrored = gc->mirror != 0 && i % 2 != 0;
        V3 p = v3(0, 0, 0), h = v3(1, 0, 0), u = v3(0, 0, 1);
        for (int j = 0; j < sequence; j++, seg++) {
            const Gene *g = &gc->genes[j];
            if (j == 0) {
                double a = mirrored ? D_PI - (g->theta + (i - 1) * 2 * D_PI / symmetry)
                                    : g->theta + i * 2 * D_PI / symmetry;
                h = v3(d_cos(a), d_sin(a), 0);
            } else {
                turtle_yaw(&h, u, mirrored ? -g->theta : g->theta);
            }
            turtle_pitch(&h, &u, g->pitch);
            h = v3_unit_or(h, h);
            u = v3_unit_or(v3_sub(u, v3_scale(h, v3_dot(h, u))), u);
            bs[seg] = p;
            p = v3_add(p, v3_scale(h, g->length / scale));
            be[seg] = p;
        }
    }
}

void org_build_body(Organism *o)
{
    gc_body_shape(o->gc, k_scale[o->growth_ratio - 1], o->bs, o->be);

    /* A segment weighs its length, as in 0.9. The body turns about its
     * centre of mass (0.9 turned it about the middle of its bounding box). */
    double mass = 0;
    V3 com = v3(0, 0, 0);
    for (int i = 0; i < o->segments; i++) {
        double len = v3_len(v3_sub(o->be[i], o->bs[i]));
        if (len == 0)
            len = 0.01;
        o->m[i] = len;
        mass += len;
        com = v3_add(com, v3_scale(v3_add(o->bs[i], o->be[i]), len / 2));
    }
    com = v3_scale(com, 1.0 / mass);
    o->mass = mass;

    /* Inertia tensor of rods of density 1 along the segments, plus each
     * capsule's thickness as a solid cylinder's (m r^2/4 across it, m r^2/2
     * along it), which also lets a straight one-segment body be spun. */
    double S[3][3] = {{0}}, T[3][3] = {{0}};
    double rr = P.segment_radius;
    double far = 0;
    for (int i = 0; i < o->segments; i++) {
        o->bs[i] = v3_sub(o->bs[i], com);
        o->be[i] = v3_sub(o->be[i], com);
        V3 a = o->bs[i], d = v3_sub(o->be[i], o->bs[i]);
        V3 u = v3_unit_or(d, v3(1, 0, 0));
        double av[3] = {a.x, a.y, a.z}, dv[3] = {d.x, d.y, d.z}, uv[3] = {u.x, u.y, u.z};
        double cyl = o->m[i] * rr * rr / 4;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                S[r][c] += o->m[i] * (av[r] * av[c] + (av[r] * dv[c] + dv[r] * av[c]) / 2 +
                                      dv[r] * dv[c] / 3);
                T[r][c] += cyl * ((r == c) + uv[r] * uv[c]);
            }
        far = bio_max(far, bio_max(v3_len(o->bs[i]), v3_len(o->be[i])));
    }
    double tr = S[0][0] + S[1][1] + S[2][2];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o->inertia.m[r][c] = (r == c ? tr : 0) - S[r][c] + T[r][c];
    o->inv_inertia = m3_inverse_sym(&o->inertia);
    o->inertia_trace = m3_trace(&o->inertia);
    o->radius = far + P.segment_radius;
}

static inline bool v3_eq(V3 a, V3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void org_update_geometry(Organism *o)
{
    M3 R = m3_from_quat(o->q);
    V3 lo = v3(DBL_MAX, DBL_MAX, DBL_MAX), hi = v3(-DBL_MAX, -DBL_MAX, -DBL_MAX);
    for (int i = 0; i < o->segments; i++) {
        /* a segment starts where the previous one ended, or at the arms'
         * shared root: those points are moved once and copied */
        if (i > 0 && v3_eq(o->bs[i], o->be[i - 1]))
            o->ws[i] = o->we[i - 1];
        else if (i > 0 && v3_eq(o->bs[i], o->bs[0]))
            o->ws[i] = o->ws[0];
        else
            o->ws[i] = v3_add(o->pos, m3_mul_v(&R, o->bs[i]));
        o->we[i] = v3_add(o->pos, m3_mul_v(&R, o->be[i]));
        V3 a = o->ws[i], b = o->we[i];
        lo = v3(bio_min(lo.x, bio_min(a.x, b.x)), bio_min(lo.y, bio_min(a.y, b.y)),
                bio_min(lo.z, bio_min(a.z, b.z)));
        hi = v3(bio_max(hi.x, bio_max(a.x, b.x)), bio_max(hi.y, bio_max(a.y, b.y)),
                bio_max(hi.z, bio_max(a.z, b.z)));
    }
    double r = P.segment_radius;
    o->lo = v3_sub(lo, v3(r, r, r));
    o->hi = v3_add(hi, v3(r, r, r));
}

double org_levelness(const Organism *o)
{
    return fabs(1 - 2 * (o->q.x * o->q.x + o->q.y * o->q.y));
}

/* x, a world-frame vector, through a body-frame tensor turned to the world
 * by q: R M R^T x. */
static V3 tensor_mul(const M3 *m, Quat q, V3 x)
{
    M3 R = m3_from_quat(q);
    return m3_mul_v(&R, m3_mul_v(m, m3_tmul_v(&R, x)));
}

/* The spin an angular impulse x gives: I^-1 x in the world frame. */
static V3 org_inv_inertia_mul(const Organism *o, V3 x)
{
    return tensor_mul(&o->inv_inertia, o->q, x);
}

/* ---- placing ---- */

static bool org_fits_world(const Organism *o)
{
    const World *w = o->world;
    return o->lo.x >= 0 && o->lo.y >= 0 && o->lo.z >= 0 && o->hi.x < w->width &&
           o->hi.y < w->height && o->hi.z < w->depth;
}

/* Twelve tries at a free random spot, as World.genesis did. Each axis's
 * draw is made only if the body fits along it, in x, y, z order. */
static bool org_place_random(Organism *o)
{
    World *w = o->world;
    o->pos = v3(0, 0, 0);
    org_update_geometry(o);
    V3 lo0 = o->lo, ext = v3_sub(o->hi, o->lo);
    for (int i = 12; i > 0; i--) {
        int bx = (int)(w->width - ext.x - 1);
        if (bx <= 0)
            return false;
        int ox = jr_next_int(&g_rng, bx);
        int by = (int)(w->height - ext.y - 1);
        if (by <= 0)
            return false;
        int oy = jr_next_int(&g_rng, by);
        int bz = (int)(w->depth - ext.z - 1);
        if (bz <= 0)
            return false;
        int oz = jr_next_int(&g_rng, bz);
        o->pos = v3_add(v3_sub(v3(ox, oy, oz), lo0), v3(0.5, 0.5, 0.5));
        org_update_geometry(o);
        if (!world_fast_check_hit(w, o)) {
            o->id = world_new_id(w);
            return true;
        }
    }
    return false;
}

/* Where a child may go: 0.9's eight directions around its parent on the
 * plane, in 0.9's order; then, if none has room, the eighteen above and
 * below, each above followed by the below under it. Each list is tried from
 * a random place in it, as 0.9 tried its eight, and the pairing means a
 * parent under the ceiling (or on the floor) has its children go to every
 * direction below (or above) alike. */
static const int k_ring[8][3] = {
    {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0},
};
static const int k_stack[18][3] = {
    {0, 0, 1},   {0, 0, -1},  {1, 0, 1},   {1, 0, -1},  {1, 1, 1},   {1, 1, -1},
    {0, 1, 1},   {0, 1, -1},  {-1, 1, 1},  {-1, 1, -1}, {-1, 0, 1},  {-1, 0, -1},
    {-1, -1, 1}, {-1, -1, -1}, {0, -1, 1}, {0, -1, -1}, {1, -1, 1},  {1, -1, -1},
};

/* The child beside its parent in direction d, boxes one unit apart. */
static bool org_try_beside(Organism *o, Organism *parent, const int *d, V3 cc, V3 ce)
{
    World *w = o->world;
    V3 pc = v3_scale(v3_add(parent->lo, parent->hi), 0.5);
    V3 gap = v3_add(v3_add(v3_scale(v3_sub(parent->hi, parent->lo), 0.5), ce), v3(1, 1, 1));
    o->pos = v3_sub(v3_add(pc, v3(gap.x * d[0], gap.y * d[1], gap.z * d[2])), cc);
    org_update_geometry(o);
    if (!org_fits_world(o) || world_fast_check_hit(w, o))
        return false;
    if (parent->gc->disperse_children)
        o->v = v3(d[0], d[1], d[2]);
    else
        o->v = parent->v;
    o->id = world_new_id(w);
    parent->energy -= o->energy;
    return true;
}

static bool org_place_near(Organism *o, Organism *parent)
{
    o->pos = v3(0, 0, 0);
    org_update_geometry(o);
    V3 cc = v3_scale(v3_add(o->lo, o->hi), 0.5), ce = v3_scale(v3_sub(o->hi, o->lo), 0.5);
    int npos = jr_next_int(&g_rng, 8);
    for (int k = 0; k < 8; k++, npos = (npos + 1) % 8)
        if (org_try_beside(o, parent, k_ring[npos], cc, ce))
            return true;
    npos = jr_next_int(&g_rng, 18);
    for (int k = 0; k < 18; k++, npos = (npos + 1) % 18)
        if (org_try_beside(o, parent, k_stack[npos], cc, ce))
            return true;
    return false;
}

/* A founder's energy comes out of the air's CO2 (and puts the same in O2)
 * only once it has a place: in 0.9 a founder that found no room took its
 * energy with it, and the world's books no longer added up. */
static bool org_found(Organism *o)
{
    World *w = o->world;
    o->parent_id = -1;
    o->generation = 1;
    o->growth_ratio = 16;
    org_create(o);
    org_build_body(o);
    if (!org_place_random(o))
        return false;
    o->energy = bio_min(P.initial_energy, w->co2);
    world_decrease_co2(w, o->energy);
    world_add_o2(w, o->energy);
    return true;
}

bool org_random_create(Organism *o)
{
    gc_assign(&o->gc, NULL);
    o->gc = gc_new_random();
    return org_found(o);
}

bool org_place_with_code(Organism *o, GeneticCode *gc)
{
    gc_assign(&o->gc, gc);
    return org_found(o);
}

bool org_inherit(Organism *o, Organism *parent, bool first)
{
    GeneticCode *inherit_gc;
    if (parent->infected_gc) {
        inherit_gc = parent->infected_gc;
        o->generation = parent->infected_generation + 1;
    } else {
        inherit_gc = parent->gc;
        o->generation = parent->generation + 1;
    }
    gc_assign(&o->gc, NULL);
    o->gc = gc_new_mutated(inherit_gc);
    o->parent_id = parent->id;
    o->growth_ratio = 16;
    /* the child buds off its parent: the parent's frame, turned about the
     * parent's up axis by the turn org_new drew */
    o->q = quat_normalize(quat_mul(parent->q, o->q));
    o->energy = bio_min(inherit_gc->reproduce_energy / (double)(parent->n_children + 1),
                        parent->energy);
    if (!first && parent->energy < o->energy + P.yellow_energy_consumption)
        return false;
    org_create(o);
    org_build_body(o);
    bool ok = org_place_near(o, parent);
    if (ok && !first)
        org_use_energy(parent, P.yellow_energy_consumption);
    return ok;
}

/* ---- living ---- */

/* Rebuild the body at a new growth stage. Kinetic energy is kept, so speeds
 * scale with the change of mass (as in 0.9), within the speed limits. Undoing
 * a step goes through here too, which scales the speeds back. */
static void org_rebuild_keeping_energy(Organism *o)
{
    double m = o->mass, I = o->inertia_trace;
    org_build_body(o);
    o->v = v3_clamp_len(v3_scale(o->v, sqrt(m / o->mass)), P.max_vel);
    o->w = v3_clamp_len(v3_scale(o->w, sqrt(I / o->inertia_trace)), P.max_rot);
}

static void org_grow(Organism *o)
{
    if (o->growth_ratio > 1 && (o->age & 0x07) == 0x07 && o->alive && o->energy >= o->mass / 10) {
        o->growth_ratio--;
        org_rebuild_keeping_energy(o);
        o->has_grown = 1;
    } else if (o->growth_ratio < 15 && o->energy < o->mass / 12) {
        o->growth_ratio++;
        org_rebuild_keeping_energy(o);
        o->has_grown = -1;
    } else {
        o->has_grown = 0;
    }
}

static void org_reproduce(Organism *o)
{
    for (int i = 0; i < bio_between_i(o->n_children, 1, 8); i++) {
        Organism *child = org_new(o->world);
        if (org_inherit(child, o, i == 0)) {
            o->total_children++;
            world_add_organism(o->world, child, o);
            gc_assign(&o->infected_gc, NULL);
        } else {
            org_free(child);
        }
        o->time_to_reproduce = 20;
    }
}

/* Outside the box, the velocity along each side crossed bounces and the
 * spin stops, as 0.9 did at its edges. */
static bool org_is_inside_world(Organism *o)
{
    World *w = o->world;
    bool out = false;
    if (o->lo.x < 0 || o->hi.x >= w->width) {
        o->v.x = -o->v.x;
        out = true;
    }
    if (o->lo.y < 0 || o->hi.y >= w->height) {
        o->v.y = -o->v.y;
        out = true;
    }
    if (o->lo.z < 0 || o->hi.z >= w->depth) {
        o->v.z = -o->v.z;
        out = true;
    }
    if (out)
        o->w = v3(0, 0, 0);
    return !out;
}

static void org_set_color(Organism *o, int c)
{
    o->flash_color = (uint8_t)c;
    o->frames_color = 10;
}

static V3 v3_drop_tiny(V3 a)
{
    if (fabs(a.x) < BIO_TOL)
        a.x = 0;
    if (fabs(a.y) < BIO_TOL)
        a.y = 0;
    if (fabs(a.z) < BIO_TOL)
        a.z = 0;
    return a;
}

static void org_segments_frame_effects(Organism *o)
{
    if (!o->alive)
        return;
    double photosynthesis = 0;
    o->n_children = 1;
    for (int i = o->segments - 1; i >= 0; i--) {
        switch (o->seg_color[i]) {
        case COL_CYAN:
            if (jr_next_int(&g_rng, 100) < 8 && org_use_energy(o, P.cyan_energy_consumption)) {
                /* 0.9's kick: a push along the segment, and a spin of fixed
                 * size and random sense. The spin's axis is the one a push
                 * along the segment would turn the body about; for a flat
                 * body lying level that is 0.9's axis, straight up. */
                V3 seg = v3_sub(o->we[i], o->ws[i]);
                o->v = v3_clamp_len(v3_add(o->v, v3_scale(seg, 12.0 / o->mass)), P.max_vel);
                V3 mid = v3_sub(v3_scale(v3_add(o->ws[i], o->we[i]), 0.5), o->pos);
                V3 up = quat_rotate(o->q, v3(0, 0, 1));
                V3 axis = v3_unit_or(v3_cross(mid, seg), up);
                double kick = bio_random_sign() * o->m[i] * D_PI;
                o->w = v3_clamp_len(v3_add(o->w, org_inv_inertia_mul(o, v3_scale(axis, kick))),
                                    P.max_rot);
            }
            break;
        case COL_GREEN:
            if (org_use_energy(o, P.green_energy_consumption))
                photosynthesis += o->m[i];
            break;
        case COL_YELLOW:
            o->n_children++;
            break;
        }
    }
    o->energy += world_photosynthesis(o->world, photosynthesis);
}

static void org_rubbing(Organism *o)
{
    o->v = v3_drop_tiny(v3_scale(o->v, P.rubbing));
    o->w = v3_drop_tiny(v3_scale(o->w, P.rubbing));
}

static void org_breath(Organism *o)
{
    if (o->alive) {
        o->age++;
        bool can_breath = org_use_energy(o, bio_min(o->mass / P.segment_cost_divisor, o->energy));
        if ((o->age >> 8) > o->gc->max_age || !can_breath) {
            org_die(o, NULL);
        } else if (o->energy <= BIO_TOL) {
            o->alive = false;
            world_organism_has_died(o->world, o, NULL);
        }
    } else {
        /* the corpse slowly decays */
        org_use_energy(o, bio_min(o->energy, P.decay_energy));
    }
}

bool org_move(Organism *o)
{
    World *w = o->world;
    o->has_moved = false;
    o->v = v3_drop_tiny(o->v);
    o->w = v3_drop_tiny(o->w);
    if (o->frames_color > 0)
        o->frames_color--;
    org_segments_frame_effects(o);
    org_rubbing(o);
    org_grow(o);

    o->prev_pos = o->pos;
    o->prev_q = o->q;
    if (o->has_grown != 0 || !v3_is_zero(o->v) || !v3_is_zero(o->w)) {
        o->pos = v3_add(o->pos, o->v);
        o->q = quat_spin(o->q, o->w);
        org_update_geometry(o);
        o->has_moved = true;
        bool collision = !org_is_inside_world(o);
        if (world_check_hit(w, o))
            collision = true;
        if (collision) {
            o->has_moved = false;
            o->pos = o->prev_pos;
            o->q = o->prev_q;
            if (o->has_grown != 0) {
                o->growth_ratio += o->has_grown;
                org_rebuild_keeping_energy(o);
            }
            org_update_geometry(o);
        } else if (!v3_is_zero(o->w)) {
            /* A free body keeps its angular momentum as it turns, so its
             * spin vector follows the turned inertia tensor (a tumbling
             * body precesses). */
            V3 L = tensor_mul(&o->inertia, o->prev_q, o->w);
            o->w = v3_clamp_len(tensor_mul(&o->inv_inertia, o->q, L), P.max_rot);
        }
    }
    if (o->time_to_reproduce > 0)
        o->time_to_reproduce--;
    if (o->energy > o->gc->reproduce_energy + P.yellow_energy_consumption * (o->n_children - 1) &&
        o->growth_ratio == 1 && o->time_to_reproduce == 0 && o->alive)
        org_reproduce(o);
    if (o->energy > 2 * o->gc->reproduce_energy)
        org_use_energy(o, o->energy - 2 * o->gc->reproduce_energy);
    org_breath(o);
    return o->energy > BIO_TOL;
}

bool org_use_energy(Organism *o, double q)
{
    if (o->energy < q)
        return false;
    double respiration = world_respiration(o->world, q);
    o->energy -= respiration;
    return respiration >= q;
}

void org_die(Organism *o, Organism *killer)
{
    if (!o->alive)
        return; /* already dead, e.g. of hunger earlier this frame */
    o->alive = false;
    o->has_moved = true;
    for (int i = 0; i < o->segments; i++)
        o->seg_color[i] = COL_BROWN;
    if (killer)
        killer->total_kills++;
    world_organism_has_died(o->world, o, killer);
}

static void org_infected_by(Organism *o, Organism *infector)
{
    infector->total_infected++;
    gc_assign(&o->infected_gc, infector->gc);
    o->infected_generation = infector->generation;
    world_organism_has_been_infected(o->world, o, infector);
}

/* ---- touching ---- */

/* Organism.touchEffects: what segment seg of a does to segment oseg of b,
 * then (first_call) what b's does back. 0.9's rules, unchanged. */
static void org_touch_effects(Organism *a, Organism *b, int seg, int oseg, bool first_call)
{
    if ((a->parent_id == b->id || a->id == b->parent_id) && b->alive)
        return;
    double taken = 0;
    switch (a->seg_color[seg]) {
    case COL_RED:
        switch (b->seg_color[oseg]) {
        case COL_BLUE:
            if (org_use_energy(b, P.blue_energy_consumption)) {
                org_set_color(b, COL_BLUE);
            } else if (org_use_energy(a, P.red_energy_consumption)) {
                taken = bio_between(a->m[seg] * P.organic_obtained_energy, 0, b->energy);
                org_set_color(b, COL_YELLOW);
            }
            break;
        case COL_RED:
            if (org_use_energy(a, P.red_energy_consumption)) {
                taken = bio_between(a->m[seg] * P.organic_obtained_energy, 0, b->energy);
                org_set_color(b, COL_RED);
            }
            break;
        default:
            if (org_use_energy(a, P.red_energy_consumption)) {
                taken = bio_between(a->m[seg] * P.organic_obtained_energy, 0, b->energy);
                org_set_color(b, COL_YELLOW);
            }
        }
        b->energy -= taken;
        a->energy += taken;
        if (taken > 0)
            world_organism_bitten(a->world, a, b, taken);
        org_use_energy(a, taken * P.organic_subs_produced);
        org_set_color(a, COL_RED);
        break;
    case COL_WHITE:
        switch (b->seg_color[oseg]) {
        case COL_BLUE:
            if (org_use_energy(b, P.blue_energy_consumption)) {
                org_set_color(a, COL_WHITE);
                org_set_color(b, COL_BLUE);
            } else if (b->infected_gc != a->gc) {
                if (org_use_energy(a, P.white_energy_consumption)) {
                    org_infected_by(b, a);
                    org_set_color(b, COL_YELLOW);
                    org_set_color(a, COL_WHITE);
                }
            }
            break;
        case COL_BROWN:
            break;
        default:
            if (b->infected_gc != a->gc) {
                if (org_use_energy(a, P.white_energy_consumption)) {
                    org_infected_by(b, a);
                    org_set_color(b, COL_YELLOW);
                    org_set_color(a, COL_WHITE);
                }
            }
        }
        break;
    case COL_GRAY:
        switch (b->seg_color[oseg]) {
        case COL_BLUE:
            if (org_use_energy(b, P.blue_energy_consumption)) {
                org_set_color(b, COL_BLUE);
                org_set_color(a, COL_GRAY);
            } else if (org_use_energy(a, P.gray_energy_consumption)) {
                org_die(b, a);
                org_set_color(a, COL_GRAY);
            }
            break;
        case COL_BROWN:
            break;
        default:
            if (org_use_energy(a, P.gray_energy_consumption)) {
                org_die(b, a);
                org_set_color(a, COL_GRAY);
            }
        }
        break;
    }
    if (b->alive && b->energy < BIO_TOL)
        org_die(b, a);
    if (first_call)
        org_touch_effects(b, a, oseg, seg, false);
}

/* Closest points of segments p1-q1 and p2-q2 (Ericson, Real-Time Collision
 * Detection, 5.1.9); returns the squared distance between them. */
static double seg_seg_closest(V3 p1, V3 q1, V3 p2, V3 q2, V3 *c1, V3 *c2)
{
    const double eps = 1e-12;
    V3 d1 = v3_sub(q1, p1), d2 = v3_sub(q2, p2), r = v3_sub(p1, p2);
    double a = v3_dot(d1, d1), e = v3_dot(d2, d2), f = v3_dot(d2, r);
    double s, t;
    if (a <= eps && e <= eps) {
        s = t = 0;
    } else if (a <= eps) {
        s = 0;
        t = bio_between(f / e, 0, 1);
    } else {
        double c = v3_dot(d1, r);
        if (e <= eps) {
            t = 0;
            s = bio_between(-c / a, 0, 1);
        } else {
            double b = v3_dot(d1, d2);
            double denom = a * e - b * b;
            s = denom != 0 ? bio_between((b * f - c * e) / denom, 0, 1) : 0;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = bio_between(-c / a, 0, 1);
            } else if (t > 1) {
                t = 1;
                s = bio_between((b - c) / a, 0, 1);
            }
        }
    }
    *c1 = v3_add(p1, v3_scale(d1, s));
    *c2 = v3_add(p2, v3_scale(d2, t));
    V3 d = v3_sub(*c1, *c2);
    return v3_dot(d, d);
}

/* The collision impulse when a, moving, touches b: a's segment ia against
 * b's segment ib, whose axes' closest points are pa and pb.
 *
 * The normal is the direction from a's segment to b's as it was before a's
 * move: after the move a thin pair may already have passed through each
 * other, and the closest points would point the wrong way. (If they were
 * already touching then, the closest points now are used.) The impulse is
 * the textbook rigid-body one with 0.9's elasticity, applied only while the
 * two are closing; 0.9 also pulled separating bodies together. a's growth
 * counts as motion of its surface, as in 0.9. */
static void org_touch_move(Organism *a, Organism *b, int ia_seg, int ib_seg, V3 pa, V3 pb)
{
    M3 R0 = m3_from_quat(a->prev_q);
    V3 s0 = v3_add(a->prev_pos, m3_mul_v(&R0, a->bs[ia_seg]));
    V3 e0 = v3_add(a->prev_pos, m3_mul_v(&R0, a->be[ia_seg]));
    V3 pa0, pb0;
    seg_seg_closest(s0, e0, b->ws[ib_seg], b->we[ib_seg], &pa0, &pb0);
    V3 fallback = v3_unit_or(v3_sub(pb, pa), v3_unit_or(v3_sub(b->pos, a->pos), v3(0, 0, 1)));
    V3 n = v3_unit_or(v3_sub(pb0, pa0), fallback);
    V3 p = v3_scale(v3_add(pa, pb), 0.5);
    V3 ra = v3_sub(p, a->pos), rb = v3_sub(p, b->pos);
    V3 va = v3_add(v3_add(a->v, v3_cross(a->w, ra)), v3_scale(ra, a->has_grown / 10.0));
    V3 vb = v3_add(b->v, v3_cross(b->w, rb));
    double vn = v3_dot(v3_sub(va, vb), n);
    if (vn <= 0)
        return;
    V3 ia = org_inv_inertia_mul(a, v3_cross(ra, n));
    V3 ib = org_inv_inertia_mul(b, v3_cross(rb, n));
    double k = 1 / a->mass + 1 / b->mass + v3_dot(v3_cross(ia, ra), n) +
               v3_dot(v3_cross(ib, rb), n);
    double j = (1 + P.elasticity) * vn / k;
    a->v = v3_clamp_len(v3_sub(a->v, v3_scale(n, j / a->mass)), P.max_vel);
    b->v = v3_clamp_len(v3_add(b->v, v3_scale(n, j / b->mass)), P.max_vel);
    a->w = v3_clamp_len(v3_sub(a->w, v3_scale(ia, j)), P.max_rot);
    b->w = v3_clamp_len(v3_add(b->w, v3_scale(ib, j)), P.max_rot);
}

static bool box_overlaps_segment(V3 lo, V3 hi, V3 a, V3 b, double grow)
{
    return bio_min(a.x, b.x) - grow <= hi.x && bio_max(a.x, b.x) + grow >= lo.x &&
           bio_min(a.y, b.y) - grow <= hi.y && bio_max(a.y, b.y) + grow >= lo.y &&
           bio_min(a.z, b.z) - grow <= hi.z && bio_max(a.z, b.z) + grow >= lo.z;
}

/* Organism.contact: the first pair of touching segments, searched from the
 * last segments back as 0.9 did. Segments shorter than 1 (a newborn's) do
 * not touch, as in 0.9. Two capsules touch when their axes come closer
 * than two segment radii. */
bool org_contact(Organism *o, Organism *org)
{
    double r = P.segment_radius;
    double reach2 = 4 * r * r;
    uint8_t stackmemo[512];
    uint8_t *memo = org->segments <= 512 ? stackmemo : calloc((size_t)org->segments, 1);
    if (!memo)
        abort();
    if (memo == stackmemo)
        memset(memo, 0, (size_t)org->segments);
    bool touched = false;
    for (int i = o->segments - 1; i >= 0 && !touched; i--) {
        if (!(o->m[i] >= 1))
            continue;
        if (!box_overlaps_segment(org->lo, org->hi, o->ws[i], o->we[i], r))
            continue;
        for (int j = org->segments - 1; j >= 0; j--) {
            if (!(org->m[j] >= 1))
                continue;
            if (!memo[j])
                memo[j] = box_overlaps_segment(o->lo, o->hi, org->ws[j], org->we[j], r) ? 1 : 2;
            if (memo[j] != 1)
                continue;
            V3 pa, pb;
            if (seg_seg_closest(o->ws[i], o->we[i], org->ws[j], org->we[j], &pa, &pb) < reach2) {
                org_touch_effects(o, org, i, j, true);
                org_touch_move(o, org, i, j, pa, pb);
                touched = true;
                break;
            }
        }
    }
    if (memo != stackmemo)
        free(memo);
    return touched;
}
