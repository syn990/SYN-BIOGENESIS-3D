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

/* The simulation's own checks:
 *   - d_sin, d_cos and d_exp agree with the C library to the last bit or two;
 *   - a flat (0.9) genetic code builds 0.9's body, exactly in the plane;
 *   - bodies keep their symmetry and their mirror images in 3D;
 *   - inertia tensors invert;
 *   - the water: light falls off with depth and in a body's shadow, the
 *     dissolved CO2 spreads without losing any, the dead sink to the floor
 *     and rest there, children bud off in all 26 directions alike;
 *   - the senses: a body is seen in the light and not in the dark, a corpse
 *     is smelled in either;
 *   - the carbon and energy books balance on every frame, the water's
 *     cells holding all the CO2, in thin and deep water and in cubes, with
 *     flat and bent life;
 *   - a seed run twice gives the same world.
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "io/worldfile.h"
#include "sim/params.h"
#include "sim/world.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void check_trig(void)
{
    double worst = 0;
    JRandom r;
    jr_set_seed(&r, 7);
    for (int i = 0; i < 200000; i++) {
        double x = (jr_next_double(&r) - 0.5) * (i < 100000 ? 20 : 4000);
        worst = fmax(worst, fabs(d_sin(x) - sin(x)));
        worst = fmax(worst, fabs(d_cos(x) - cos(x)));
    }
    CHECK(worst < 4e-16, "d_sin/d_cos differ from libm by %.3g", worst);
    CHECK(d_sin(0) == 0 && d_cos(0) == 1, "sin 0 / cos 0");
    worst = 0;
    for (int i = 0; i < 200000; i++) {
        double x = (jr_next_double(&r) - 0.9) * (i < 100000 ? 2 : 700);
        worst = fmax(worst, fabs(d_exp(x) - exp(x)) / exp(x));
    }
    CHECK(worst < 3e-16, "d_exp differs from libm by %.3g of it", worst);
    CHECK(d_exp(0) == 1 && d_exp(-1000) == 0, "exp 0 / exp -1000");
}

static GeneticCode *random_code(int symmetry, int mirror, int ngenes, int pitch_prob)
{
    int saved = P.pitch_prob;
    P.pitch_prob = pitch_prob;
    Gene genes[16];
    for (int i = 0; i < ngenes; i++)
        gene_randomize(&genes[i]);
    P.pitch_prob = saved;
    return gc_new_from_genes(genes, ngenes, symmetry, mirror, false);
}

static Organism *built(World *w, GeneticCode *gc, int growth)
{
    Organism *o = org_new(w);
    o->q = quat_identity();
    o->growth_ratio = growth;
    org_place_with_code(o, gc); /* builds the body; placement may fail, no matter */
    o->growth_ratio = growth;
    org_build_body(o);
    return o;
}

/* 0.9's GeneticCode.draw: the flat body by summed angles, in doubles. */
static void flat_reference(const GeneticCode *gc, double *x, double *y)
{
    int seg = 0;
    for (int i = 0; i < gc->symmetry; i++) {
        double th = 0, px = 0, py = 0;
        for (int j = 0; j < gc->ngenes; j++, seg++) {
            const Gene *g = &gc->genes[j];
            if (j == 0)
                th = (gc->mirror == 0 || i % 2 == 0) ? g->theta + i * 2 * D_PI / gc->symmetry
                                                     : D_PI - (g->theta + (i - 1) * 2 * D_PI /
                                                                              gc->symmetry);
            else
                th += (gc->mirror == 0 || i % 2 == 0) ? g->theta : -g->theta;
            px += g->length * d_cos(th);
            py += g->length * d_sin(th);
            x[seg] = px;
            y[seg] = py;
        }
    }
}

