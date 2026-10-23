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

/* The SYN-OS interface over the world (ui/shell.c's pieces, filled in):
 * the bar with the world's figures, the selected organism's card, the
 * menus, the console's commands and the key hints. Everything it does goes
 * through app_do or the console's own commands below. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app/app_internal.h"
#include "guide/guide.h"
#include "guide/kind.h"
#include "sim/params.h"
#include "ui/input.h"
#include "ui/shell.h"

static char g_assets[1024];

/* where the assets are, for the kinds' book of words (guide/kind.c) */
const char *gfx_asset(const char *rel)
{
    static char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_assets, rel);
    return path;
}

bool interface_init(App *a, const char *assets)
{
    (void)a;
    snprintf(g_assets, sizeof g_assets, "%s", assets);
    if (!shell_init(assets))
        return false;
    shell_print(SHELL_HEAD, "SYN-BIOGENESIS-3D");
    shell_print(SHELL_NOTE, "Biogenesis 0.9's rules in a box. 'help' lists the commands.");
    return true;
}

void interface_shutdown(void)
{
    shell_shutdown();
}

void interface_note(App *a, bool urgent, const char *fmt, ...)
{
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (a->interface)
        shell_notify(urgent, "%s", text);
}

/* ---- what an organism is ---- */

/* SYN-BIOGENESIS's kind (guide/kind.c: SYN-TREK's way of naming planets,
 * read from what the genes do) and its sentence, then whether the body is
 * flat, as 0.9 grew them, or bent into the third dimension. */
static void describe(const Organism *o, char *kind, size_t kn, char *about, size_t an)
{
    syn_kind_label(o->gc, kind, kn);
    char sentence[256];
    syn_kind_describe(o->gc, sentence, sizeof sentence);
    snprintf(about, an, "%s %s", sentence,
             gc_is_flat(o->gc) ? "Flat, as 0.9 grew them." : "Bent into the third dimension.");
}

/* ---- the card ---- */

/* The organism's body in the card's well, turning slowly about its own up
 * and tilted toward the eye, in 0.9's colours. */
static void draw_body(void *ud, UIRect box)
{
    App *a = ud;
    const Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    if (!o || o->radius <= 0)
        return;
    double turn = a->seconds * 0.7, tilt = 0.55;
    double ct = cos(turn), st = sin(turn), cx = cos(tilt), sx = sin(tilt);
    double scale = (box.w * 0.5 - 8) / (o->radius - P.segment_radius + 1);
    float mx = box.x + box.w * 0.5f, my = box.y + box.h * 0.5f;
    for (int s = 0; s < o->segments; s++) {
        V3 p[2] = {o->bs[s], o->be[s]};
        float sxy[2][2];
        for (int k = 0; k < 2; k++) {
            double x = p[k].x * ct - p[k].y * st, y = p[k].x * st + p[k].y * ct, z = p[k].z;
            double yy = y * cx - z * sx;
            sxy[k][0] = mx + (float)(x * scale);
            sxy[k][1] = my - (float)(yy * scale);
        }
        uint8_t r, g, b;
        bio_color_rgb(o->seg_color[s], &r, &g, &b);
        skin_line(sxy[0][0], sxy[0][1], sxy[1][0], sxy[1][1], 2.0f, (Rgb){r, g, b}, 1);
    }
}

