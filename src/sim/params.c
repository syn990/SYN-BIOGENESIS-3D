/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from Utils.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/params.c.
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

#include "sim/params.h"

#include "sim/dmath.h"

Params P;
JRandom g_rng;

const double k_scale[16] = {1.00, 1.12, 1.25, 1.40, 1.57, 1.76, 1.97, 2.21,
                            2.47, 2.77, 3.11, 3.48, 3.90, 4.36, 4.89, 5.47};

void params_set_defaults(Params *p)
{
    p->initial_organisms = 15;
    p->initial_o2 = 0;
    p->initial_co2 = 5000;
    p->world_width = 1000;
    p->world_height = 1000;
    p->world_depth = 32;
    p->segment_radius = 1.0;
    p->max_age = 30;
    p->rubbing = 0.98;
    p->mutation_rate = 0.05;
    p->segment_cost_divisor = 5000;
    p->organic_obtained_energy = 0.5;
    p->green_obtained_energy_divisor = 500;
    p->organic_subs_produced = 0.1;
    p->red_energy_consumption = 0;
    p->green_energy_consumption = 0;
    p->blue_energy_consumption = 0;
    p->cyan_energy_consumption = 0;
    p->white_energy_consumption = 1;
    p->gray_energy_consumption = 1;
    p->yellow_energy_consumption = 0;
    p->red_prob = 10;
    p->green_prob = 30;
    p->blue_prob = 10;
    p->cyan_prob = 20;
    p->white_prob = 10;
    p->gray_prob = 10;
    p->yellow_prob = 10;
    p->pitch_prob = 50;
    p->drain_subs_divisor = 5000;
    p->initial_energy = 50;
    p->max_vel = 5;
    p->max_rot = D_PI / 16;
    p->elasticity = 0.8;
    p->decay_energy = 0.1;
}

int bio_random_sign(void)
{
    return (jr_next_int(&g_rng, 2) << 1) - 1;
}

bool bio_random_mutation(void)
{
    return jr_next_double(&g_rng) < P.mutation_rate;
}