static void check_bodies(void)
{
    World w;
    world_init(&w);
    for (int t = 0; t < 400; t++) {
        int sym = 1 + t % 8, mirror = (t / 8) % 2, ng = 1 + t % 7;
        /* flat: 0.9's body exactly in the plane z = 0 */
        GeneticCode *gc = random_code(sym, mirror, ng, 0);
        Organism *o = built(&w, gc, 1);
        double x[128], y[128];
        flat_reference(gc, x, y);
        double worst = 0;
        bool planar = true;
        V3 root = o->bs[0];
        for (int s = 0; s < o->segments; s++) {
            planar = planar && o->bs[s].z == 0 && o->be[s].z == 0;
            worst = fmax(worst, fabs(o->be[s].x - root.x - x[s]));
            worst = fmax(worst, fabs(o->be[s].y - root.y - y[s]));
        }
        CHECK(planar, "flat code %d left the plane", t);
        CHECK(worst < 1e-9, "flat code %d differs from 0.9's body by %.3g", t, worst);
        org_free(o);
        gc_unref(gc);

        /* bent: arm i+1 is arm i turned by 2pi/symmetry about the axis */
        gc = random_code(sym, 0, ng, 100);
        o = built(&w, gc, 1);
        Quat turn = quat_axis_angle(v3(0, 0, 1), 2 * D_PI / sym);
        root = o->bs[0];
        worst = 0;
        for (int s = 0; s + ng < o->segments; s++) {
            V3 a = quat_rotate(turn, v3_sub(o->be[s], root));
            V3 b = v3_sub(o->be[s + ng], root);
            worst = fmax(worst, v3_len(v3_sub(a, b)));
        }
        CHECK(worst < 1e-9, "bent code %d broke its %d-fold symmetry by %.3g", t, sym, worst);
        org_free(o);
        gc_unref(gc);

        /* mirrored and bent: arm 1 is arm 0 reflected in the plane x = 0 */
        if (sym >= 2) {
            gc = random_code(sym, 1, ng, 100);
            o = built(&w, gc, 1);
            root = o->bs[0];
            worst = 0;
            for (int s = 0; s < ng; s++) {
                V3 a = v3_sub(o->be[s], root);
                V3 b = v3_sub(o->be[s + ng], root);
                worst = fmax(worst, v3_len(v3_sub(v3(-a.x, a.y, a.z), b)));
            }
            CHECK(worst < 1e-9, "mirrored code %d is not its own reflection (%.3g)", t, worst);
            org_free(o);
            gc_unref(gc);
        }

        /* the inertia tensor inverts and is positive */
        gc = random_code(sym, mirror, ng, 50);
        o = built(&w, gc, 1 + t % 16);
        bool finite = true;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                finite = finite && isfinite(o->inv_inertia.m[r][c]);
        CHECK(finite && o->inv_inertia.m[0][0] > 0 && o->inv_inertia.m[1][1] > 0 &&
                  o->inv_inertia.m[2][2] > 0,
              "code %d has a bad inertia tensor", t);
        org_free(o);
        gc_unref(gc);
    }
    world_free(&w);
}

static void water_world(World *w, int width, int height, int depth)
{
    params_set_defaults(&P);
    P.world_width = width;
    P.world_height = height;
    P.world_depth = depth;
    P.initial_organisms = 0;
    P.day_length = 0; /* always noon, unless a check says otherwise */
    jr_set_seed(&g_rng, 3);
    world_init(w);
    world_genesis(w);
}

/* One copy of gc placed at p, lying level, as an adult, its energy taken
 * from the water as a founder's is. */
static Organism *put(World *w, GeneticCode *gc, V3 p)
{
    Organism *o = org_new_bare(w, gc);
    o->q = quat_identity();
    o->growth_ratio = 1;
    o->parent_id = -1;
    o->id = world_new_id(w);
    o->founder = o->id;
    org_build_body(o);
    o->pos = p;
    org_update_geometry(o);
    world_take_co2(w, 100);
    world_add_o2(w, 100);
    o->energy = 100;
    o->has_moved = true;
    world_add_organism(w, o, NULL);
    return o;
}

/* Keep o alive, young and short of a child, its energy from the water as
 * a founder's is, so the books still balance. */
static void feed(World *w, Organism *o)
{
    double e = o->gc->reproduce_energy * 0.9 - o->energy;
    if (e > 0) {
        world_take_co2(w, e);
        world_add_o2(w, e);
        o->energy += e;
    }
    o->age = 0;
    o->time_to_reproduce = 20; /* leaves would make it a parent within the frame */
}

static GeneticCode *code_of(int color, double plants)
{
    Gene g[2] = {{12, 0, 0, (uint8_t)color, 0}, {10, 0.7, 0, (uint8_t)color, 0}};
    GeneticCode *gc = gc_new_from_genes(g, 2, 3, 0, false);
    gc->drive[DRIVE_PLANTS] = plants;
    return gc;
}

