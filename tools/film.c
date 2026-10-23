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

/* Films the headless world: runs it and writes raw RGB video frames to
 * stdout, for ffmpeg (tools/film.sh), drawn by tools/raster.c from a camera
 * that orbits the box or follows the life in it. tools/view shows the same
 * picture live in a window. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "raster.h"
#include "sim/params.h"

static void write_caption(const char *path, const World *w, long long seed)
{
    if (!path)
        return;
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *cf = fopen(tmp, "w");
    if (!cf)
        return;
    int flat = 0;
    for (int i = 0; i < w->norgs; i++)
        flat += w->orgs[i]->alive && gc_is_flat(w->orgs[i]->gc);
    fprintf(cf, "SYN-BIOGENESIS-3D   seed %lld   %d x %d x %d\n", seed, w->width, w->height,
            w->depth);
    fprintf(cf, "time %lld   alive %d   dead %d   flat %d%%   kills %lld   bites %lld",
            (long long)(w->frame / 256), w->population, world_n_corpses(w),
            w->population ? 100 * flat / w->population : 0, (long long)w->counts.kills,
            (long long)w->counts.bites);
    fclose(cf);
    rename(tmp, path);
}

static void usage(void)
{
    fputs("usage: film [options] > frames.rgb (raw rgb24; tools/film.sh makes a video)\n"
          "  --seed N --size W H D --organisms N --co2 X --pitch P --radius R\n"
          "  --import FILE --copies K     as syn-biogenesis-3d\n"
          "  --warm UNITS     time units to run before filming (default 30)\n"
          "  --seconds S      length of the film at 30 frames a second (default 40)\n"
          "  --steps K        world frames per film frame (default 3)\n"
          "  --frame W H      picture size (default 1280 720)\n"
          "  --orbit DEG      camera turn per second (default 6)\n"
          "  --elevation DEG  camera height angle (default 28)\n"
          "  --distance D     camera distance (default: fits the box)\n"
          "  --follow         aim at the living instead of the box's centre\n"
          "  --caption FILE   keep FILE holding the frame's caption (for ffmpeg)\n",
          stderr);
}

int main(int argc, char **argv)
{
    params_set_defaults(&P);
    long long seed = 1;
    int warm = 30, seconds = 40, steps = 3, copies = 1, width = 1280, height = 720;
    double orbit = 6, elevation = 28, distance = 0;
    bool follow = false;
    const char *import = NULL, *caption = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--seed") && more)
            seed = atoll(argv[++i]);
        else if (!strcmp(a, "--size") && i + 3 < argc) {
            P.world_width = atoi(argv[++i]);
            P.world_height = atoi(argv[++i]);
            P.world_depth = atoi(argv[++i]);
        } else if (!strcmp(a, "--organisms") && more)
            P.initial_organisms = atoi(argv[++i]);
        else if (!strcmp(a, "--co2") && more)
            P.initial_co2 = atof(argv[++i]);
        else if (!strcmp(a, "--pitch") && more)
            P.pitch_prob = atoi(argv[++i]);
        else if (!strcmp(a, "--radius") && more)
            P.segment_radius = atof(argv[++i]);
        else if (!strcmp(a, "--import") && more)
            import = argv[++i];
        else if (!strcmp(a, "--copies") && more)
            copies = atoi(argv[++i]);
        else if (!strcmp(a, "--warm") && more)
            warm = atoi(argv[++i]);
        else if (!strcmp(a, "--seconds") && more)
            seconds = atoi(argv[++i]);
        else if (!strcmp(a, "--steps") && more)
            steps = atoi(argv[++i]);
        else if (!strcmp(a, "--frame") && i + 2 < argc) {
            width = atoi(argv[++i]);
            height = atoi(argv[++i]);
        } else if (!strcmp(a, "--orbit") && more)
            orbit = atof(argv[++i]);
        else if (!strcmp(a, "--elevation") && more)
            elevation = atof(argv[++i]);
        else if (!strcmp(a, "--distance") && more)
            distance = atof(argv[++i]);
        else if (!strcmp(a, "--follow"))
            follow = true;
        else if (!strcmp(a, "--caption") && more)
            caption = argv[++i];
        else {
            usage();
            return 2;
        }
    }

    jr_set_seed(&g_rng, seed);
    World w;
    world_init(&w);
    world_genesis(&w);
    if (import) {
        char err[512];
        GeneticCode *gc = bgg_load(import, err, sizeof err);
        if (!gc) {
            fprintf(stderr, "film: %s\n", err);
            return 1;
        }
        world_add_founders(&w, gc, copies);
        gc_unref(gc);
    }
    for (long f = 0; f < (long)warm * 256; f++)
        world_time(&w);

    Frame fr;
    if (!frame_init(&fr, width, height))
        abort();
    V3 centre = v3(w.width / 2.0, w.height / 2.0, w.depth / 2.0);
    if (distance <= 0)
        distance = 1.25 * v3_len(v3(w.width, w.height, w.depth));
    V3 aim = centre;
    Stroke *buf = NULL;
    int cap = 0;
    int nframes = seconds * 30;
    for (int k = 0; k < nframes; k++) {
        if (k > 0)
            for (int s = 0; s < steps; s++)
                world_time(&w);
        if (follow && w.population > 0) {
            V3 target = world_life_centre_or_box(&w);
            aim = k == 0 ? target : v3_add(aim, v3_scale(v3_sub(target, aim), 0.02));
        }
        Camera cam;
        camera_orbit(&cam, aim, -60 + orbit * k / 30.0, elevation, distance, height);
        draw_world(&fr, &cam, &w, &buf, &cap);
        write_caption(caption, &w, seed);
        frame_to_bytes(&fr);
        fwrite(fr.out, 1, (size_t)width * height * 3, stdout);
    }
    fflush(stdout);
    free(buf);
    frame_free(&fr);
    world_free(&w);
    return 0;
}
