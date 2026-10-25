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
#include "syn/arena.h"
#include "syn/history.h"
#include "app/library.h"
#include "app/telescope.h"
#include "options.h"
#include "platform/window.h"
#include "render/bodies.h"
#include "render/lines.h"
#include "render/water.h"
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
    ACT_BEING_COPY, /* a->menu_gc: its code on the clipboard */
    ACT_BEING_KEEP, /* a->menu_gc: kept in the library's Arena folder */
    /* SYN-BIOGENESIS's commands */
    ACT_COMMANDS,  /* the command list */
    ACT_MAIN_MENU,
    ACT_OPTIONS,
    ACT_FULLSCREEN,
    ACT_ABOUT,
    ACT_MANUAL,    /* Biogenesis's manual, in the browser */
    ACT_NEW,       /* a new world from a new seed */
    ACT_OPEN,      /* a saved world */
    ACT_SAVE_AS,
    ACT_INC_CO2,
    ACT_DEC_CO2,
    ACT_KILL_ALL,
    ACT_DISPERSE_ALL,
    ACT_CREATE_RANDOM, /* at a->place */
    ACT_PASTE,         /* the clipboard, at a->place */
    ACT_IMPORT,        /* a .bgg, at a->place */
    ACT_FEED,
    ACT_WEAKEN,
    ACT_REPRODUCE,
    ACT_REJUVENATE,
    ACT_COPY,
    ACT_EXPORT,
    ACT_SAVE_IMAGE,
    ACT_REVIVE,
    ACT_DISPERSE,
    ACT_ZOOM_IN,
    ACT_ZOOM_OUT,
    ACT_UNTRACK,
    ACT_FAMILY, /* the selected one's species in the family tree */
    ACT_LAB_TAB,    /* the laboratory, with the code being made or the copied one */
    ACT_GENES,      /* the selected one's genes in the laboratory */
    ACT_BEING_LAB,  /* a->menu_gc in the laboratory */
    ACT_TELESCOPE_TAB,
    ACT_WHERE,       /* where the selected one lives, in the telescope */
    ACT_BEING_WHERE, /* where a->menu_gc lives */
    ACT_CREATE_WINNER, /* a being worked out for the world as it is, at a->place (syn/winner.h) */
    ACT_LOOK_NEXT,
    ACT_MINIMAP,
    ACT_ZOOM_1, /* a world unit a pixel at the aim */
    ACT_LOOK,          /* + a Look (app/looks.h) */
    ACT_LOOK_LAST = ACT_LOOK + 3,
    ACT_LIBRARY_PICK = 1000, /* + k: a pick from one of the library's menus (library_pick) */
};

typedef enum {
    SCREEN_WORLD,
    SCREEN_LIBRARY,
    SCREEN_PARAMS,
    SCREEN_STATS,
    SCREEN_ARENA,
    SCREEN_MENU,    /* the main menu, over the living world */
    SCREEN_OPTIONS,
    SCREEN_FILES,   /* opening and saving: worlds, beings, pictures */
    SCREEN_LAB,     /* the genetic laboratory */
    SCREEN_TELESCOPE,
} Screen;

/* The genetic laboratory's code being made (src/app/lab.c): kept while
 * other screens are looked at, until it is copied or cancelled. */
#define LAB_MAX_GENES 100
typedef struct {
    bool open;
    Gene genes[LAB_MAX_GENES];
    int ngenes, symmetry, mirror, disperse;
    double drive[DRIVE_COUNT];
    double height;
    int plan;
    double cone;
    int scroll;
    /* how the body is seen: it turns by itself, and is dragged round and
     * tilted by hand */
    double turn, tilt, last_seconds;
    bool dragging;
    float drag_x, drag_y;
} Lab;

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
    float params_scroll;
    Lab lab;
    /* where a new organism goes: the water under a right click, or under
     * the pointer (place_set), else the middle of the view */
    V3 place;
    bool place_set;
    bool fullscreen;
    /* a genetic code copied (held), and the being a menu was opened on */
    GeneticCode *clipboard, *menu_gc;
    char menu_name[64], menu_origin[200], menu_folder[48];
    char world_path[1100]; /* the world file it was last opened from or saved to ("": none) */
    char loaded[128]; /* the saved world it came from, "" for a seed's */
    int64_t ff_target; /* a frame being run to as fast as it goes, then paused; 0 none */
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
    BodyInstance *inst; /* in order: ntier[0] fine, ntier[1] coarse, ntier[2] lines */
    int *inst_org;
    int ninst, inst_cap, ntier[3];
    BodyInstance *tier[3]; /* each tier's, as they are built */
    int *tier_org[3];
    int tier_cap[3];
    LineVertex *lines;
    int nlines, lines_cap;
    WaterShadow *shadows;
    int nshadows, shadows_cap;
    WaterPoint *points;
    int npoints, points_cap;
    SceneView view;
    V3 cam_eye, cam_fwd, cam_right, cam_up;
} App;

