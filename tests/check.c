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
 *   - d_sin/d_cos agree with the C library to the last bit or two;
 *   - a flat (0.9) genetic code builds 0.9's body, exactly in the plane;
 *   - bodies keep their symmetry and their mirror images in 3D;
 *   - inertia tensors invert;
 *   - the carbon and energy books balance on every frame, in thin slabs,
 *     deep slabs and cubes, with flat and bent life;
 *   - a seed run twice gives the same world.
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
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
    double carbon = w.o2 + w.co2;
    for (int f = 0; f < frames; f++) {
        world_time(&w);
        double bodies = world_energy_in_bodies(&w);
        double tol = 1e-6 * (1 + carbon);
        if (fabs(bodies + w.lost - w.o2) > tol || fabs(w.o2 + w.co2 - carbon) > tol) {
            CHECK(0, "seed %" PRId64 " %dx%dx%d pitch %d frame %" PRId64
                     ": bodies+lost %.9f, O2 %.9f, O2+CO2 %.9f of %.9f",
                  seed, width, height, depth, pitch, w.frame, bodies + w.lost, w.o2,
                  w.o2 + w.co2, carbon);
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
        {1000, 1000, 4, 0},    /* a slab one body thick: flat life, 2D in all but name */
        {1000, 1000, 32, 50},  /* the default */
        {1000, 1000, 256, 50}, /* deep water */
        {317, 317, 317, 100},  /* a cube with the default's volume, every new gene bent */
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

/* A code written as a .bgg and read back is the same code, bends and all;
 * a flat one is written exactly as 0.9 writes them (no pitch anywhere). */
static void check_bgg(void)
{
    const char *path = "tests/roundtrip.bgg";
    for (int t = 0; t < 50; t++) {
        GeneticCode *gc = random_code(1 + t % 8, t % 2, 1 + t % 9, t < 25 ? 0 : 60);
        char err[256];
        CHECK(bgg_save(path, gc, err, sizeof err), "save: %s", err);
        GeneticCode *back = bgg_load(path, err, sizeof err);
        CHECK(back != NULL, "load: %s", err);
        if (back) {
            bool same = back->ngenes == gc->ngenes && back->symmetry == gc->symmetry &&
                        back->mirror == gc->mirror &&
                        back->disperse_children == gc->disperse_children;
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
        gc_unref(gc);
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
    check_worlds();
    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
