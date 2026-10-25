/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/telescope.h, the SYN edition of its
 * C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* The dice telescope: the random generator as algebra rather than as a
 * stream.
 *
 * Every random decision in a world is a draw from one java.util.Random, a
 * 48-bit linear congruential generator, as in Biogenesis 0.9, and every
 * draw is the same affine step x -> A x + B (mod 2^48). That gives three
 * things:
 * - The state n draws later, or n draws earlier, is a single affine map,
 *   reached in about log n compositions whichever way n points.
 * - A seed is only its 48 low bits: setting a seed keeps (seed ^ A) mod
 *   2^48, and every 48-bit state is the first state of exactly one seed in
 *   [0, 2^48).
 * - A gene's length is 2 + 16u, with u = nextDouble() made from 53 bits of
 *   two consecutive states, as 0.9 draws it here too, so a length read
 *   exactly leaves only a few states (almost always one) that could have
 *   drawn it. A gene's turn, 2 pi u, does the same with slightly less
 *   certainty.
 *
 * A saved world carries its organisms' genes. A founder's gene that no
 * mutation has touched since is still the draw the generator made at
 * genesis. From one of those, `--telescope world` steps the generator back
 * to the seed the world was made from (crack.h), then replays the world
 * from that seed to the saved frame as the proof.
 *
 * Nothing here changes the simulation: it reads saved worlds and runs
 * world_genesis and world_time as they are. */
#ifndef SYN_TELESCOPE_H
#define SYN_TELESCOPE_H

#include <stdint.h>

#define TEL_MASK ((1ULL << 48) - 1)

/* The generator's state n draws after `state` (n < 0: before it). */
uint64_t tel_jump(uint64_t state, int64_t n);
/* The other way: how many draws after `from` the generator is at `to`, in
 * [0, 2^48). The generator's period is all 2^48 states, so every state is
 * some number of draws after any other: the discrete logarithm of the
 * affine step. It is easy here because the modulus is a power of two: the
 * low k bits of the states repeat every 2^k draws, so the count is read a
 * bit at a time from the bottom, in 48 steps. */
uint64_t tel_draws(uint64_t from, uint64_t to);
/* The state setting this seed leaves, and back: the seed in [0, 2^48) that
 * starts at that state. */
uint64_t tel_seed_state(int64_t seed);
int64_t tel_state_seed(uint64_t state);
/* Every state from which the next draws make exactly this gene length
 * (gene_randomize_length) or this turn (gene_randomize_theta); at most max
 * are written to out, and the count found is returned. */
int tel_crack_length(double length, uint64_t *out, int max);
int tel_crack_theta(double theta, uint64_t *out, int max);
/* The same for a bend (gene_randomize_pitch's, 0 is none) and a drive
 * (gc_new_random's and a mutation's, -1 to 1): this program's own draws,
 * 53 bits each like the others. */
int tel_crack_pitch(double pitch, uint64_t *out, int max);
int tel_crack_drive(double drive, uint64_t *out, int max);
/* A plain nextDouble() (a height): exactly one 53-bit value. */
int tel_crack_unit(double u, uint64_t *out, int max);

/* How many genes two codes differ by, place by place, plus those one has
 * beyond the other, plus one each for the arms and the mirroring. */
struct GeneticCode;
int tel_code_distance(const struct GeneticCode *a, const struct GeneticCode *b);

/* syn-biogenesis-3d --telescope ... */
int syn_telescope_main(int argc, char **argv);

#endif
