/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from WorldStatistics.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/stats.c.
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

#include "sim/stats.h"

#include <string.h>

#include "sim/organism.h"
#include "sim/world.h"

enum { EXT_NO, EXT_POSSIBLE, EXT_CONFIRMED, EXT_FINISHING };

void stats_init(WorldStats *s, int organisms, double o2, double co2)
{
    memset(s, 0, sizeof *s);
    s->min_population = organisms;
    s->max_oxygen = o2;
    s->min_oxygen = o2;
    s->max_co2 = co2;
    s->min_co2 = co2;
}

void stats_release(WorldStats *s)
{
    GeneticCode **refs[] = {&s->alive_most_children, &s->alive_most_kills,
                            &s->alive_most_infections, &s->most_children,
                            &s->most_kills,          &s->most_infections,
                            &s->last_born,           &s->last_dead,
                            &s->last_infected};
    for (size_t i = 0; i < sizeof refs / sizeof refs[0]; i++)
        gc_assign(refs[i], NULL);
}

double stats_average_population(const WorldStats *s)
{
    return s->time > 0 ? s->population_sum / (double)s->time : 0;
}

double stats_average_deaths(const WorldStats *s)
{
    return s->time > 0 ? s->death_sum / (double)s->time : 0;
}

double stats_average_births(const WorldStats *s)
{
    return s->time > 0 ? s->birth_sum / (double)s->time : 0;
}

double stats_average_infections(const WorldStats *s)
{
    return s->time > 0 ? s->infections_sum / (double)s->time : 0;
}

void stats_population_increase(WorldStats *s, int n)
{
    if (n > s->max_population) {
        s->max_population = n;
        s->max_population_time = s->time;
    }
}

void stats_population_decrease(WorldStats *s, int n)
{
    if (n < s->min_population) {
        s->min_population = n;
        s->min_population_time = s->time;
    }
}

void stats_organism_created(WorldStats *s)
{
    s->created_organisms++;
}

void stats_organism_born(WorldStats *s, Organism *child, Organism *parent)
{
    gc_assign(&s->last_born, child->gc);
    if (parent->total_children > s->most_children_n) {
        gc_assign(&s->most_children, parent->gc);
        s->most_children_n = parent->total_children;
        s->most_children_time = s->time;
    }
    s->birth_sum++;
    s->birth_last_time++;
}

void stats_organism_die(WorldStats *s, Organism *dying, Organism *killer)
{
    gc_assign(&s->last_dead, dying->gc);
    if (killer && killer->total_kills > s->most_kills_n) {
        gc_assign(&s->most_kills, killer->gc);
        s->most_kills_n = killer->total_kills;
        s->most_kills_time = s->time;
    }
    s->death_sum++;
    s->death_last_time++;
}

void stats_organism_infects(WorldStats *s, Organism *infected, Organism *infector)
{
    gc_assign(&s->last_infected, infected->gc);
    if (infector && infector->total_infected > s->most_infections_n) {
        gc_assign(&s->most_infections, infector->gc);
        s->most_infections_n = infector->total_infected;
        s->most_infections_time = s->time;
    }
    s->infections_sum++;
}

static void series_push(StatsSeries *ser, double v)
{
    if (ser->n == STATS_HISTORY) {
        memmove(ser->v, ser->v + 1, (STATS_HISTORY - 1) * sizeof(double));
        ser->n--;
    }
    ser->v[ser->n++] = v;
}

void stats_event_time(WorldStats *s, int population, double o2, double co2)
{
    s->time++;
    if (s->death_last_time > 1.5 * stats_average_deaths(s)) {
        if (s->death_last_time > 3 * stats_average_deaths(s)) {
            if (s->mass_extinction_state != EXT_CONFIRMED &&
                s->mass_extinction_state != EXT_FINISHING)
                s->mass_extinctions++;
            s->mass_extinction_state = EXT_CONFIRMED;
        } else {
            switch (s->mass_extinction_state) {
            case EXT_NO:
                s->mass_extinction_state = EXT_POSSIBLE;
                break;
            case EXT_POSSIBLE:
                s->mass_extinction_state = EXT_CONFIRMED;
                s->mass_extinctions++;
                break;
            case EXT_FINISHING:
                s->mass_extinction_state = EXT_CONFIRMED;
                break;
            }
        }
    } else {
        switch (s->mass_extinction_state) {
        case EXT_POSSIBLE:
        case EXT_FINISHING:
            s->mass_extinction_state = EXT_NO;
            break;
        case EXT_CONFIRMED:
            s->mass_extinction_state = EXT_FINISHING;
            break;
        }
    }
    s->population_sum += population;
    if (o2 > s->max_oxygen) {
        s->max_oxygen = o2;
        s->max_oxygen_time = s->time;
    }
    if (o2 < s->min_oxygen) {
        s->min_oxygen = o2;
        s->min_oxygen_time = s->time;
    }
    if (co2 > s->max_co2) {
        s->max_co2 = co2;
        s->max_co2_time = s->time;
    }
    if (co2 < s->min_co2) {
        s->min_co2 = co2;
        s->min_co2_time = s->time;
    }
    if (s->birth_last_time > s->max_births)
        s->max_births = s->birth_last_time;
    if (s->death_last_time > s->max_deaths)
        s->max_deaths = s->death_last_time;
    series_push(&s->population, population);
    series_push(&s->deaths, s->death_last_time);
    series_push(&s->births, s->birth_last_time);
    series_push(&s->oxygen, o2);
    series_push(&s->co2, co2);
    s->death_last_time = 0;
    s->birth_last_time = 0;
}

void stats_find_best_alive(WorldStats *s, World *w)
{
    gc_assign(&s->alive_most_children, NULL);
    gc_assign(&s->alive_most_kills, NULL);
    gc_assign(&s->alive_most_infections, NULL);
    s->alive_most_children_n = 0;
    s->alive_most_kills_n = 0;
    s->alive_most_infections_n = 0;
    for (int i = 0; i < w->norgs; i++) {
        Organism *o = w->orgs[i];
        if (o->removed || !o->alive)
            continue;
        if (o->total_children > s->alive_most_children_n) {
            s->alive_most_children_n = o->total_children;
            gc_assign(&s->alive_most_children, o->gc);
        }
        if (o->total_kills > s->alive_most_kills_n) {
            s->alive_most_kills_n = o->total_kills;
            gc_assign(&s->alive_most_kills, o->gc);
        }
        if (o->total_infected > s->alive_most_infections_n) {
            s->alive_most_infections_n = o->total_infected;
            gc_assign(&s->alive_most_infections, o->gc);
        }
    }
}
