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

/* The transcendental functions the simulation is allowed to use.
 *
 * A seed has to name one world on every machine and every compiler, so the
 * simulation never calls the C library's sin, cos and exp: their last bits
 * differ between glibc, the Windows CRT and macOS. d_sin, d_cos and d_exp
 * are fdlibm's kernels and range reduction written out in plain IEEE double
 * arithmetic, which every conforming platform evaluates identically (the
 * Makefile turns floating-point contraction off so no FMA sneaks in).
 * sqrt is exactly rounded by IEEE 754 and safe to use directly.
 */
#ifndef SIM_DMATH_H
#define SIM_DMATH_H

#define D_PI 3.14159265358979323846

double d_sin(double x);
double d_cos(double x);
double d_exp(double x);

#endif