static void card(App *a, int w, int h)
{
    Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    if (!o)
        return;
    ShellCard c;
    memset(&c, 0, sizeof c);
    snprintf(c.title, sizeof c.title, "#%d", o->id);
    if (!o->alive) {
        c.state = "DEAD";
        c.state_tone = TONE_BAD;
    } else if (o->infected_gc) {
        c.state = "INFECTED";
        c.state_tone = TONE_WARN;
    } else {
        c.state = "ALIVE";
        c.state_tone = TONE_GOOD;
    }
    if (o->parent_id >= 0)
        snprintf(c.subtitle, sizeof c.subtitle, "generation %d, child of #%d", o->generation,
                 o->parent_id);
    else
        snprintf(c.subtitle, sizeof c.subtitle, "a founder%s",
                 o->gc->lineage == 1 ? ", imported" : "");
    int max_age = o->gc->max_age + 1;
    struct {
        const char *label;
        char value[32];
        Tone tone;
    } cells[12];
    int n = 0;
#define CELL(lbl, tone_, ...)                                                                     \
    do {                                                                                           \
        cells[n].label = lbl;                                                                      \
        snprintf(cells[n].value, sizeof cells[n].value, __VA_ARGS__);                              \
        cells[n].tone = tone_;                                                                     \
        n++;                                                                                       \
    } while (0)
    CELL("Energy", TONE_TEXT, "%.1f", o->energy);
    CELL("Age", TONE_TEXT, "%d / %d", o->age >> 8, max_age);
    CELL("Children", TONE_TEXT, "%d", o->total_children);
    CELL("Kills", o->total_kills ? TONE_BAD : TONE_TEXT, "%d", o->total_kills);
    CELL("Infected", o->total_infected ? TONE_WARN : TONE_TEXT, "%d", o->total_infected);
    CELL("Mass", TONE_TEXT, "%.0f", o->mass);
    CELL("Genes", TONE_TEXT, "%d x %d", o->gc->ngenes, o->gc->symmetry);
    CELL("Shape", gc_is_flat(o->gc) ? TONE_TEXT : TONE_ACCENT, "%s",
         gc_is_flat(o->gc) ? "flat" : "bent");
    CELL("Growth", TONE_TEXT, "%s", o->growth_ratio == 1 ? "adult" : "growing");
    CELL("Speed", TONE_TEXT, "%.2f", v3_len(o->v));
    CELL("Level", TONE_TEXT, "%.2f", org_levelness(o));
    CELL("Mirror", TONE_TEXT, "%s", o->gc->mirror ? "yes" : "no");
#undef CELL
    for (int i = 0; i < n; i++) {
        c.cells[i].label = cells[i].label;
        snprintf(c.cells[i].value, sizeof c.cells[i].value, "%s", cells[i].value);
        c.cells[i].tone = cells[i].tone;
    }
    c.ncells = n;
    c.bar_label[0] = "LIFE";
    c.bar[0] = o->alive ? 1.0 - (double)o->age / (256.0 * max_age) : 0;
    c.bar_tone[0] = TONE_GOOD;
    c.bar_label[1] = "TO A CHILD";
    c.bar[1] = o->energy / (o->gc->reproduce_energy > 0 ? o->gc->reproduce_energy : 1);
    c.bar_tone[1] = TONE_ACCENT;
    describe(o, c.kind, sizeof c.kind, c.about, sizeof c.about);
    c.actions[c.nactions++] = (ShellCommand){ACT_FOLLOW, "Follow", "F", ICON_EYE, false,
                                             a->follow == FOLLOW_SELECTED,
                                             "Keep the camera on it"};
    c.actions[c.nactions++] = (ShellCommand){ACT_KILL, "Kill", "K", ICON_SKULL, !o->alive, false,
                                             "It dies where it is"};
    c.actions[c.nactions++] = (ShellCommand){ACT_KEEP, "Keep", NULL, ICON_BOOKMARK, false, false,
                                             "Keep its code in the library (Kept)"};
    c.actions[c.nactions++] = (ShellCommand){ACT_DESELECT, "Release", "Esc", ICON_XMARK, false,
                                             false, "Let it go"};
    c.draw_body = draw_body;
    c.ud = a;
    int got = shell_card(w, h, &c);
    if (got)
        app_do(a, got);
}

/* ---- menus ---- */

