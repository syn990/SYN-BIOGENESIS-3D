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

/* The software rasteriser tools/film and tools/view share, until the real
 * renderer exists: every segment as a shaded, anti-aliased capsule in 0.9's
 * colours, far ones first and fogged, inside the world's box and over its
 * floor. It only reads the World. */
#ifndef TOOLS_RASTER_H
#define TOOLS_RASTER_H

#include <stdbool.h>

#include "sim/world.h"

typedef struct {
    int width, height;
    float *rgb;         /* 0..1 colour, 3 per pixel */
    unsigned char *out; /* the same as bytes, after frame_to_bytes */
} Frame;

typedef struct {
    V3 eye, right, up, fwd;
    double focal; /* pixels */
} Camera;

typedef struct Stroke Stroke;

bool frame_init(Frame *f, int width, int height);
void frame_free(Frame *f);
void frame_to_bytes(Frame *f);

/* A camera at distance from aim, turned yaw degrees about it and raised
 * elevation degrees above it. */
void camera_orbit(Camera *c, V3 aim, double yaw_deg, double elevation_deg, double distance,
                  int height);
/* The world drawn into f; buf/cap is the caller's scratch. */
void draw_world(Frame *f, const Camera *c, const World *w, Stroke **buf, int *cap);

#endif
