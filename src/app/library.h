/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/library_screen.h, the SYN edition of
 * its C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* LIBRARY, the being library's screen (syn/library.h): its folders on the
 * left, with SYN-BIOGENESIS's library of 0.9 beings and the arena's ladder
 * as more, read only, and the field guide; the beings of the one chosen
 * on the right, each drawn; and what can be done with the one picked: put
 * it on the arena's ladder, place it in the world, open it in the
 * laboratory, copy it, rename it, move it to another folder, delete it.
 * Beings come in from files (IMPORT), from the world and the laboratory
 * (Keep), and from the arena (Keep in the library). */
#ifndef APP_LIBRARY_H
#define APP_LIBRARY_H

#include <stdbool.h>

#include "sim/genetic_code.h"

struct App;

void library_screen(struct App *a, int w, int h);
/* Read again the next time it is shown */
void library_refresh(void);
/* The field guide shown, the next time the screen is */
void library_show_guide(void);
/* A code kept in a folder of the library under a name, saying where it
 * came from; a note says where it went. */
bool app_library_keep(struct App *a, const GeneticCode *gc, const char *folder, const char *name,
                      const char *origin);
/* What the file browser's IMPORT chose. */
void library_imported(struct App *a, const char *path);
/* A pick from one of the screen's menus (ACT_LIBRARY_PICK + k). */
void library_pick(struct App *a, int k);
void library_screen_free(void);

#endif