static void check_water(void)
{
    World w;
    /* the light: e^-1 one light depth down, and less in a leaf's shadow */
    water_world(&w, 256, 256, 256);
    world_prepare(&w);
    double l1 = world_light_at(&w, v3(100, 100, 256 - P.light_depth));
    CHECK(fabs(l1 - exp(-1)) < 1e-15, "light one light depth down: %.17g", l1);
    GeneticCode *leaf = code_of(COL_GREEN, 0);
    put(&w, leaf, v3(48, 48, 240));
    world_prepare(&w);
    int layer = w.fx * w.fy, top = (w.fz - 1) * layer, dark = 0;
    for (int c = 0; c < layer; c++)
        if (w.shade_cell[top + c] > w.shade_cell[top + dark])
            dark = c;
    V3 below = v3((dark % w.fx + 0.5) * w.fsx, (dark / w.fx + 0.5) * w.fsy, 150);
    double under = world_light_at(&w, below), beside = world_light_at(&w, v3(208, 208, 150));
    CHECK(w.shade_cell[top + dark] > 0 && under < beside * 0.99 && beside > 0,
          "no shadow under a leaf: %.4f vs %.4f", under, beside);
    world_free(&w);

    /* the day: half light at sunrise, full at noon, none at midnight */
    water_world(&w, 64, 64, 64);
    P.day_length = 8;
    double seen[3];
    for (int k = 0; k < 3; k++) {
        w.frame = (int64_t)(k == 0 ? 0 : k == 1 ? 2 : 6) * 256;
        world_prepare(&w);
        seen[k] = w.daylight;
    }
    CHECK(fabs(seen[0] - 0.5) < 1e-12 && fabs(seen[1] - 1) < 1e-12 && fabs(seen[2]) < 1e-12,
          "the day's light at sunrise, noon and midnight: %.3f %.3f %.3f", seen[0], seen[1], seen[2]);
    world_free(&w);

    /* the CO2 spreads, the total kept */
    water_world(&w, 300, 200, 160);
    w.co2_cell[0] += 5000;
    w.co2 += 5000;
    double before = 0;
    for (int i = 0; i < world_n_cells(&w); i++)
        before += w.co2_cell[i];
    double corner = w.co2_cell[0];
    for (int f = 0; f < 3000; f++)
        world_time(&w);
    double after = 0;
    for (int i = 0; i < world_n_cells(&w); i++)
        after += w.co2_cell[i];
    CHECK(fabs(after - before) < 1e-9 * before, "spreading lost CO2: %.12g to %.12g", before, after);
    CHECK(w.co2_cell[0] < corner * 0.5, "the CO2 did not spread: %.3f of %.3f left",
          w.co2_cell[0], corner);
    world_free(&w);

    /* the dead sink to the floor and rest there */
    water_world(&w, 200, 200, 300);
    GeneticCode *body = code_of(COL_RED, 0);
    Organism *o = put(&w, body, v3(100, 100, 250));
    org_die(o, NULL);
    for (int f = 0; f < 3000 && o->lo.z > 1.5; f++)
        world_time(&w);
    CHECK(o->lo.z <= 1.5, "a corpse is still at %.1f above the floor", o->lo.z);
    for (int f = 0; f < 50; f++)
        world_time(&w);
    CHECK(o->lo.z >= 0 && o->lo.z <= 1.5 && fabs(o->v.z) < 0.05, "a corpse on the floor at %.2f, "
          "moving %.3f", o->lo.z, o->v.z);
    world_free(&w);

    /* the dead rot into methane where they lie, and a purple body there
     * lives on it, in the dark as in the light */
    water_world(&w, 300, 300, 300);
    P.light_depth = 1; /* the floor black */
    GeneticCode *meat = code_of(COL_RED, 0);
    Organism *dead = put(&w, meat, v3(150, 150, 12));
    org_die(dead, NULL);
    for (int f = 0; f < 200; f++)
        world_time(&w);
    int here = world_cell_of(&w, dead->pos);
    double most = 0;
    for (int i = 0; i < world_n_cells(&w); i++)
        most = w.ch4_cell[i] > most ? w.ch4_cell[i] : most;
    CHECK(w.ch4 > 0 && w.ch4_cell[here] == most, "a corpse's methane: %.4f in all, %.4f in its cell, the most %.4f",
          w.ch4, w.ch4_cell[here], most);
    GeneticCode *eater = code_of(COL_PURPLE, 0);
    eater->height = 0.04;
    Organism *pe = put(&w, eater, v3(140, 140, 12));
    /* below what a child takes, so it keeps what it makes */
    pe->time_to_reproduce = 1000;
    double had = pe->energy;
    for (int f = 0; f < 50; f++)
        world_time(&w);
    CHECK(pe->energy > had, "a purple body by a corpse in the dark made nothing: %.4f to %.4f", had,
          pe->energy);
    world_free(&w);
    gc_unref(meat);
    gc_unref(eater);

    /* children bud off in all 26 directions alike */
    water_world(&w, 400, 400, 400);
    Organism *parent = put(&w, leaf, v3(200, 200, 200));
    int count[27] = {0};
    for (int t = 0; t < 5200; t++) {
        parent->energy = 1000;
        Organism *c = org_new(&w);
        if (org_inherit(c, parent, true)) {
            V3 d = v3_sub(c->pos, parent->pos);
            int k = (d.x > 1 ? 2 : d.x < -1 ? 0 : 1) * 9 + (d.y > 1 ? 2 : d.y < -1 ? 0 : 1) * 3 +
                    (d.z > 1 ? 2 : d.z < -1 ? 0 : 1);
            count[k]++;
        }
        org_free(c);
    }
    int lo = 1 << 30, hi = 0;
    for (int k = 0; k < 27; k++)
        if (k != 13) {
            lo = count[k] < lo ? count[k] : lo;
            hi = count[k] > hi ? count[k] : hi;
        }
    CHECK(count[13] == 0 && lo > 140 && hi < 260, "children's directions: %d to %d of 200", lo, hi);
    world_free(&w);

    /* the senses: a hungry hunter sees a plant in the light, not in the
     * dark, and smells a corpse in either; a fed one seeks nothing */
    GeneticCode *hunter = code_of(COL_RED, 1);
    for (int dark = 0; dark < 2; dark++) {
        water_world(&w, 400, 400, 600);
        double z = dark ? 60 : 560;
        /* at the height its line keeps, so nothing pulls it up or down */
        hunter->height = z / 600;
        Organism *h = put(&w, hunter, v3(150, 200, z));
        Organism *p = put(&w, leaf, v3(200, 200, z));
        h->energy = 5;
        world_prepare(&w);
        org_sense(h);
        if (dark)
            CHECK(v3_is_zero(h->aim), "a hunter saw a plant in the dark (light %.3f)", p->lit);
        else
            CHECK(h->aim.x > 0 && fabs(h->aim.y) + fabs(h->aim.z) < 1e-9,
                  "a hunter in the light did not turn to a plant: %.2f %.2f %.2f",
                  h->aim.x, h->aim.y, h->aim.z);
        hunter->drive[DRIVE_DEAD] = 1;
        hunter->drive[DRIVE_PLANTS] = 0;
        org_die(p, NULL);
        world_prepare(&w);
        org_sense(h);
        CHECK(h->aim.x > 0 && fabs(h->aim.y) + fabs(h->aim.z) < 1e-9,
              "a hunter did not smell a corpse (%s)", dark ? "dark" : "light");
        h->energy = h->gc->reproduce_energy;
        org_sense(h);
        CHECK(v3_is_zero(h->aim), "a fed hunter still sought a corpse");
        hunter->drive[DRIVE_DEAD] = 0;
        hunter->drive[DRIVE_PLANTS] = 1;
        world_free(&w);
    }
    gc_unref(hunter);

    /* the float: a body without cilia whose line keeps to 0.3 of the
     * water rises from the floor and sinks from the surface to it */
    double kept = leaf->height;
    leaf->height = 0.3;
    for (int from = 0; from < 2; from++) {
        water_world(&w, 300, 300, 600);
        Organism *f = put(&w, leaf, v3(150, 150, from ? 590 : 60));
        for (int t = 0; t < 40000; t++) {
            feed(&w, f);
            world_time(&w);
        }
        CHECK(fabs(f->pos.z - 180) < 25, "a float from %s settled at %.0f, not about 180",
              from ? "the surface" : "the floor", f->pos.z);
        world_free(&w);
    }
    leaf->height = kept;

    /* a swimmer turns to face its aim: a single straight cilium, aimed
     * straight up, comes round from lying level to pointing up */
    Gene g1[1] = {{16, 0, 0, COL_CYAN, 0}};
    GeneticCode *swim = gc_new_from_genes(g1, 1, 1, 0, false);
    swim->height = 1;
    water_world(&w, 400, 400, 900);
    P.rise = 0;
    Organism *sw = put(&w, swim, v3(200, 200, 100));
    for (int t = 0; t < 6000; t++) {
        feed(&w, sw);
        world_time(&w);
    }
    V3 u = v3_unit_or(v3_sub(sw->we[0], sw->ws[0]), v3(0, 0, 0));
    CHECK(fabs(u.z) > 0.9 && sw->pos.z > 300, "a swimmer did not turn up and swim up: heading z %.2f, "
          "at %.0f", u.z, sw->pos.z);
    world_free(&w);
    gc_unref(swim);
    gc_unref(body);
    gc_unref(leaf);
}

