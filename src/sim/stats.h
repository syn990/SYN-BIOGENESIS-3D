/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from WorldStatistics.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/sim/stats.h.
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

/* WorldStatistics.java: the records a world keeps of itself. One time unit
 * is 256 frames; every unit the population, births, deaths and gases go
 * into 100-unit histories, and mass extinctions are told from the death
 * rate. Bookkeeping only: nothing here changes the world. */
#ifndef SIM_STATS_H
#define SIM_STATS_H

#include <stdint.h>

#include "sim/genetic_code.h"

#define STATS_HISTORY 100

typedef struct {
    double v[STATS_HISTORY];
    int n;
} StatsSeries;

typedef struct {
    int64_t time;
    int max_population;
    int max_births;
    int max_deaths;
    int64_t max_population_time;
    int min_population;
    int64_t min_population_time;
    int mass_extinctions;
    int mass_extinction_state;
    int created_organisms;
    int64_t population_sum;
    int64_t death_sum;
    int death_last_time;
    int64_t birth_sum;
    int birth_last_time;
    int64_t infections_sum;
    double max_oxygen;
    int64_t max_oxygen_time;
    double min_oxygen;
    int64_t min_oxygen_time;
    double max_co2;
    int64_t max_co2_time;
    double min_co2;
    int64_t min_co2_time;

    /* records over the living, filled by stats_find_best_alive */
    GeneticCode *alive_most_children;
    int alive_most_children_n;
    GeneticCode *alive_most_kills;
    int alive_most_kills_n;
    GeneticCode *alive_most_infections;
    int alive_most_infections_n;

    GeneticCode *most_children;
    int most_children_n;
    int64_t most_children_time;
    GeneticCode *most_kills;
    int most_kills_n;
    int64_t most_kills_time;
    GeneticCode *most_infections;
    int most_infections_n;
    int64_t most_infections_time;

    GeneticCode *last_born;
    GeneticCode *last_dead;
    GeneticCode *last_infected;

    StatsSeries population, deaths, births, oxygen, co2;
} WorldStats;

struct Organism;
struct World;

/* A world's first statistics: its founders and gases as genesis left them. */
void stats_init(WorldStats *s, int organisms, double o2, double co2);
void stats_release(WorldStats *s);
double stats_average_population(const WorldStats *s);
double stats_average_deaths(const WorldStats *s);
double stats_average_births(const WorldStats *s);
double stats_average_infections(const WorldStats *s);

void stats_population_increase(WorldStats *s, int new_population);
void stats_population_decrease(WorldStats *s, int new_population);
void stats_organism_created(WorldStats *s);
void stats_organism_born(WorldStats *s, struct Organism *child, struct Organism *parent);
void stats_organism_die(WorldStats *s, struct Organism *dying, struct Organism *killer);
void stats_organism_infects(WorldStats *s, struct Organism *infected, struct Organism *infector);
void stats_event_time(WorldStats *s, int population, double o2, double co2);
void stats_find_best_alive(WorldStats *s, struct World *w);

#endif