static void main_menu(App *a, float x, float y)
{
    ShellCommand items[] = {
        {0, "WORLD", NULL, NULL, false, false, NULL},
        {ACT_PAUSE, a->paused ? "Go on" : "Pause", "Space", a->paused ? ICON_PLAY : ICON_PAUSE,
         false, false, NULL},
        {ACT_STEP, "One frame", ".", NULL, false, false, NULL},
        {ACT_FASTER, "Faster", "Up", ICON_PLUS, false, false, NULL},
        {ACT_SLOWER, "Slower", "Down", ICON_MINUS, false, false, NULL},
        {ACT_NEXT_SEED, "Next seed", "N", ICON_DICE, false, false, NULL},
        {ACT_RESTART, "Restart", "R", ICON_ROTATE, false, false, NULL},
        {0, "SCREENS", NULL, NULL, false, false, NULL},
        {ACT_LIBRARY_TAB, "Library", "L", ICON_DNA, false, a->screen == SCREEN_LIBRARY, NULL},
        {ACT_PARAMS_TAB, "Parameters", "P", ICON_SLIDERS, false, a->screen == SCREEN_PARAMS, NULL},
        {0, "VIEW", NULL, NULL, false, false, NULL},
        {ACT_FOLLOW, "Follow", "F", ICON_EYE, false, a->follow != FOLLOW_NONE, NULL},
        {ACT_HOME, "The whole box", "Home", ICON_HOUSE, false, false, NULL},
        {ACT_THEME_NEXT, "Next theme", "T", ICON_PALETTE, false, false, NULL},
        {ACT_HINTS, a->hints ? "Hide the key hints" : "Show the key hints", "H", NULL, false,
         false, NULL},
        {ACT_CONSOLE, "Console", "`", ICON_TERMINAL, false, false, NULL},
        {0, "-", NULL, NULL, false, false, NULL},
        {ACT_QUIT, "Quit", "Q", ICON_POWER, false, false, NULL},
    };
    shell_menu_open(x, y, "SYN-BIOGENESIS-3D", items, (int)(sizeof items / sizeof items[0]));
}

void interface_context_menu(App *a, float x, float y, int organism)
{
    if (organism < 0) {
        main_menu(a, x, y);
        return;
    }
    a->selected = organism;
    Organism *o = app_find(&a->w, organism);
    a->selected_was_alive = o && o->alive;
    char title[32];
    snprintf(title, sizeof title, "#%d", organism);
    ShellCommand items[] = {
        {ACT_FOLLOW, "Follow", "F", ICON_EYE, false, a->follow == FOLLOW_SELECTED, NULL},
        {ACT_KILL, "Kill", "K", ICON_SKULL, !(o && o->alive), false, NULL},
        {ACT_KEEP, "Keep in the library", NULL, ICON_BOOKMARK, false, false, NULL},
        {ACT_DESELECT, "Release", "Esc", ICON_XMARK, false, false, NULL},
    };
    shell_menu_open(x, y, title, items, 4);
}

/* ---- the console ---- */

static const char *const k_words[] = {
    "help",  "seed",   "next",  "restart", "size",   "pitch",  "organisms", "co2",
    "import", "speed", "pause", "go",      "step",   "theme",  "themes",    "follow",
    "home",  "select", "kill",  "genes",   "hints",  "clear",  "quit",      "colour",
    "guide", "kind",   "kinds", "set",     "params", "library", "keep",
};

