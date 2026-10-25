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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "compat.h"
#include "sim/dmath.h"

Params P;
JRandom g_rng;

const double k_scale[16] = {1.00, 1.12, 1.25, 1.40, 1.57, 1.76, 1.97, 2.21,
                            2.47, 2.77, 3.11, 3.48, 3.90, 4.36, 4.89, 5.47};

void params_set_defaults(Params *p)
{
    p->initial_organisms = 30;
    p->initial_o2 = 0;
    /* 0.9's 5000 in a 1000 x 1000 floor, 32 deep */
    p->co2_density = 156.25;
    p->world_width = 600;
    p->world_height = 600;
    p->world_depth = 300;
    p->segment_radius = 1.0;
    p->light_depth = 250;
    p->shade = 1.0;
    p->co2_diffusion = 5;
    p->co2_saturation = 156.25;
    p->sink = 0.01;
    p->sight = 120;
    p->leaf_turn = 0.1;
    p->rise = 0.004;
    p->day_length = 0;
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
    p->purple_energy_consumption = 0;
    p->red_prob = 10;
    p->green_prob = 30;
    p->blue_prob = 10;
    p->cyan_prob = 20;
    p->white_prob = 10;
    p->gray_prob = 10;
    p->yellow_prob = 10;
    p->purple_prob = 10;
    p->ch4_saturation = 20;
    p->methane_divisor = 500;
    p->ch4_oxidation = 2000;
    p->pitch_prob = 50;
    p->cone_prob = 25;
    p->orb_prob = 25;
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

#define PI_(f) {#f, 'i', offsetof(Params, f)}
#define PD_(f) {#f, 'd', offsetof(Params, f)}
const ParamField k_param_fields[] = {
    PI_(initial_organisms), PD_(initial_o2), PD_(co2_density), PI_(world_width),
    PI_(world_height), PI_(world_depth), PD_(segment_radius), PD_(light_depth), PD_(shade),
    PD_(co2_diffusion), PD_(co2_saturation), PD_(sink), PD_(sight), PD_(leaf_turn), PD_(rise), PD_(day_length), PI_(max_age),
    PD_(rubbing), PD_(mutation_rate), PI_(segment_cost_divisor), PD_(organic_obtained_energy),
    PI_(green_obtained_energy_divisor), PD_(organic_subs_produced), PD_(red_energy_consumption),
    PD_(green_energy_consumption), PD_(blue_energy_consumption), PD_(cyan_energy_consumption),
    PD_(white_energy_consumption), PD_(gray_energy_consumption), PD_(yellow_energy_consumption),
    PI_(red_prob), PI_(green_prob), PI_(blue_prob), PI_(cyan_prob), PI_(white_prob),
    PI_(gray_prob), PI_(yellow_prob), PI_(pitch_prob), PI_(initial_energy), PD_(max_vel),
    PD_(max_rot), PD_(elasticity), PD_(decay_energy), PD_(purple_energy_consumption), PI_(purple_prob),
    PD_(ch4_saturation), PI_(methane_divisor), PD_(ch4_oxidation), PI_(cone_prob), PI_(orb_prob),
};
const int k_n_param_fields = (int)(sizeof k_param_fields / sizeof k_param_fields[0]);

const ParamField *params_field(const char *name)
{
    for (int i = 0; i < k_n_param_fields; i++)
        if (strcmp(k_param_fields[i].name, name) == 0)
            return &k_param_fields[i];
    return NULL;
}

double params_get(const Params *p, const ParamField *f)
{
    const char *base = (const char *)p + f->offset;
    return f->type == 'i' ? (double)*(const int *)base : *(const double *)base;
}

void params_put(Params *p, const ParamField *f, double v)
{
    char *base = (char *)p + f->offset;
    if (f->type == 'i')
        *(int *)base = (int)v;
    else
        *(double *)base = v;
}

const char *params_config_dir(void)
{
    static char dir[1024];
    snprintf(dir, sizeof dir, "%s/syn-biogenesis-3d", config_home());
    return dir;
}

char *params_text(const Params *p)
{
    size_t cap = (size_t)k_n_param_fields * 64 + 1, k = 0;
    char *t = malloc(cap);
    if (!t)
        return NULL;
    for (int i = 0; i < k_n_param_fields && k < cap; i++)
        k += (size_t)snprintf(t + k, cap - k, "%s %.17g\n", k_param_fields[i].name,
                              params_get(p, &k_param_fields[i]));
    return t;
}

void params_read_text(Params *p, const char *text)
{
    char name[128];
    double v;
    for (const char *line = text; line && *line;) {
        if (sscanf(line, "%127s %lf", name, &v) == 2 && params_field(name))
            params_put(p, params_field(name), v);
        line = strchr(line, '\n');
        if (line)
            line++;
    }
}

bool params_save(const Params *p)
{
    char path[1100], tmp[1110];
    const char *dir = params_config_dir();
    char parent[1024];
    snprintf(parent, sizeof parent, "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        mkdir(parent, 0755);
    }
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/parameters", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    char *text = params_text(p);
    FILE *f = text ? fopen(tmp, "w") : NULL;
    bool ok = f && fputs(text, f) >= 0;
    if (f && fclose(f) != 0)
        ok = false;
    free(text);
    ok = ok && rename(tmp, path) == 0;
    if (!ok)
        remove(tmp);
    return ok;
}

bool params_load(Params *p)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/parameters", params_config_dir());
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char *text = NULL;
    size_t len = 0, cap = 0, got;
    char chunk[4096];
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (len + got + 1 > cap) {
            cap = (len + got + 1) * 2;
            char *q = realloc(text, cap);
            if (!q)
                break;
            text = q;
        }
        memcpy(text + len, chunk, got);
        len += got;
    }
    fclose(f);
    if (!text)
        return true;
    text[len] = 0;
    params_read_text(p, text);
    free(text);
    return true;
}