/* Run a world, checking both books after every frame. Returns its hash. */
static uint64_t run_world(int64_t seed, int width, int height, int depth, int pitch, int frames,
                          int *pop)
{
    params_set_defaults(&P);
    P.world_width = width;
    P.world_height = height;
    P.world_depth = depth;
    P.pitch_prob = pitch;
    jr_set_seed(&g_rng, seed);
    World w;
    world_init(&w);
    world_genesis(&w);
    double carbon = w.o2 + w.co2 + w.ch4;
    for (int f = 0; f < frames; f++) {
        world_time(&w);
        double bodies = world_energy_in_bodies(&w);
        double tol = 1e-6 * (1 + carbon);
        double water = 0, methane = 0;
        for (int i = 0; i < world_n_cells(&w); i++) {
            water += w.co2_cell[i];
            methane += w.ch4_cell[i];
        }
        if (fabs(bodies + w.lost - w.o2) > tol || fabs(w.o2 + w.co2 + w.ch4 - carbon) > tol ||
            fabs(water - w.co2) > tol || fabs(methane - w.ch4) > tol || w.ch4 < -tol) {
            CHECK(0, "seed %" PRId64 " %dx%dx%d pitch %d frame %" PRId64
                     ": bodies+lost %.9f, O2 %.9f, O2+CO2+CH4 %.9f of %.9f, the water %.9f of %.9f, "
                     "its methane %.9f of %.9f",
                  seed, width, height, depth, pitch, w.frame, bodies + w.lost, w.o2,
                  w.o2 + w.co2 + w.ch4, carbon, water, w.co2, methane, w.ch4);
            break;
        }
        CHECK(w.lost < 1e-4, "lost energy grew to %.3g", w.lost);
    }
    uint64_t h = world_hash(&w);
    if (pop)
        *pop = w.population;
    world_free(&w);
    return h;
}

