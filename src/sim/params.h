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

/* Simulation parameters: 0.9's tunables with 0.9's defaults, plus what a
 * world of water needs: its depth, the segments' thickness, the light that
 * falls from the surface, the CO2 dissolved in it, the sinking of the dead
 * and how far an organism senses. Global, like the original's static
 * fields. */
#ifndef SIM_PARAMS_H
#define SIM_PARAMS_H

#include <stdbool.h>
#include <stddef.h>

#include "sim/jrandom.h"

typedef struct {
    int initial_organisms;
    double initial_o2;
    double co2_density;    /* new: CO2 dissolved per million cubic units at genesis */
    int world_width, world_height;
    int world_depth;       /* new: the third side of the world box; the surface is on top */
    double segment_radius; /* new: segments are capsules this thick; 2D lines had none */
    double light_depth;    /* new: the depth over which the light falls to 1/e */
    double shade;          /* new: how much of the light a body's shadow stops */
    double co2_diffusion;  /* new: how fast dissolved CO2 spreads, square units a frame */
    double co2_saturation; /* new: the CO2 per million cubic units at which leaves work at
                              full rate; 0.9's drain_subs_divisor, as a concentration */
    double sink;           /* new: how fast the dead sink: speed gained a frame */
    double sight;          /* new: how far an organism sees in full light, and smells the dead */
    double leaf_turn;      /* new: how hard leaves turn to lie across the light */
    double rise;           /* new: how fast a living body's float lifts or sinks it toward its
                              aim: speed gained a frame */
    double day_length;     /* new: time units from one dawn to the next; 0, always day */
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
    double purple_energy_consumption; /* new: what a purple segment's use costs */
    int red_prob, green_prob, blue_prob, cyan_prob, white_prob, gray_prob, yellow_prob;
    int purple_prob;          /* new: the chance a new gene is purple, beside 0.9's */
    double ch4_saturation;    /* new: the methane per million cubic units at which purple works
                                 at full rate */
    int methane_divisor;      /* new: purple's yield divisor, as the leaves' light divisor */
    double ch4_oxidation;     /* new: frames over which dissolved methane turns back to CO2 (the
                                 time constant; 0: never) */
    int pitch_prob; /* new: percent of new random genes that bend out of their plane */
    int cone_prob;  /* new: percent of new random codes whose arms stand as a cone */
    int orb_prob;   /* new: percent of new random codes whose arms stand as an orb */
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

/* Every parameter by name, in a fixed order (a saved world's): an int or a
 * double field of Params. */
typedef struct {
    const char *name;
    char type; /* 'i' or 'd' */
    size_t offset;
} ParamField;
extern const ParamField k_param_fields[];
extern const int k_n_param_fields;
const ParamField *params_field(const char *name);
double params_get(const Params *p, const ParamField *f);
void params_put(Params *p, const ParamField *f, double v);

/* The configuration folder: syn-biogenesis-3d under config_home()
 * (compat.h), not made here. */
const char *params_config_dir(void);
/* The window's parameters, kept there as "parameters" (a name and a value
 * a line), so the arena without a window runs what the window set. Load
 * leaves alone what the file doesn't name; false if there is none. */
bool params_save(const Params *p);
bool params_load(Params *p);
/* The parameters as that text, malloc'd; and read back (what it doesn't
 * name is left alone). */
char *params_text(const Params *p);
void params_read_text(Params *p, const char *text);

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
