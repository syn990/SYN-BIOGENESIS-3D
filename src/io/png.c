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

#include "io/png.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const unsigned char *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++)
                k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
            crc_table[i] = k;
        }
    c ^= 0xffffffffu;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static bool chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char h[8], t[4];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    /* the crc goes over the type and the data, chained as zlib's crc32 is */
    be32(t, crc(crc(0, h + 4, 4), data, n));
    return fwrite(h, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(t, 1, 4, f) == 4;
}

bool png_write_rgba(const char *path, int w, int h, const unsigned char *rgba)
{
    if (w <= 0 || h <= 0)
        return false;
    /* the rows, each after its filter byte (0: none) */
    size_t row = (size_t)w * 4 + 1, raw_n = row * (size_t)h;
    unsigned char *raw = malloc(raw_n);
    if (!raw)
        return false;
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * row] = 0;
        memcpy(raw + (size_t)y * row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    /* zlib: a header, stored blocks of at most 65535 bytes, Adler-32 */
    size_t nblocks = raw_n / 65535 + 1, z_n = 2 + raw_n + nblocks * 5 + 4;
    unsigned char *z = malloc(z_n);
    if (!z) {
        free(raw);
        return false;
    }
    size_t k = 0;
    z[k++] = 0x78;
    z[k++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t at = 0; at < raw_n || at == 0;) {
        size_t len = raw_n - at < 65535 ? raw_n - at : 65535;
        z[k++] = at + len >= raw_n ? 1 : 0;
        z[k++] = (unsigned char)len;
        z[k++] = (unsigned char)(len >> 8);
        z[k++] = (unsigned char)~len;
        z[k++] = (unsigned char)(~len >> 8);
        memcpy(z + k, raw + at, len);
        for (size_t i = 0; i < len; i++) {
            a = (a + raw[at + i]) % 65521;
            b = (b + a) % 65521;
        }
        k += len;
        at += len;
        if (len == 0)
            break;
    }
    be32(z + k, (b << 16) | a);
    k += 4;
    free(raw);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(z);
        return false;
    }
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bits a channel */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filters */
    ihdr[12] = 0; /* no interlace */
    bool ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) && chunk(f, "IDAT", z, k) &&
              chunk(f, "IEND", NULL, 0);
    free(z);
    if (fclose(f) != 0)
        ok = false;
    return ok;
}
