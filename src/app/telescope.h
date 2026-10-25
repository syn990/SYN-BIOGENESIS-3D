/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/telescope_screen.h, the SYN edition
 * of its C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* TELESCOPE, the dice telescope's screen (app/telescope.c). */
#ifndef APP_TELESCOPE_H
#define APP_TELESCOPE_H

#include "sim/genetic_code.h"

struct App;

void telescope_screen(struct App *a, int w, int h);
/* What the file browser's A SAVED WORLD chose. */
void telescope_chosen(struct App *a, const char *path);
/* The being to look for (WHERE A BEING LIVES), the search begun at once;
 * the screen opens on it. */
void telescope_find(struct App *a, const GeneticCode *gc, const char *name);
/* Ends the searches still going. */
void telescope_screen_free(void);

#endif
