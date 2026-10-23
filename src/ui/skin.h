/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of the SYN-OS interface SYN-BIOGENESIS-3D shares with SYN-BIOGENESIS,
 * SYN-TREK and SYN-DEBT; taken from SYN-BIOGENESIS's src/synos, where it this file follows SYN-DEBT's src/ui/skin.h, drawn with
 * SDL_Renderer instead of OpenGL.
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
#ifndef SYNOS_SKIN_H
#define SYNOS_SKIN_H

/* Drawing in the active SYN-OS theme.
 *
 * Every piece of chrome (bars, panels, buttons, tabs, rows) goes through
 * here, so changing theme restyles the whole app at once. Colours come from
 * the theme's palette; the SHAPE of things comes from its family, following
 * what that family's waybar and labwc templates do in SYN-OS. */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stddef.h>

#include "ui/theme.h"

typedef struct { float x, y, w, h; } UIRect;

static inline UIRect ui_rect(float x, float y, float w, float h) {
    UIRect r = {x, y, w, h};
    return r;
}

/* Cell (col, row) of a cols x rows grid filling area with the given gaps. */
static inline UIRect ui_grid_cell(UIRect area, int cols, int rows, int col, int row, float gap_x,
                                  float gap_y) {
    float cw = (area.w - gap_x * (float)(cols - 1)) / (float)cols;
    float ch = (area.h - gap_y * (float)(rows - 1)) / (float)rows;
    return ui_rect(area.x + (cw + gap_x) * (float)col, area.y + (ch + gap_y) * (float)row, cw, ch);
}

typedef enum {
    TONE_TEXT = 0,   /* body text */
    TONE_MUTED,      /* labels, secondary figures */
    TONE_ACCENT,     /* the theme's accent, lifted until it reads on the background */
    TONE_GOOD,
    TONE_BAD,        /* the theme's urgent colour */
    TONE_WARN,
    TONE_ON_ACCENT,  /* text sitting on an accent fill */
    TONE_COUNT
} Tone;

typedef enum { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT } Align;

typedef struct {
    TTF_Font *title;      /* 52, the menu title */
    TTF_Font *button;     /* 24, menu buttons */
    TTF_Font *heading;    /* 20 bold */
    TTF_Font *big;        /* 28 bold */
    TTF_Font *body;       /* 16 */
    TTF_Font *bold;       /* 16 bold */
    TTF_Font *small;      /* 13 */
    TTF_Font *small_bold; /* 13 bold, table headers */
    TTF_Font *ui;         /* 14, the size Biogenesis' dialogs are laid out for */
    /* SYN-OS's own desktop: Terminus (SYN Terminal) as labwc, mako, foot
     * and wmenu write, for what the machine says, and Font Awesome, the
     * icons waybar draws. Noto Sans in their place when they can't be read. */
    TTF_Font *mono;            /* 16, the console */
    TTF_Font *mono_bold;       /* 16 bold */
    TTF_Font *mono_small;      /* 14, menus, notifications, key hints */
    TTF_Font *mono_small_bold; /* 14 bold */
    TTF_Font *icon;            /* 15 */
    TTF_Font *icon_big;        /* 20 */
} Fonts;

/* Opens the fonts (Noto Sans, from the assets) and the text cache. */
bool skin_init(const char *font_path, Fonts *out);
/* Then SYN-OS's: SYN Terminal regular and bold, and Font Awesome. A font
 * missing is Noto Sans instead, so nothing goes blank. */
void skin_init_desktop(const char *mono, const char *mono_bold, const char *icons, Fonts *out);
void skin_shutdown(Fonts *f);
/* Once per frame: lets the text cache drop strings no longer drawn. */
void skin_end_frame(void);

void skin_set_theme(const Theme *t);
const Theme *skin_theme(void);

/* Everything drawn is multiplied by this (the menu fades in). */
void skin_set_alpha(float a);
float skin_alpha(void);