void app_do(App *a, int action);
bool app_make_world(App *a);
/* One world frame, and the history's sample when a time unit ends. */
void app_step(App *a);
/* Run the world flat out to frame (time units x 256) and pause there,
 * drawing as it goes. */
void app_run_to(App *a, int64_t frame);
/* ~/.config/syn-biogenesis-3d/worlds/NAME.b3w (name: NULL for one made
 * from the seed and the time); false with err set. */
bool app_save_world(App *a, const char *name, char *path, size_t pathlen, char *err, int errlen);
/* NAME (in the worlds folder) or a path. */
bool app_load_world(App *a, const char *name, char *err, int errlen);
/* "name  name ..." of the saved worlds, for the console */
void app_list_worlds(void (*line)(const char *s));
Organism *app_find(const World *w, int id);
void app_draw_pose(const Organism *o, double alpha, V3 *pos, Quat *q);
/* An arena round's world made again from its parameters, seed and
 * fighters, to be watched: P becomes the round's parameters. */
bool app_watch_round(App *a, const ArenaRoundSpec *s);
/* The point in the water under (x, y): where the pointer's ray crosses the
 * plane through the middle of the view, kept inside the box. */
V3 app_point_under(App *a, float x, float y);
/* Where the next organism made goes (a->place, used once), else the middle
 * of the view. */
V3 app_take_place(App *a);
/* ~/.config/syn-biogenesis-3d/worlds (not made here) */
void app_worlds_dir(char *out, size_t n);

/* Opening and saving (src/app/files.c): SYN-BIOGENESIS's file browser, the
 * folder above, this one and a preview, as a screen; what is chosen is
 * done there. */
enum {
    FILES_OPEN_WORLD,
    FILES_SAVE_WORLD,
    FILES_IMPORT_PASTE,
    FILES_EXPORT_GENES,
    FILES_SAVE_IMAGE,
    FILES_IMPORT_LAB,
    FILES_IMPORT_LIBRARY,
    FILES_TELESCOPE_WORLD,
    FILES_PURPOSES
};
void files_open(App *a, int purpose);
void files_screen(App *a, int w, int h);
/* A code placed as a founder at p (pasted, imported, created at random);
 * false, with a note, if there is no room there. */
bool app_paste(App *a, GeneticCode *gc, V3 p);
/* The menu of a being shown on a screen (a code, not an organism): copy it,
 * keep it in the library's folder. origin says where it came from, for the
 * library. */
void app_being_menu(App *a, const GeneticCode *gc, const char *name, const char *origin, const char *folder);
const char *app_speed_text(const App *a);

/* The genetic laboratory (lab.c). lab_open starts it on gc (NULL: no
 * genes) and shows it; lab_close lets its code go. */
void lab_open(App *a, const GeneticCode *gc);
void lab_load_code(App *a, const GeneticCode *gc);
void lab_close(App *a);
/* The code made (NULL while it has no genes); disperse: as its children
 * are set to be born, else kept close. */
GeneticCode *lab_code(const App *a, bool disperse);
void lab_screen(App *a, int w, int h);

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
/* A code's grown body in box, turned seconds' worth about its axis and
 * tilted toward the eye, in 0.9's colours. */
void screen_code(const GeneticCode *gc, UIRect box, double seconds);
/* The same, turned turn radians about its axis and tilted tilt (0: seen
 * from above, pi/2 side on). */
void screen_code_view(const GeneticCode *gc, UIRect box, double turn, double tilt);
/* What screen_code_view draws, for a picture aspect wide to its height,
 * px_h pixels high: the eye in *v and the flesh in *body (kept until the
 * next call); how many instances, 0 for none. */
int screen_code_scene(const GeneticCode *gc, double aspect, double px_h, double turn, double tilt, SceneView *v,
                      const BodyInstance **body);
/* "1,234" */
void fmt_num(char *out, size_t n, double v);
void params_screen(App *a, int w, int h);
void menu_screen(App *a, int w, int h);
/* The organism alone as the camera sees it, size x size, as a PNG. */
bool organism_image(const App *a, const Organism *o, int size, const char *path, char *err, int errlen);
/* A code's grown body as screen_code draws it (unturned), as a PNG on
 * black. */
bool code_image(const GeneticCode *gc, int size, const char *path);
void options_screen(App *a, int w, int h);
void stats_screen(App *a, int w, int h);
/* The statistics' FAMILY tab, on o's species. */
void stats_family(App *a, const Organism *o);
/* o's species by name ("VELORAX-3"), "" if none yet. */
void stats_species(const Organism *o, char *out, size_t n);
void stats_screen_free(void);
/* The parameter called name (Params' field name), set from text: false if
 * there is none or the value is out of its range. */
bool params_set_text(const char *name, const char *value, char *err, int errlen);
void params_print(void);

#endif
