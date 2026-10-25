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
#define ICON_SLIDERS "\xef\x87\x9e"
#define ICON_BOOKMARK "\xef\x80\xae"
#define ICON_CHART "\xef\x88\x81"
#define ICON_TROPHY "\xef\x82\x91"
#define ICON_SAVE "\xef\x83\x87"
#define ICON_FORWARD "\xef\x81\x90"
#define ICON_SUN "\xef\x86\x85"
#define ICON_MOON "\xef\x86\x86"
#define ICON_STEP "\xef\x81\x91"
#define ICON_FIRE "\xef\x81\xad"
#define ICON_DISPLAY "\xee\x85\xa3"
#define ICON_SEARCH "\xef\x80\x82"
#define ICON_OPEN "\xef\x81\xbc"
#define ICON_FOLDER "\xef\x81\xbb"
#define ICON_FILE "\xef\x80\x96"
#define ICON_IMPORT "\xef\x95\xaf"
#define ICON_EXPORT "\xef\x95\xae"
#define ICON_FLASK "\xef\x83\x83"
#define ICON_BOOK "\xef\x80\xad"
#define ICON_BINOCULARS "\xef\x87\xa5"
#define ICON_BARS "\xef\x83\x89"
#define ICON_SEEDLING "\xef\x93\x98"
#define ICON_WAND "\xee\x8b\x8a"
#define ICON_PASTE "\xef\x83\xaa"
#define ICON_COPY "\xef\x83\x85"
#define ICON_IMAGE "\xef\x80\xbe"
#define ICON_HEART_CRACK "\xef\x9e\xa9"
#define ICON_HEART_PULSE "\xef\x88\x9e"
#define ICON_SITEMAP "\xef\x83\xa8"
#define ICON_CHECK "\xef\x80\x8c"
#define ICON_GEAR "\xef\x80\x93"
#define ICON_MAP "\xef\x89\xb8"
#define ICON_EXPAND "\xef\x81\xa5"
#define ICON_INFO "\xef\x81\x9a"
#define ICON_KEYBOARD "\xef\x84\x9c"
#define ICON_MICROSCOPE "\xef\x98\x90"
#define ICON_WIND "\xef\x9c\xae"
#define ICON_TRASH "\xef\x87\xb8"
#define ICON_ZOOM_IN "\xef\x80\x8e"
#define ICON_ZOOM_OUT "\xef\x80\x90"
#define ICON_BACKUP "\xef\x87\x9a"

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
    const char *section; /* its heading in the command list: "WORLD", "VIEW" */
    const char *word;    /* its name in the console, or NULL */
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
    ShellCommand actions[12];
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
/* The themes one by one, the one in use, and choosing one (kept). */
int shell_theme_count(void);
/* The window's other choices, kept with the theme (NULL: not set). */
const char *shell_setting(const char *key);
void shell_setting_set(const char *key, const char *value);
const Theme *shell_theme_at(int i);
int shell_theme_index(void);
void shell_theme_pick(int i);

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

/* The command list (/, or the bar's glyph): every command under its
 * heading, filtered by what is typed; up and down pick, Enter runs, Esc
 * closes. Returns the id of the command run this frame, or 0. */
void shell_launcher_open(void);
bool shell_launcher_showing(void);
int shell_launcher(int w, int h, const ShellCommand *c, int n);
/* The command with this console word, or NULL; every command by its word,
 * printed in the console under its heading. */
const ShellCommand *shell_lookup(const ShellCommand *c, int n, const char *word);
void shell_list(const ShellCommand *c, int n);

/* A value that can be typed over, drawn right-aligned in r. A click on it
 * (or Tab in the field drawn before it) makes it a line to type in, the
 * whole value selected; Enter, Tab or a click elsewhere takes the line:
 * true, with it in out. Escape gives the value back. While a field has the
 * keyboard, keys go to it, not the window. id names the field from frame
 * to frame. */
bool shell_field(const void *id, UIRect r, TTF_Font *f, const char *value, char *out, size_t n);
/* The same for words: drawn from the left (a name). */
bool shell_text_field(const void *id, UIRect r, TTF_Font *f, const char *value, char *out, size_t n);
/* The field takes the keyboard from the next frame, its value selected, as
 * if clicked (a name to type in a file browser). */
void shell_field_focus(const void *id, const char *value);
/* Whether this field (NULL: any) has the keyboard. */
bool shell_field_active(const void *id);

#endif
