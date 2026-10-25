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

/* A picture written as a PNG, without a library: the pixels in zlib's
 * stored (uncompressed) blocks, which every reader takes. */
#ifndef IO_PNG_H
#define IO_PNG_H

#include <stdbool.h>

/* rgba: w x h pixels, 4 bytes each, top row first. */
bool png_write_rgba(const char *path, int w, int h, const unsigned char *rgba);

#endif
