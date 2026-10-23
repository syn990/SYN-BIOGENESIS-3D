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

/* A live look at a world, until the real renderer (step 2) exists: the
 * world runs in an SDL window, drawn by tools/raster.c on the CPU.
 *
 *   drag          turn the camera around the world
 *   wheel         closer / further
 *   space         pause
 *   up / down     faster / slower (world frames per picture)
 *   f             follow the life / look at the box's centre
 *   n             a new world (the next seed)
 *   esc, q        quit
 *
 * The title bar has the seed, time, the living, the dead and the speed. */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/bgg.h"
#include "raster.h"
#include "sim/params.h"

typedef struct {
    long long seed;
    int warm, copies;
    const char *import;
} Setup;

static bool genesis(World *w, const Setup *s)
{
    jr_set_seed(&g_rng, s->seed);
    world_free(w);
    world_init(w);
    world_genesis(w);
    if (s->import) {
        char err[512];
        GeneticCode *gc = bgg_load(s->import, err, sizeof err);
        if (!gc) {
            fprintf(stderr, "view: %s\n", err);
            return false;
        }
        world_add_founders(w, gc, s->copies);
        gc_unref(gc);
    }
    for (long f = 0; f < (long)s->warm * 256; f++)
        world_time(w);
    return true;
}

int main(int argc, char **argv)
{
    params_set_defaults(&P);
    Setup setup = {1, 20, 1, NULL};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--seed") && more)
            setup.seed = atoll(argv[++i]);
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
            setup.import = argv[++i];
        else if (!strcmp(a, "--copies") && more)
            setup.copies = atoi(argv[++i]);
        else if (!strcmp(a, "--warm") && more)
            setup.warm = atoi(argv[++i]);
        else {
            fputs("usage: view [--seed N] [--size W H D] [--organisms N] [--co2 X] [--pitch P]\n"
                  "            [--radius R] [--import FILE.bgg] [--copies K] [--warm UNITS]\n"
                  "  drag to turn, wheel to zoom, space to pause, up/down for speed,\n"
                  "  f to follow the life, n for the next seed, esc to quit\n",
                  stderr);
            return 2;
        }
    }

    World w;
    memset(&w, 0, sizeof w);
    if (!genesis(&w, &setup))
        return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "view: %s\n", SDL_GetError());
        return 1;
    }
    int width = 1280, height = 720;
    SDL_Window *win = SDL_CreateWindow("SYN-BIOGENESIS-3D", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, width, height,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren) {
        fprintf(stderr, "view: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Texture *tex = NULL;
    Frame fr = {0};

    /* a slab is watched from close by, following its life; a deep world
     * whole, from outside its box */
    V3 box = v3(w.width, w.height, w.depth);
    bool slab = w.depth * 4 < (w.width < w.height ? w.width : w.height);
    bool follow = slab;
    double distance = slab ? 520 : 1.1 * v3_len(box), yaw = -60, elevation = slab ? 38 : 28;
    V3 aim = follow ? world_life_centre(&w) : v3_scale(box, 0.5);
    int speed = 2;
    bool paused = false, dragging = false, running = true;
    Stroke *buf = NULL;
    int cap = 0;
    Uint32 last_title = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                dragging = e.button.button == SDL_BUTTON_LEFT;
                break;
            case SDL_MOUSEBUTTONUP:
                dragging = false;
                break;
            case SDL_MOUSEMOTION:
                if (dragging) {
                    yaw -= e.motion.xrel * 0.3;
                    elevation += e.motion.yrel * 0.3;
                    elevation = elevation < -85 ? -85 : elevation > 85 ? 85 : elevation;
                }
                break;
            case SDL_MOUSEWHEEL:
                distance *= e.wheel.y > 0 ? 1 / 1.12 : 1.12;
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = false;
                    break;
                case SDLK_SPACE:
                    paused = !paused;
                    break;
                case SDLK_UP:
                    speed = speed < 64 ? speed * 2 : 64;
                    break;
                case SDLK_DOWN:
                    speed = speed > 1 ? speed / 2 : 1;
                    break;
                case SDLK_f:
                    follow = !follow;
                    break;
                case SDLK_n:
                    setup.seed++;
                    genesis(&w, &setup);
                    break;
                }
                break;
            }
        }
        if (!paused)
            for (int s = 0; s < speed; s++)
                world_time(&w);

        SDL_GetRendererOutputSize(ren, &width, &height);
        if (!tex || width != fr.width || height != fr.height) {
            if (tex)
                SDL_DestroyTexture(tex);
            frame_free(&fr);
            if (!frame_init(&fr, width, height))
                abort();
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                    width, height);
        }
        V3 target = follow ? world_life_centre(&w) : v3_scale(box, 0.5);
        aim = v3_add(aim, v3_scale(v3_sub(target, aim), 0.05));
        Camera cam;
        camera_orbit(&cam, aim, yaw, elevation, distance, height);
        draw_world(&fr, &cam, &w, &buf, &cap);
        frame_to_bytes(&fr);
        SDL_UpdateTexture(tex, NULL, fr.out, width * 3);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        Uint32 now = SDL_GetTicks();
        if (now - last_title > 250) {
            char title[256];
            snprintf(title, sizeof title,
                     "SYN-BIOGENESIS-3D  |  seed %lld  |  %d x %d x %d  |  time %lld  |  alive %d"
                     "  |  dead %d  |  kills %lld  |  x%d%s%s",
                     setup.seed, w.width, w.height, w.depth, (long long)(w.frame / 256),
                     w.population, world_n_corpses(&w), (long long)w.counts.kills, speed,
                     follow ? "  |  following" : "", paused ? "  |  PAUSED" : "");
            SDL_SetWindowTitle(win, title);
            last_title = now;
        }
    }
    free(buf);
    frame_free(&fr);
    world_free(&w);
    SDL_Quit();
    return 0;
}
