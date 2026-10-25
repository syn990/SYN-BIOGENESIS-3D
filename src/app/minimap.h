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

/* The minimap (app/minimap.c). */
#ifndef APP_MINIMAP_H
#define APP_MINIMAP_H

#include <stdbool.h>

struct App;

bool minimap_on(void);
void minimap_toggle(void);
/* At the bottom right, its foot at y bottom, while part of the world is
 * out of view (and the minimap is on). */
void minimap_draw(struct App *a, int w, int h, float bottom);

#endif
