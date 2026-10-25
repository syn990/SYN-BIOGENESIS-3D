/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/sidecar.c, the SYN edition of its C port
 * of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "syn/sidecar.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syn/family.h"
#include "syn/history.h"

/* magic, format, simulation; the world file's size and FNV-1a hash; the
 * history's bytes and the tree's, each after its length */
#define MAGIC "SYNSIDE1"
#define FORMAT 1
#define SIM 3 /* this program's */
#define HEAD (8 + 4 + 4 + 8 + 8)

static void side_path(char *out, size_t n, const char *world_path)
{
    snprintf(out, n, "%s.syn", world_path);
}

static uint8_t *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *p = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *q = realloc(p, cap);
            if (!q) {
                free(p);
                fclose(f);
                return NULL;
            }
            p = q;
        }
        size_t got = fread(p + len, 1, cap - len, f);
        len += got;
        if (got == 0)
            break;
    }
    bool err = ferror(f);
    fclose(f);
    if (err) {
        free(p);
        return NULL;
    }
    *n = len;
    return p;
}

static uint64_t fnv64(const uint8_t *p, size_t n)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static void le(uint8_t *out, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t unle(const uint8_t *p, int bytes)
{
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

bool syn_sidecar_save(const World *w, const char *world_path)
{
    size_t wn;
    uint8_t *world = read_all(world_path, &wn);
    if (!world)
        return false;
    uint8_t head[HEAD];
    memcpy(head, MAGIC, 8);
    le(head + 8, FORMAT, 4);
    le(head + 12, SIM, 4);
    le(head + 16, wn, 8);
    le(head + 24, fnv64(world, wn), 8);
    free(world);

    uint8_t *hist = NULL, *fam = NULL;
    size_t hn = syn_history_save(&hist);
    size_t fn = hn ? syn_family_save(w, &fam) : 0;
    bool ok = hn && fn && hn <= UINT32_MAX && fn <= UINT32_MAX;
    char path[1200], tmp[1300];
    side_path(path, sizeof path, world_path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = ok ? fopen(tmp, "wb") : NULL;
    if (f) {
        uint8_t len[4];
        fwrite(head, 1, HEAD, f);
        le(len, hn, 4);
        fwrite(len, 1, 4, f);
        fwrite(hist, 1, hn, f);
        le(len, fn, 4);
        fwrite(len, 1, 4, f);
        fwrite(fam, 1, fn, f);
        ok = !ferror(f);
        ok = fclose(f) == 0 && ok;
        /* in place at once, never half written (rename on Windows will not
         * replace a file) */
        ok = ok && rename(tmp, path) == 0;
        if (!ok)
            remove(tmp);
    } else {
        ok = false;
    }
    if (!ok)
        fprintf(stderr, "syn-biogenesis-3d: the history could not be saved beside %s\n", world_path);
    free(hist);
    free(fam);
    return ok;
}

bool syn_sidecar_load(World *w, const char *world_path)
{
    char path[1200];
    side_path(path, sizeof path, world_path);
    size_t sn;
    uint8_t *side = read_all(path, &sn);
    if (!side)
        return false;
    size_t wn;
    uint8_t *world = read_all(world_path, &wn);
    bool ok = world && sn >= HEAD + 8 && memcmp(side, MAGIC, 8) == 0 &&
              unle(side + 8, 4) == FORMAT && unle(side + 12, 4) == SIM &&
              unle(side + 16, 8) == wn && unle(side + 24, 8) == fnv64(world, wn);
    free(world);
    if (ok) {
        size_t at = HEAD;
        size_t hn = (size_t)unle(side + at, 4);
        at += 4;
        ok = hn <= sn - at - 4;
        if (ok) {
            const uint8_t *hist = side + at;
            at += hn;
            size_t fn = (size_t)unle(side + at, 4);
            at += 4;
            ok = fn == sn - at && syn_history_load(w, hist, hn, side + at, fn, true);
        }
    }
    free(side);
    return ok;
}
