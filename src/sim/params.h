/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from Utils.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/params.h.
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

/* Simulation parameters: 0.9's tunables with 0.9's defaults, plus the two
 * a third dimension needs: the world's depth and the segments' thickness.
 * Global, like the original's static fields. */
#ifndef SIM_PARAMS_H
#define SIM_PARAMS_H

#include <stdbool.h>

#include "sim/jrandom.h"

typedef struct {
    int initial_organisms;
    double initial_o2;
    double initial_co2;
    int world_width, world_height;
    int world_depth;       /* new: the third side of the world box */
    double segment_radius; /* new: segments are capsules this thick; 2D lines had none */
    int max_age;
    double rubbing;
    double mutation_rate;
    int segment_cost_divisor;
    double organic_obtained_energy;
    int green_obtained_energy_divisor;
    double organic_subs_produced;
    double red_energy_consumption;
    double green_energy_consumption;
    double blue_energy_consumption;
    double cyan_energy_consumption;
    double white_energy_consumption;
    double gray_energy_consumption;
    double yellow_energy_consumption;
    int red_prob, green_prob, blue_prob, cyan_prob, white_prob, gray_prob, yellow_prob;
    int pitch_prob; /* new: percent of new random genes that bend out of their plane */
    int drain_subs_divisor;
    int initial_energy;
    double max_vel;
    double max_rot;
    double elasticity;
    double decay_energy;
} Params;

extern Params P;
/* Utils.random: the one random stream every simulation decision draws from. */
extern JRandom g_rng;

#define BIO_TOL 0.0000001

/* The growth stages: a newborn is drawn at 1/5.47 of its size (stage 16)
 * and grows one stage at a time to 1/1.00 (stage 1). */
extern const double k_scale[16];

void params_set_defaults(Params *p);

int bio_random_sign(void);
bool bio_random_mutation(void);

static inline int bio_between_i(int value, int min, int max)
{
    int a = value < max ? value : max;
    return a > min ? a : min;
}

static inline double bio_min(double a, double b)
{
    return a < b ? a : b;
}

static inline double bio_max(double a, double b)
{
    return a > b ? a : b;
}

static inline double bio_between(double value, double min, double max)
{
    return bio_max(bio_min(max, value), min);
}

#endif
