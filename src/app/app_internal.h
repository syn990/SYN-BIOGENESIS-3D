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

/* What the window's two halves share: app.c (the loop, the camera, the
 * scene) and interface.c (the SYN-OS interface over it). */
#ifndef APP_APP_INTERNAL_H
#define APP_APP_INTERNAL_H

#include <stdbool.h>

#include "app/arena.h"
#include "app/history.h"
#include "app/library.h"
#include "options.h"
#include "platform/window.h"
#include "render/capsules.h"
#include "render/lines.h"
#include "sim/world.h"
#include "ui/skin.h"

typedef enum { FOLLOW_NONE, FOLLOW_LIFE, FOLLOW_SELECTED } Follow;

/* Everything the window can do: keys, the bar, menus, the card and the
 * console all come here (app_do). */
enum {
    ACT_NONE,
    ACT_MENU,
    ACT_PAUSE,
    ACT_STEP,
    ACT_FASTER,
    ACT_SLOWER,
    ACT_NEXT_SEED,
    ACT_RESTART,
    ACT_HOME,
    ACT_FOLLOW,
    ACT_DESELECT,
    ACT_KILL,
    ACT_THEME_NEXT,
    ACT_THEME_PREV,
    ACT_CONSOLE,
    ACT_HINTS,
    ACT_QUIT,
    ACT_WORLD_TAB,
    ACT_LIBRARY_TAB,
    ACT_PARAMS_TAB,
    ACT_STATS_TAB,
    ACT_ARENA_TAB,
    ACT_KEEP,
    ACT_SAVE,
};

typedef enum { SCREEN_WORLD, SCREEN_LIBRARY, SCREEN_PARAMS, SCREEN_STATS, SCREEN_ARENA } Screen;

/* world frames per second; the last is "as many as fit" */
extern const int k_speeds[];
extern const int k_nspeeds;

typedef struct App {
    PlatformWindow win;
    World w;
    WorldOptions wo;
    int warm;
    bool running;
    bool interface; /* the SYN-OS interface is up (fonts found) */
    bool hints;     /* the key hints along the bottom */
    Screen screen;
    /* the library screen */
    Library lib;
    bool lib_read;
    int lib_folder, lib_sel;
    float lib_scroll;
    float params_scroll;
    History hist;
    Arena *arena;
    char loaded[128]; /* the saved world it came from, "" for a seed's */
    /* time */
    int speed;
    bool paused;
    double acc;
    double fps;
    /* camera */
    double yaw, pitch, distance;
    V3 aim;
    Follow follow;
    int selected; /* organism id, -1 for none */
    bool selected_was_alive;
    double alpha; /* this picture's place between the world's last two frames */
    double seconds;
    /* drawing scratch */
    CapsuleInstance *inst;
    int *inst_org;
    int ninst, inst_cap;
    LineVertex *lines;
    int nlines, lines_cap;
    SceneView view;
    V3 cam_eye, cam_fwd, cam_right, cam_up;
} App;

void app_do(App *a, int action);
bool app_make_world(App *a);
/* One world frame, and the history's sample when a time unit ends. */
void app_step(App *a);
/* ~/.config/syn-biogenesis-3d/worlds/NAME.b3w (name: NULL for one made
 * from the seed and the time); false with err set. */
bool app_save_world(App *a, const char *name, char *path, size_t pathlen, char *err, int errlen);
/* NAME (in the worlds folder) or a path. */
bool app_load_world(App *a, const char *name, char *err, int errlen);
/* "name  name ..." of the saved worlds, for the console */
void app_list_worlds(void (*line)(const char *s));
Organism *app_find(const World *w, int id);
void app_draw_pose(const Organism *o, double alpha, V3 *pos, Quat *q);
const char *app_speed_text(const App *a);

/* interface.c */
bool interface_init(App *a, const char *assets);
void interface_shutdown(void);
/* After the scene: draws the interface and does what was clicked. */
void interface_draw(App *a, int w, int h);
/* The menu for a right click at (x, y): an organism's or the world's. */
void interface_context_menu(App *a, float x, float y, int organism);
/* What happened, said where it can be seen. */
void interface_note(App *a, bool urgent, const char *fmt, ...);

/* screens.c: the screens under the bar, over the world */
/* A screen's panel under the bar, with its title bar; returns its inside. */
UIRect screen_panel(int w, int h, bool hints, const char *title, const char *subtitle);
/* A button in the skin; true when clicked. */
bool screen_button(UIRect b, const char *icon, const char *label, bool disabled, const char *tip);
void library_screen(App *a, int w, int h);
void params_screen(App *a, int w, int h);
void stats_screen(App *a, int w, int h);
/* The parameter called name (Params' field name), set from text: false if
 * there is none or the value is out of its range. */
bool params_set_text(const char *name, const char *value, char *err, int errlen);
void params_print(void);

#endif
