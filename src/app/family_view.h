/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/family_view.h, the SYN edition of its C port
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

/* The statistics screen's FAMILY tab: the family tree of syn/family.h
 * drawn as Graphviz draws a graph left to right, live.
 *
 * Each species is a box with its founder's body, where it stands now and
 * how many it peaked at; each edge says what changed. Shown: the species
 * living now, the best of each epoch, or every one that mattered, with
 * whatever they descend from (species in between that are only ancestors
 * are folded into the edge). Drag to move, the wheel zooms, a click
 * focuses a species and its card tells its story; a double click opens it
 * in the laboratory, a right click its menu (the laboratory, copy it, keep
 * it).
 *
 * EXPORT writes the graph as shown into a folder beside the world:
 * family.dot (with family.svg and family.png when Graphviz is installed),
 * a picture and a .bgg of each species shown, and the best of each epoch
 * compiled as epoch-N-#ID.bgg with an index (best-of-each-epoch.txt). */
#ifndef APP_FAMILY_VIEW_H
#define APP_FAMILY_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ui/skin.h"

struct App;

/* The tab's controls, along the row at (x, y), h high; what it shows
 * goes in info. Returns where they end. */
float syn_family_controls(struct App *a, float x, float y, float h, char *info, size_t n);
/* The tree in area, and the focused species' card at its right. */
void syn_family_view(struct App *a, UIRect area, bool keys);
/* Focus a species and bring it into view (0: none). */
void syn_family_focus(uint32_t id);
void syn_family_view_free(void);

#endif
