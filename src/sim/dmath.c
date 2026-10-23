/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * The kernels and the medium-size range reduction are fdlibm's
 * (k_sin.c, k_cos.c, e_rem_pio2.c, s_sin.c, s_cos.c):
 *
 *   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 *   Developed at SunSoft, a Sun Microsystems, Inc. business.
 *   Permission to use, copy, modify, and distribute this
 *   software is freely granted, provided that this notice
 *   is preserved.
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

#include "sim/dmath.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Every double operation must round to double as it goes. x87 arithmetic
 * (FLT_EVAL_METHOD 2) keeps 80-bit intermediates and would compute other
 * worlds; build for SSE2 (any x86-64 target) or another IEEE double unit. */
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "SYN-BIOGENESIS-3D needs FLT_EVAL_METHOD 0 (e.g. -msse2 -mfpmath=sse on 32-bit x86)"
#endif

static inline int32_t hi_word(double x)
{
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    return (int32_t)(b >> 32);
}

static inline double with_hi(int32_t hi)
{
    uint64_t b = (uint64_t)(uint32_t)hi << 32;
    double x;
    memcpy(&x, &b, sizeof x);
    return x;
}

/* __kernel_sin: sin(x + y) for |x| <= pi/4, y the tail of x. */
static double k_sin(double x, double y, int iy)
{
    static const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                        S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                        S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    if ((hi_word(x) & 0x7fffffff) < 0x3e400000 && (int)x == 0)
        return x;
    double z = x * x;
    double v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0)
        return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

/* __kernel_cos: cos(x + y) for |x| <= pi/4. */
static double k_cos(double x, double y)
{
    static const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                        C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                        C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    int32_t ix = hi_word(x) & 0x7fffffff;
    if (ix < 0x3e400000 && (int)x == 0)
        return 1.0;
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333)
        return 1.0 - (0.5 * z - (z * r - x * y));
    double qx = ix > 0x3fe90000 ? 0.28125 : with_hi(ix - 0x00200000);
    double hz = 0.5 * z - qx;
    double a = 1.0 - qx;
    return a - (hz - (z * r - x * y));
}

/* __ieee754_rem_pio2 for |x| < 2^19 * pi/2: x - n*pi/2 as y[0] + y[1].
 * Larger arguments are first brought into range with fmod, which IEEE 754
 * computes exactly; the simulation's angles never get near that size. */
static int rem_pio2(double x, double *y)
{
    static const double invpio2 = 6.36619772367581382433e-01,
                        pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11,
                        pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
                        pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
    double t = fabs(x);
    if (t > 823549.6) {
        x = fmod(x, 2 * D_PI);
        t = fabs(x);
    }
    int n = (int)(t * invpio2 + 0.5);
    double fn = (double)n;
    double r = t - fn * pio2_1;
    double w = fn * pio2_1t;
    int j = (hi_word(t) >> 20) & 0x7ff;
    y[0] = r - w;
    int i = j - ((hi_word(y[0]) >> 20) & 0x7ff);
    if (i > 16) {
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        i = j - ((hi_word(y[0]) >> 20) & 0x7ff);
        if (i > 49) {
            t = r;
            w = fn * pio2_3;
            r = t - w;
            w = fn * pio2_3t - ((t - r) - w);
            y[0] = r - w;
        }
    }
    y[1] = (r - y[0]) - w;
    if (x < 0) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}

double d_sin(double x)
{
    if ((hi_word(x) & 0x7fffffff) <= 0x3fe921fb)
        return k_sin(x, 0.0, 0);
    double y[2];
    switch (rem_pio2(x, y) & 3) {
    case 0:
        return k_sin(y[0], y[1], 1);
    case 1:
        return k_cos(y[0], y[1]);
    case 2:
        return -k_sin(y[0], y[1], 1);
    default:
        return -k_cos(y[0], y[1]);
    }
}

double d_cos(double x)
{
    if ((hi_word(x) & 0x7fffffff) <= 0x3fe921fb)
        return k_cos(x, 0.0);
    double y[2];
    switch (rem_pio2(x, y) & 3) {
    case 0:
        return k_cos(y[0], y[1]);
    case 1:
        return -k_sin(y[0], y[1], 1);
    case 2:
        return -k_cos(y[0], y[1]);
    default:
        return k_sin(y[0], y[1], 1);
    }
}