static void check_worlds(void)
{
    static const struct {
        int w, h, d, pitch;
    } k[] = {
        {1000, 1000, 8, 0},   /* a slab a body thick: flat life, 2D in all but name */
        {1000, 1000, 64, 50}, /* a pond */
        {600, 600, 300, 50},  /* the default */
        {400, 400, 400, 100}, /* a cube, every new gene bent */
        {200, 200, 900, 50},  /* a deep column, mostly dark */
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        int pop = 0;
        uint64_t a = run_world(11 + (int64_t)i, k[i].w, k[i].h, k[i].d, k[i].pitch, 8000, &pop);
        uint64_t b = run_world(11 + (int64_t)i, k[i].w, k[i].h, k[i].d, k[i].pitch, 8000, NULL);
        CHECK(a == b, "%dx%dx%d: the same seed gave two worlds", k[i].w, k[i].h, k[i].d);
        printf("  %4d x %4d x %4d pitch %3d: books balanced for 8000 frames, %d alive\n", k[i].w,
               k[i].h, k[i].d, k[i].pitch, pop);
    }
}

/* A code written as a .bgg and read back is the same code, bends and
 * drives and all; a flat one without drives is written exactly as 0.9
 * writes them (no pitch, no drives), and read back with the drives its body
 * asks for, as a 0.9 being is. */
