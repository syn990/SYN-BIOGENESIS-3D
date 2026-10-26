/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of the SYN-OS interface SYN-BIOGENESIS-3D shares with SYN-BIOGENESIS
 * and SYN-DEBT; taken from SYN-BIOGENESIS's src/synos, where it follows
 * SYN-DEBT's src/core/theme.h.
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

#ifndef SYNOS_THEME_H
#define SYNOS_THEME_H

/* SYN-OS themes: the same .theme palette files labwc, waybar, foot, mako and
 * the GTK/Qt templates are generated from (~/.config/syn-os/themes on SYN-OS,
 * assets/themes here). Every theme has the same fifteen keys; its family
 * decides how things are drawn, the way the waybar and labwc templates for
 * each family draw them:
 *
 *   VANILLA   flat panels, an accent bar marks the active thing
 *   BEVEL     gradients, 1px borders, a lit top edge, a drop shadow
 *   FLATLINE  no boxes at all, text straight on the background, accent text when active
 *   HALO      transparent panels with coloured outlines, active glows
 *   SLAB      chunky 2px borders on filled panels
 *
 * WIN95 and MATRIX sit in the bevel and flatline families but have their own
 * templates in SYN-OS, so they get their own styles here too. */

#include <stdbool.h>

typedef struct { unsigned char r, g, b; } Rgb;

typedef enum {
    STYLE_VANILLA = 0, STYLE_BEVEL, STYLE_FLATLINE, STYLE_HALO, STYLE_SLAB, STYLE_WIN95, STYLE_MATRIX, STYLE_COUNT
} ThemeStyle;

typedef struct {
    char name[64];     /* SYN_THEME_NAME, e.g. SYN-OS-HALO-CYAN */
    char family[32];   /* SYN_THEME_FAMILY, e.g. SYN-OS-HALO */
    char group[24];    /* SYN_THEME_GROUP, e.g. halo */
    char glyph[16];    /* SYN_GLYPH, UTF-8 */
    bool light;        /* SYN_THEME_MODE == light */
    Rgb bg, bg_alt, panel, panel_hover, accent, accent_dim, text, border, urgent;
    ThemeStyle style;
} Theme;

typedef struct { Theme *items; int count; } ThemeList;

/* SYN-OS-RED's values, what syn_theme.c falls back to when nothing can be read. */
void theme_fallback(Theme *out);

/* Parses KEY="value" lines over the fallback. False if SYN_THEME_NAME is missing. */
bool theme_parse(const char *text, Theme *out);

/* Loads every *.theme in dir, ordered by family (vanilla, bevel, flatline,
 * halo, slab), dark before light, then name. Returns the count. */
int  theme_load_dir(const char *dir, ThemeList *out);
void theme_list_free(ThemeList *l);
int  theme_find(const ThemeList *l, const char *name);   /* -1 if absent */

/* The theme SYN-OS currently has applied (~/.config/syn-os/current-theme),
 * written into buf. False when not running on SYN-OS. */
bool theme_system_current(char *buf, int len);

const char *theme_style_name(ThemeStyle s);
/* "HALO-CYAN" from "SYN-OS-HALO-CYAN": the name without its SYN-OS- prefix. */
const char *theme_short_name(const Theme *t);

#endif /* SYNOS_THEME_H */