void skin_tone_rgb(Tone t, Uint8 *r, Uint8 *g, Uint8 *b);
/* A tone, or a palette colour, as an SDL_Color */
SDL_Color skin_tone_color(Tone t);
SDL_Color skin_color(Rgb c, float alpha);

/* Text in a tone. Returns the width drawn. */
float skin_text(TTF_Font *f, const char *s, float x, float y, Tone tone, Align align);
/* Same, in any colour. */
float skin_text_rgb(TTF_Font *f, const char *s, float x, float y, Uint8 r, Uint8 g, Uint8 b, Align align);
float skin_text_width(TTF_Font *f, const char *s);
float skin_text_height(TTF_Font *f);
/* Shortens s with "..." until it fits max_w, into buf. */
const char *skin_fit(TTF_Font *f, const char *s, float max_w, char *buf, size_t len);

/* A filled rectangle in a palette colour */
void skin_fill(UIRect r, Rgb c, float alpha);

/* Chrome. `over_scene` makes fills translucent so the scene behind shows through. */
typedef struct { bool hover, active, pressed, disabled; } WidgetState;

void skin_background(float w, float h);
void skin_bar(UIRect r);                       /* the top bar, like waybar */
void skin_panel(UIRect r, bool over_scene);    /* a container: tiles, tables, dialogs */
void skin_button(UIRect r, TTF_Font *f, const char *label, WidgetState st, bool over_scene);
/* Same, with the label in a tone of its own (TONE_COUNT for the usual). */
void skin_button_tone(UIRect r, TTF_Font *f, const char *label, WidgetState st, bool over_scene, Tone label_tone);
/* A text box: sunk into the page, lit when it has the keyboard, red when refused. */
void skin_input(UIRect r, bool focused, bool hover, bool invalid);
void skin_tab(UIRect r, TTF_Font *f, const char *label, WidgetState st);
/* A button's or a tab's chrome alone, for a label of the caller's own (an
 * icon beside the words): the tone the label goes in. */
Tone skin_button_chrome(UIRect r, WidgetState st, bool over_scene);
Tone skin_tab_chrome(UIRect r, WidgetState st);
void skin_row_hover(UIRect r);
void skin_separator(float x, float y, float w);
void skin_meter(UIRect r, double fraction, Tone tone);   /* a thin progress bar */
void skin_swatches(UIRect r, const Theme *t);             /* a theme's palette at a glance */

/* ---- SYN-OS's desktop ---- */

/* A waybar module: the box a figure sits in on the bar, as the family's
 * waybar style draws it. level 0 is normal, 1 waybar's .warning, 2 its
 * .critical (which blinks). Returns the tone to write in. */
Tone skin_module(UIRect r, WidgetState st, int level);
/* A readout's well, sunk into the panel (SYN-TREK's), in the family's way. */
void skin_well(UIRect r);
/* A key, as the hints and buttons show it: "P", "Ctrl+S". Width for f. */
float skin_keycap_width(TTF_Font *f, const char *key);
void skin_keycap(UIRect r, TTF_Font *f, const char *key);
/* A notification, as mako draws it on SYN-OS: the panel colour inside a
 * 3px border in the accent, or the urgent colour. */
void skin_note(UIRect r, bool urgent);
/* The theme's SYN_GLYPH (the bar's launcher mark), size px high, centred
 * on cx, cy, in the accent; icons is the font for the glyphs that are
 * Font Awesome's. */
void skin_glyph(float cx, float cy, float size, TTF_Font *icons);
/* A title bar, as the family's labwc theme draws an active window's:
 * the colour its label goes in. */
Rgb skin_titlebar(UIRect r);
/* A tone's colour */
Rgb skin_tone(Tone t);
/* Strokes for the shapes that aren't boxes: a line w thick. */
void skin_line(float x0, float y0, float x1, float y1, float w, Rgb c, float alpha);

/* Hover test with SDL mouse coordinates. */
static inline bool skin_hit(UIRect r, int mx, int my) {
    return (float)mx >= r.x && (float)mx < r.x + r.w && (float)my >= r.y && (float)my < r.y + r.h;
}

#endif /* SYNOS_SKIN_H */