static void check_bgg(void)
{
    const char *path = "tests/roundtrip.bgg";
    for (int t = 0; t < 50; t++) {
        GeneticCode *gc = random_code(1 + t % 8, t % 2, 1 + t % 9, t < 25 ? 0 : 60);
        if (t % 3 == 2)
            for (int d = 0; d < DRIVE_COUNT; d++)
                gc->drive[d] = jr_next_double(&g_rng) * 2 - 1;
        char err[256];
        bool steered = gc_steers(gc);
        CHECK(bgg_save(path, gc, err, sizeof err), "save: %s", err);
        GeneticCode *back = bgg_load(path, err, sizeof err);
        CHECK(back != NULL, "load: %s", err);
        if (!steered)
            gc_instincts(gc);
        if (back) {
            bool same = back->ngenes == gc->ngenes && back->symmetry == gc->symmetry &&
                        back->mirror == gc->mirror &&
                        back->disperse_children == gc->disperse_children;
            for (int d = 0; same && d < DRIVE_COUNT; d++)
                same = back->drive[d] == gc->drive[d];
            for (int i = 0; same && i < gc->ngenes; i++)
                same = back->genes[i].length == gc->genes[i].length &&
                       back->genes[i].theta == gc->genes[i].theta &&
                       back->genes[i].pitch == gc->genes[i].pitch &&
                       back->genes[i].color == gc->genes[i].color;
            CHECK(same, "code %d changed on its way through a .bgg", t);
            gc_unref(back);
        }
        FILE *f = fopen(path, "r");
        char text[8192] = "";
        size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        text[n] = 0;
        if (f)
            fclose(f);
        CHECK(gc_is_flat(gc) == (strstr(text, "pitch") == NULL),
              "code %d: pitch written %s", t, gc_is_flat(gc) ? "for a flat code" : "missing");
        CHECK(steered == (strstr(text, "light=") != NULL),
              "code %d: drives written %s", t, steered ? "missing" : "for a 0.9 code");
        gc_unref(gc);
    }
    remove(path);
}

/* A world saved part-way and loaded goes on exactly as the one that never
 * stopped: the same fingerprint frame for frame, in every kind of world. */
static void check_worldfile(void)
{
    static const struct {
        int w, h, d, pitch;
    } k[] = {{1000, 1000, 8, 0}, {600, 600, 300, 50}, {400, 400, 400, 100}};
    const char *path = "tests/roundtrip.b3w";
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        params_set_defaults(&P);
        P.world_width = k[i].w;
        P.world_height = k[i].h;
        P.world_depth = k[i].d;
        P.pitch_prob = k[i].pitch;
        jr_set_seed(&g_rng, 77 + (int64_t)i);
        World a;
        world_init(&a);
        world_genesis(&a);
        for (int f = 0; f < 3000; f++)
            world_time(&a);
        char err[256];
        CHECK(worldfile_save(&a, path, err, sizeof err), "save: %s", err);
        uint64_t rng_then = g_rng.seed;
        Params p_then = P;
        for (int f = 0; f < 3000; f++)
            world_time(&a);
        uint64_t straight = world_hash(&a);
        world_free(&a);

        /* somewhere else entirely, then the file */
        params_set_defaults(&P);
        jr_set_seed(&g_rng, 1);
        World b;
        world_init(&b);
        world_genesis(&b);
        CHECK(worldfile_load(&b, path, err, sizeof err), "load: %s", err);
        CHECK(g_rng.seed == rng_then && memcmp(&P, &p_then, sizeof P) == 0,
              "the file did not bring back the dice and the parameters");
        for (int f = 0; f < 3000; f++)
            world_time(&b);
        CHECK(world_hash(&b) == straight, "%dx%dx%d: the loaded world went another way", k[i].w,
              k[i].h, k[i].d);
        printf("  %4d x %4d x %4d: saved at frame 3000, loaded, the same at 6000\n", k[i].w,
               k[i].h, k[i].d);
        world_free(&b);
    }
    remove(path);
}

int main(void)
{
    params_set_defaults(&P);
    jr_set_seed(&g_rng, 1);
    check_trig();
    check_bodies();
    check_bgg();
    check_water();
    check_worldfile();
    check_worlds();
    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
