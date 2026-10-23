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

/* Vectors, rotations (unit quaternions) and 3x3 matrices in doubles.
 *
 * Where 2D Biogenesis kept a position, an angle theta and the scalars
 * dx, dy, dtheta and inertia, an organism here keeps a position V3, an
 * orientation Quat, a velocity V3, an angular velocity V3 and an inertia
 * tensor M3. Every function is plain IEEE arithmetic plus sqrt and the
 * d_sin/d_cos of dmath.h, so results are the same on every platform.
 */
#ifndef SIM_VEC3_H
#define SIM_VEC3_H

#include <math.h>

#include "sim/dmath.h"

typedef struct {
    double x, y, z;
} V3;

typedef struct {
    double w, x, y, z;
} Quat;

typedef struct {
    double m[3][3];
} M3;

static inline V3 v3(double x, double y, double z)
{
    return (V3){x, y, z};
}

static inline V3 v3_add(V3 a, V3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline V3 v3_sub(V3 a, V3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline V3 v3_scale(V3 a, double s)
{
    return v3(a.x * s, a.y * s, a.z * s);
}

static inline double v3_dot(V3 a, V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline V3 v3_cross(V3 a, V3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline double v3_len(V3 a)
{
    return sqrt(v3_dot(a, a));
}

static inline int v3_is_zero(V3 a)
{
    return a.x == 0 && a.y == 0 && a.z == 0;
}

/* a scaled to length 1, or fallback when a has no direction */
static inline V3 v3_unit_or(V3 a, V3 fallback)
{
    double l = v3_len(a);
    return l > 1e-12 ? v3_scale(a, 1.0 / l) : fallback;
}

/* a shortened to length max if it is longer */
static inline V3 v3_clamp_len(V3 a, double max)
{
    double l = v3_len(a);
    return l > max ? v3_scale(a, max / l) : a;
}

static inline Quat quat_identity(void)
{
    return (Quat){1, 0, 0, 0};
}

static inline Quat quat_mul(Quat a, Quat b)
{
    return (Quat){a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                  a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                  a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

static inline Quat quat_normalize(Quat q)
{
    double l = sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return (Quat){q.w / l, q.x / l, q.y / l, q.z / l};
}

/* The rotation by angle radians about unit axis. */
static inline Quat quat_axis_angle(V3 axis, double angle)
{
    double s = d_sin(angle / 2);
    return (Quat){d_cos(angle / 2), axis.x * s, axis.y * s, axis.z * s};
}

/* q turned further by the world-frame rotation vector w (axis * angle):
 * one frame of spin at angular velocity w. */
static inline Quat quat_spin(Quat q, V3 w)
{
    double a = v3_len(w);
    if (a == 0)
        return q;
    return quat_normalize(quat_mul(quat_axis_angle(v3_scale(w, 1.0 / a), a), q));
}

static inline V3 quat_rotate(Quat q, V3 v)
{
    V3 u = v3(q.x, q.y, q.z);
    V3 t = v3_scale(v3_cross(u, v), 2.0);
    return v3_add(v3_add(v, v3_scale(t, q.w)), v3_cross(u, t));
}

static inline M3 m3_from_quat(Quat q)
{
    double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return (M3){{{1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy)},
                 {2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx)},
                 {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy)}}};
}

static inline V3 m3_mul_v(const M3 *a, V3 v)
{
    return v3(a->m[0][0] * v.x + a->m[0][1] * v.y + a->m[0][2] * v.z,
              a->m[1][0] * v.x + a->m[1][1] * v.y + a->m[1][2] * v.z,
              a->m[2][0] * v.x + a->m[2][1] * v.y + a->m[2][2] * v.z);
}

/* a^T v */
static inline V3 m3_tmul_v(const M3 *a, V3 v)
{
    return v3(a->m[0][0] * v.x + a->m[1][0] * v.y + a->m[2][0] * v.z,
              a->m[0][1] * v.x + a->m[1][1] * v.y + a->m[2][1] * v.z,
              a->m[0][2] * v.x + a->m[1][2] * v.y + a->m[2][2] * v.z);
}

/* The inverse of a symmetric positive-definite matrix (an inertia tensor). */
static inline M3 m3_inverse_sym(const M3 *a)
{
    const double(*m)[3] = a->m;
    double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    double id = 1.0 / det;
    M3 r;
    r.m[0][0] = c00 * id;
    r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
    r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
    r.m[1][0] = c01 * id;
    r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
    r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
    r.m[2][0] = c02 * id;
    r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
    r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
    return r;
}

static inline double m3_trace(const M3 *a)
{
    return a->m[0][0] + a->m[1][1] + a->m[2][2];
}

#endif
