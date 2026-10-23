/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * The pieces follow SYN-BIOGENESIS's src/syn/shell.c, redrawn here over the
 * 3D world with this program's own input (ui/input.c).
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

/* The SYN-OS interface over the world, made of SYN-OS's own desktop:
 *
 *   the bar        waybar's: the theme's glyph and the name, the screens as
 *                  tabs, the world's figures as modules, MENU
 *   notifications  mako's, top right
 *   the menu       labwc's
 *   the console    a drop-down foot with a command line (`)
 *   the hints      SYN-OS's TUIs' status line, along the bottom
 *   the card       the selected organism, SYN-TREK's target card
 *
 * The look is the SYN-OS theme in use: the one SYN-OS has on unless another
 * is chosen. Text the machine writes is in SYN Terminal (Terminus); the
 * icons are Font Awesome's, as waybar's. */
#ifndef UI_SHELL_H
#define UI_SHELL_H

#include <stdbool.h>

#include "ui/skin.h"

/* Font Awesome's glyphs, as UTF-8 (SYN-BIOGENESIS's set) */
#define ICON_PLAY "\xef\x81\x8b"
#define ICON_PAUSE "\xef\x81\x8c"
#define ICON_CLOCK "\xef\x80\x97"
#define ICON_USERS "\xef\x83\x80"
#define ICON_CLOUD "\xef\x83\x82"
#define ICON_SMOG "\xef\x9d\x9f"
#define ICON_GAUGE "\xef\x8f\xbd"
#define ICON_SKULL "\xef\x95\x8c"
#define ICON_CROSSHAIRS "\xef\x81\x9b"
#define ICON_GLOBE "\xef\x82\xac"
#define ICON_PALETTE "\xef\x94\xbf"
#define ICON_TERMINAL "\xef\x84\xa0"
#define ICON_EYE "\xef\x81\xae"
#define ICON_XMARK "\xef\x80\x8d"
#define ICON_DICE "\xef\x94\xa2"
#define ICON_ROTATE "\xef\x83\xa2"
#define ICON_HOUSE "\xef\x80\x95"
#define ICON_POWER "\xef\x80\x91"
#define ICON_PLUS "\xef\x81\xa7"
#define ICON_MINUS "\xef\x81\xa8"
#define ICON_DNA "\xef\x91\xb1"
#define ICON_VIRUS "\xee\x81\xb4"
#define ICON_LEAF "\xef\x81\xac"
#define ICON_BOLT "\xef\x83\xa7"

#define SHELL_BAR_H 44
#define SHELL_HINTS_H 26

/* Something the window can do: in the menu, on the card. */
typedef struct {
    int id; /* the window's own action; 0 with a label is a heading, "-" a rule */
    const char *label;
    const char *key; /* the key that does it, or NULL */
    const char *icon;
    bool disabled;
    bool on;
    const char *tip;
} ShellCommand;

typedef struct {
    const char *label;
    const char *icon;
    int id;
    bool active, disabled;
} ShellTab;

typedef struct {
    const char *icon;
    char text[48];
    const char *tip;
    int level; /* 0; 1 waybar's .warning; 2 .critical */
    int id;    /* what a click does, or 0 */
} ShellModule;

typedef struct {
    const char *label;
    char value[32];
    Tone tone;
} ShellCell;

typedef struct {
    char title[48];
    const char *state;
    Tone state_tone;
    char subtitle[96];
    ShellCell cells[12];
    int ncells;
    const char *bar_label[2];
    double bar[2];
    Tone bar_tone[2];
    char kind[48];
    char about[300];
    ShellCommand actions[8];
    int nactions;
    void (*draw_body)(void *ud, UIRect box);
    void *ud;
} ShellCard;

typedef enum { SHELL_OUT, SHELL_ECHO, SHELL_NOTE, SHELL_ERROR, SHELL_HEAD, SHELL_GOOD } ShellKind;

/* Fonts and themes from assets (fonts/, themes/), the theme from the saved
 * choice, else SYN-OS's own, else SYN-OS-RED. False without fonts. */
bool shell_init(const char *assets);
void shell_shutdown(void);
const Fonts *shell_fonts(void);

const char *shell_theme_name(void);
bool shell_theme_set(const char *name);
void shell_theme_step(int by);
/* "name  name  ..." of every theme, for the console */
void shell_theme_list(void);

/* The frame's interface: begin after the scene, end last. */
void shell_begin(int w, int h);
void shell_end(int w, int h);

/* Returns the id clicked (a tab's, a module's, menu_id), or 0. */
int shell_bar(int w, const char *name, const ShellTab *tabs, int ntabs, const ShellModule *mods,
              int nmods, int menu_id);
/* keys: pairs of a key and what it does */
void shell_hints(int w, int h, const char *const *keys, int nkeys, const char *right);

void shell_notify(bool urgent, const char *fmt, ...);
void shell_notifications(int w, int top);

/* Returns the id of the action clicked, or 0. */
int shell_card(int w, int h, const ShellCard *c);

void shell_menu_open(float x, float y, const char *title, const ShellCommand *items, int n);
bool shell_menu_showing(void);
/* Returns the id of the item picked, or 0. */
int shell_menu(int w, int h);

void shell_console_toggle(void);
bool shell_console_showing(void);
void shell_print(ShellKind kind, const char *fmt, ...);
/* The line entered this frame, or NULL. words: what Tab completes. */
const char *shell_console(int w, int top, int h, const char *const *words, int nwords);
/* Splits a line into words in place ("quoted words" kept whole). */
int shell_split(char *line, char **argv, int max);

#endif