static void help(void)
{
    static const char *const lines[] = {
        "WORLD",
        "  seed N             a new world from seed N        next / restart",
        "  size W H D         a new world of that box        pitch P (0-100)",
        "  organisms N        founders in a new world        co2 X",
        "  import FILE [K]    a new world with K copies of a 0.9 being (.bgg)",
        "TIME",
        "  speed N|max        world frames a second          pause / go",
        "  step [N]           N frames, paused",
        "VIEW",
        "  follow / home      the camera                     theme [NAME] / themes",
        "  select ID / kill   an organism                    genes: the selected one's",
        "PARAMETERS AND BEINGS",
        "  params             every parameter and its value  set NAME VALUE",
        "  library            the beings' screen             keep: keep the selected one",
        "FIELD GUIDE",
        "  guide              what every colour does         colour NAME: one, at length",
        "  kind               what the selected one is       kinds: every kind",
        "  hints / clear / quit",
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        shell_print(lines[i][0] == ' ' ? SHELL_OUT : SHELL_HEAD, "%s", lines[i]);
}

static void new_world(App *a)
{
    char err[512];
    if (!world_options_check(err, sizeof err)) {
        shell_print(SHELL_ERROR, "%s", err);
        return;
    }
    if (app_make_world(a)) {
        app_do(a, ACT_HOME);
        interface_note(a, false, "Seed %lld: %d organisms in %d x %d x %d", (long long)a->wo.seed,
                       a->w.population, a->w.width, a->w.height, a->w.depth);
    } else {
        shell_print(SHELL_ERROR, "no world: see the terminal");
    }
}

static void genes(App *a)
{
    Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    if (!o) {
        shell_print(SHELL_ERROR, "nothing selected: click an organism, or 'select ID'");
        return;
    }
    const GeneticCode *gc = o->gc;
    shell_print(SHELL_HEAD, "#%d: %d genes, symmetry %d, %s, children %s", o->id, gc->ngenes,
                gc->symmetry, gc->mirror ? "mirrored" : "not mirrored",
                gc->disperse_children ? "dispersed" : "kept close");
    for (int i = 0; i < gc->ngenes; i++) {
        const Gene *g = &gc->genes[i];
        shell_print(SHELL_OUT, "  %2d  %-7s length %5.2f  turn %6.1f deg  bend %6.1f deg", i + 1,
                    bio_color_name(g->color), g->length, g->theta * 180 / D_PI,
                    g->pitch * 180 / D_PI);
    }
}

static void run(App *a, const char *entered)
{
    char line[256];
    snprintf(line, sizeof line, "%s", entered);
    char *argv[8];
    int argc = shell_split(line, argv, 8);
    if (!argc)
        return;
    const char *c = argv[0];
    if (!strcasecmp(c, "help")) {
        help();
    } else if (!strcasecmp(c, "seed") && argc > 1) {
        a->wo.seed = strtoll(argv[1], NULL, 10);
        new_world(a);
    } else if (!strcasecmp(c, "seed")) {
        shell_print(SHELL_OUT, "seed %lld", (long long)a->wo.seed);
    } else if (!strcasecmp(c, "next")) {
        a->wo.seed++;
        new_world(a);
    } else if (!strcasecmp(c, "restart")) {
        new_world(a);
    } else if (!strcasecmp(c, "size") && argc > 3) {
        P.world_width = atoi(argv[1]);
        P.world_height = atoi(argv[2]);
        P.world_depth = atoi(argv[3]);
        new_world(a);
    } else if (!strcasecmp(c, "pitch") && argc > 1) {
        P.pitch_prob = atoi(argv[1]);
        new_world(a);
    } else if (!strcasecmp(c, "organisms") && argc > 1) {
        P.initial_organisms = atoi(argv[1]);
        new_world(a);
    } else if (!strcasecmp(c, "co2") && argc > 1) {
        P.initial_co2 = strtod(argv[1], NULL);
        new_world(a);
    } else if (!strcasecmp(c, "import") && argc > 1) {
        static char path[512];
        snprintf(path, sizeof path, "%s", argv[1]);
        a->wo.import = path;
        a->wo.copies = argc > 2 ? atoi(argv[2]) : 10;
        new_world(a);
    } else if (!strcasecmp(c, "speed") && argc > 1) {
        if (!strcasecmp(argv[1], "max")) {
            a->speed = k_nspeeds - 1;
        } else {
            int want = atoi(argv[1]), best = 0;
            for (int i = 0; i < k_nspeeds - 1; i++)
                if (abs(k_speeds[i] - want) < abs(k_speeds[best] - want))
                    best = i;
            a->speed = best;
        }
        shell_print(SHELL_OUT, "speed %s", app_speed_text(a));
    } else if (!strcasecmp(c, "pause")) {
        a->paused = true;
    } else if (!strcasecmp(c, "go")) {
        a->paused = false;
    } else if (!strcasecmp(c, "step")) {
        int k = argc > 1 ? atoi(argv[1]) : 1;
        a->paused = true;
        for (int i = 0; i < k; i++)
            world_time(&a->w);
        a->alpha = 1;
        shell_print(SHELL_OUT, "frame %lld", (long long)a->w.frame);
    } else if (!strcasecmp(c, "theme") && argc > 1) {
        if (shell_theme_set(argv[1]))
            interface_note(a, false, "Theme %s", shell_theme_name());
        else
            shell_print(SHELL_ERROR, "no theme %s ('themes' lists them)", argv[1]);
    } else if (!strcasecmp(c, "theme")) {
        app_do(a, ACT_THEME_NEXT);
    } else if (!strcasecmp(c, "themes")) {
        shell_theme_list();
    } else if (!strcasecmp(c, "follow")) {
        app_do(a, ACT_FOLLOW);
    } else if (!strcasecmp(c, "home")) {
        app_do(a, ACT_HOME);
    } else if (!strcasecmp(c, "select") && argc > 1) {
        int id = atoi(argv[1]);
        if (app_find(&a->w, id)) {
            a->selected = id;
            a->selected_was_alive = app_find(&a->w, id)->alive;
            a->follow = FOLLOW_SELECTED;
        } else {
            shell_print(SHELL_ERROR, "no organism #%d", id);
        }
    } else if (!strcasecmp(c, "kill")) {
        app_do(a, ACT_KILL);
    } else if (!strcasecmp(c, "genes")) {
        genes(a);
    } else if (!strcasecmp(c, "hints")) {
        app_do(a, ACT_HINTS);
    } else if (!strcasecmp(c, "clear")) {
        for (int i = 0; i < 40; i++)
            shell_print(SHELL_OUT, "%s", "");
    } else if (!strcasecmp(c, "guide")) {
        shell_print(SHELL_HEAD, "THE COLOURS (the same rules as 0.9)");
        for (int k = 0; k < COL_COUNT; k++)
            shell_print(SHELL_OUT, "  %-7s %s", syn_col_name(k), syn_guide_short(k));
    } else if (!strcasecmp(c, "colour") || !strcasecmp(c, "color")) {
        int k = argc > 1 ? syn_guide_find(argv[1]) : -1;
        if (k < 0) {
            shell_print(SHELL_ERROR, "colour NAME: one of green, red, cyan, blue, white, gray, "
                                     "yellow, brown");
        } else {
            shell_print(SHELL_HEAD, "%s", syn_col_name(k));
            shell_print(SHELL_OUT, "  %s", syn_guide_short(k));
            shell_print(SHELL_OUT, "  %s", syn_guide_long(k));
        }
    } else if (!strcasecmp(c, "kind")) {
        Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
        if (!o) {
            shell_print(SHELL_ERROR, "nothing selected: click an organism, or 'select ID'");
        } else {
            char kind[96], about[300];
            describe(o, kind, sizeof kind, about, sizeof about);
            shell_print(SHELL_HEAD, "#%d: %s", o->id, kind);
            shell_print(SHELL_OUT, "  %s", syn_kind_meaning(syn_kind(o->gc, NULL, 0)));
            shell_print(SHELL_OUT, "  %s", about);
        }
    } else if (!strcasecmp(c, "kinds")) {
        for (int k = 0; k < KIND_COUNT; k++)
            if (k != KIND_GRAZER && k != KIND_HUNTER && k != KIND_OMNIVORE && k != KIND_RAIDER &&
                k != KIND_SCAVENGER && k != KIND_PARASITE) /* Color Mod's eaters */
                shell_print(SHELL_OUT, "  %-11s %s", syn_kind_name((Kind)k),
                            syn_kind_meaning((Kind)k));
    } else if (!strcasecmp(c, "params")) {
        params_print();
    } else if (!strcasecmp(c, "set") && argc > 2) {
        char err[160];
        if (params_set_text(argv[1], argv[2], err, sizeof err))
            shell_print(SHELL_GOOD, "%s = %s", argv[1], argv[2]);
        else
            shell_print(SHELL_ERROR, "%s", err);
    } else if (!strcasecmp(c, "library")) {
        app_do(a, ACT_LIBRARY_TAB);
    } else if (!strcasecmp(c, "keep")) {
        app_do(a, ACT_KEEP);
    } else if (!strcasecmp(c, "quit")) {
        app_do(a, ACT_QUIT);
    } else {
        shell_print(SHELL_ERROR, "%s: no such command ('help' lists them)", c);
    }
}

/* ---- the frame ---- */

void interface_draw(App *a, int w, int h)
{
    const World *wd = &a->w;
    shell_begin(w, h);

    ShellTab tabs[] = {
        {"WORLD", ICON_GLOBE, ACT_WORLD_TAB, a->screen == SCREEN_WORLD, false},
        {"LIBRARY", ICON_DNA, ACT_LIBRARY_TAB, a->screen == SCREEN_LIBRARY, false},
        {"PARAMETERS", ICON_SLIDERS, ACT_PARAMS_TAB, a->screen == SCREEN_PARAMS, false},
    };
    ShellModule mods[9];
    int nm = 0;
    double carbon = wd->o2 + wd->co2;
    mods[nm] = (ShellModule){a->paused ? ICON_PAUSE : ICON_PLAY, "", "Pause or go on (space); "
                             "up and down change the speed", a->paused ? 1 : 0, ACT_PAUSE};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%s", a->paused ? "PAUSED" : app_speed_text(a));
    mods[nm] = (ShellModule){ICON_USERS, "", "The living", wd->population == 0 ? 2 : wd->population < 5 ? 1 : 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%d", wd->population);
    mods[nm] = (ShellModule){ICON_CLOCK, "", "Time: units of 256 world frames", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%lld", (long long)(wd->frame / 256));
    mods[nm] = (ShellModule){ICON_SKULL, "", "The dead, still decaying", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%d", world_n_corpses(wd));
    mods[nm] = (ShellModule){ICON_CLOUD, "", "Oxygen: the energy all bodies hold", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "O2 %.0f", wd->o2);
    mods[nm] = (ShellModule){ICON_SMOG, "", "Carbon dioxide: what green segments live on",
                             carbon > 0 && wd->co2 < carbon * 0.02 ? 1 : 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "CO2 %.0f", wd->co2);
    mods[nm] = (ShellModule){ICON_CROSSHAIRS, "", "Kills since genesis", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%lld", (long long)wd->counts.kills);
    mods[nm] = (ShellModule){ICON_GAUGE, "", "Pictures a second", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%.0f", a->fps);
    mods[nm] = (ShellModule){ICON_PALETTE, "", "The SYN-OS theme: click for the next (t)", 0,
                             ACT_THEME_NEXT};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%s", theme_short_name(skin_theme()));

    int got = shell_bar(w, "SYN-BIOGENESIS-3D", tabs, 3, mods, nm, ACT_MENU);
    if (got == ACT_MENU)
        main_menu(a, (float)w - 10, SHELL_BAR_H + 4);
    else if (got)
        app_do(a, got);

    const char *line = shell_console(w, SHELL_BAR_H, h - (a->hints ? SHELL_HINTS_H : 0), k_words,
                                     (int)(sizeof k_words / sizeof k_words[0]));
    if (line)
        run(a, line);

    if (!shell_console_showing()) {
        if (a->screen == SCREEN_LIBRARY)
            library_screen(a, w, h);
        else if (a->screen == SCREEN_PARAMS)
            params_screen(a, w, h);
        else
            card(a, w, a->hints ? h : h + SHELL_HINTS_H);
    }

    if (a->hints) {
        static const char *const keys[] = {
            "drag", "turn", "wheel", "zoom", "click", "select", "right-click", "menu",
            "Space", "pause", "Up/Down", "speed", "F", "follow", "N", "next seed",
            "T", "theme", "`", "console", "H", "hide these"};
        char right[96];
        snprintf(right, sizeof right, "seed %lld  %d x %d x %d", (long long)a->wo.seed, wd->width,
                 wd->height, wd->depth);
        shell_hints(w, h, keys, (int)(sizeof keys / sizeof keys[0]) / 2, right);
    }
    /* under a screen's title bar when one is up */
    shell_notifications(w, SHELL_BAR_H + (a->screen != SCREEN_WORLD && !shell_console_showing() ? 44 : 0));
    int picked = shell_menu(w, h);
    if (picked)
        app_do(a, picked);
    shell_end(w, h);
}
