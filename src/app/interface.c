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
#include "syn/winner.h"
#include "app/looks.h"
#include "app/minimap.h"
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
    look_load();
    shell_print(SHELL_HEAD, "SYN-BIOGENESIS-3D");
    shell_print(SHELL_NOTE, "Biogenesis 0.9's life in a box of water. 'help' lists the commands.");
    return true;
}

void interface_shutdown(void)
{
    shell_shutdown();
}

void interface_note(App *a, bool urgent, const char *fmt, ...)
{
    char text[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (a->interface)
        shell_notify(urgent, "%s", text);
}

/* ---- what an organism is ---- */

/* The height a line keeps to, in words. */
static const char *height_words(double h)
{
    return h >= 0.9 ? "the surface" : h >= 0.65 ? "the upper water" : h >= 0.35 ? "mid-water" : h >= 0.1 ? "the deep"
                                                                                                          : "the floor";
}

/* Where it keeps and how it gets there: its height, then its drives of a
 * third or more, strongest first. Without cilia its float takes it to its
 * height and the drives have nothing to push with. */
static void drives_sentence(const GeneticCode *gc, double depth, char *out, size_t n)
{
    static const char *const toward[DRIVE_COUNT] = {"out of the shade", "toward richer water",
                                                    "toward plants", "toward hunters",
                                                    "toward the dead", "toward company"};
    static const char *const away[DRIVE_COUNT] = {"into the shade", "away from rich water",
                                                  "away from plants", "away from hunters",
                                                  "away from the dead", "away from company"};
    bool cilia = false;
    for (int i = 0; i < gc->ngenes; i++)
        cilia = cilia || gc->genes[i].color == COL_CYAN;
    size_t at = (size_t)snprintf(out, n, "It keeps to %s, %.0f deep; ", height_words(gc->height),
                                 (1 - gc->height) * depth);
    if (at >= n)
        return;
    if (!cilia) {
        snprintf(out + at, n - at, "no cilia: its float takes it there, and it drifts across where it is pushed.");
        return;
    }
    int order[DRIVE_COUNT], k = 0;
    for (int d = 0; d < DRIVE_COUNT; d++)
        if (fabs(gc->drive[d]) >= 1.0 / 3)
            order[k++] = d;
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && fabs(gc->drive[order[j]]) > fabs(gc->drive[order[j - 1]]); j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    if (k == 0) {
        snprintf(out + at, n - at, "it swims there, and across at random, as 0.9's did.");
        return;
    }
    at += (size_t)snprintf(out + at, n - at, "it swims there, its cilia steering it");
    for (int i = 0; i < k && at < n; i++) {
        int d = order[i];
        at += (size_t)snprintf(out + at, n - at, "%s%s", i == 0 ? " " : i == k - 1 ? " and " : ", ",
                               gc->drive[d] > 0 ? toward[d] : away[d]);
    }
    if (at < n)
        snprintf(out + at, n - at, ".");
}

/* SYN-BIOGENESIS's kind (guide/kind.c, read from what the genes do) and
 * its sentence, whether the body is flat,
 * as 0.9 grew them, or bent into the third dimension, and where it
 * steers. */
static void describe(const Organism *o, char *kind, size_t kn, char *about, size_t an)
{
    syn_kind_label(o->gc, kind, kn);
    char sentence[256], drives[256];
    syn_kind_describe(o->gc, sentence, sizeof sentence);
    drives_sentence(o->gc, o->world->depth, drives, sizeof drives);
    char shape[96];
    const GeneticCode *gc = o->gc;
    if (gc->plan == PLAN_ORB)
        snprintf(shape, sizeof shape, "Its arms stand as an orb%s.", gc_is_flat(gc) ? "" : ", bent");
    else if (gc->plan == PLAN_CONE)
        snprintf(shape, sizeof shape, "Its arms stand as a cone, %.0f degrees %s%s.", fabs(gc->cone) * 180 / D_PI,
                 gc->cone < 0 ? "down" : "up", gc_is_flat(gc) ? "" : ", bent");
    else
        snprintf(shape, sizeof shape, "%s", gc_is_flat(gc) ? "Flat, as 0.9 grew them." : "Bent into the third dimension.");
    if (snprintf(about, an, "%s %s %s", sentence, shape, drives) >= (int)an)
        about[an - 1] = 0; /* cut short: the card has room for so much */
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
    snprintf(c.title, sizeof c.title, "ORGANISM %d", o->id);
    bool tracked = a->follow == FOLLOW_SELECTED;
    if (!o->alive) {
        c.state = "DEAD";
        c.state_tone = TONE_WARN;
    } else if (tracked) {
        c.state = "TRACKING";
        c.state_tone = TONE_ACCENT;
    } else {
        c.state = "ALIVE";
        c.state_tone = TONE_GOOD;
    }
    /* its species, as the family tree names it */
    char sp[64];
    stats_species(o, sp, sizeof sp);
    if (sp[0])
        snprintf(c.subtitle, sizeof c.subtitle, "SPECIES %s%s", sp, o->infected_gc ? ", infected" : "");
    else if (o->parent_id >= 0)
        snprintf(c.subtitle, sizeof c.subtitle, "generation %d, child of #%d%s", o->generation, o->parent_id,
                 o->infected_gc ? ", infected" : "");
    else
        snprintf(c.subtitle, sizeof c.subtitle, "a founder%s", o->infected_gc ? ", infected" : "");
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
    char v[32];
    CELL("GENERATION", TONE_TEXT, "%d", o->generation);
    CELL("AGE", TONE_TEXT, "%d", o->age >> 8);
    fmt_num(v, sizeof v, o->energy);
    CELL("ENERGY", TONE_TEXT, "%s", v);
    fmt_num(v, sizeof v, o->mass);
    CELL("MASS", TONE_TEXT, "%s", v);
    CELL("CHILDREN", TONE_TEXT, "%d", o->total_children);
    CELL("VICTIMS", o->total_kills ? TONE_BAD : TONE_TEXT, "%d", o->total_kills);
    CELL("INFECTED", o->total_infected ? TONE_WARN : TONE_TEXT, "%d", o->total_infected);
    CELL("BIRTH AT", TONE_TEXT, "%d", o->gc->reproduce_energy);
    CELL("DEPTH", TONE_TEXT, "%.0f", a->w.depth - o->pos.z);
    CELL("LIGHT", o->lit < 0.1 ? TONE_MUTED : TONE_TEXT, "%.0f%%", 100 * o->lit);
    CELL("SHAPE", gc_is_flat(o->gc) ? TONE_TEXT : TONE_ACCENT, "%s", gc_is_flat(o->gc) ? "flat" : "bent");
    CELL("SPEED", TONE_TEXT, "%.2f", v3_len(o->v));
#undef CELL
    for (int i = 0; i < n; i++) {
        c.cells[i].label = cells[i].label;
        snprintf(c.cells[i].value, sizeof c.cells[i].value, "%s", cells[i].value);
        c.cells[i].tone = cells[i].tone;
    }
    c.ncells = n;
    /* what it has towards a child, and how much of its life has gone */
    c.bar_label[0] = "TO A CHILD";
    c.bar[0] = o->gc->reproduce_energy > 0 ? o->energy / o->gc->reproduce_energy : 0;
    c.bar_tone[0] = TONE_GOOD;
    c.bar_label[1] = "LIFE";
    c.bar[1] = (double)(o->age >> 8) / max_age;
    c.bar_tone[1] = c.bar[1] > 0.8 ? TONE_BAD : TONE_WARN;
    describe(o, c.kind, sizeof c.kind, c.about, sizeof c.about);
    ShellCommand *t = c.actions;
    int k = 0;
    if (o->alive) {
        t[k++] = (ShellCommand){.id = ACT_FEED, .label = "Feed", .icon = ICON_BOLT, .tip = "Feed it  (F)"};
        t[k++] = (ShellCommand){.id = ACT_WEAKEN, .label = "Weaken", .icon = ICON_HEART_CRACK,
                                .tip = "Half its energy, breathed out"};
        t[k++] = (ShellCommand){.id = ACT_KILL, .label = "Kill", .icon = ICON_SKULL, .tip = "Kill it  (Del)"};
        t[k++] = (ShellCommand){.id = ACT_COPY, .label = "Copy", .icon = ICON_COPY,
                                .tip = "Copy its genetic code  (Ctrl+C)"};
        t[k++] = (ShellCommand){.id = ACT_FOLLOW, .label = tracked ? "Untrack" : "Track", .icon = ICON_CROSSHAIRS,
                                .on = tracked, .tip = "Keep the camera on it  (T)"};
        t[k++] = (ShellCommand){.id = ACT_GENES, .label = "Genes", .icon = ICON_DNA,
                                .tip = "Its genes in the laboratory  (G)"};
        t[k++] = (ShellCommand){.id = ACT_FAMILY, .label = "Family", .icon = ICON_SITEMAP,
                                .tip = "Its species in the family tree"};
        t[k++] = (ShellCommand){.id = ACT_KEEP, .label = "Keep", .icon = ICON_BOOKMARK,
                                .tip = "Keep its genetic code in the being library"};
        t[k++] = (ShellCommand){.id = ACT_WHERE, .label = "Where", .icon = ICON_DICE,
                                .tip = "Where it lives: its genesis ancestor, its seeds, where it reigns"};
    } else {
        t[k++] = (ShellCommand){.id = ACT_REVIVE, .label = "Revive", .icon = ICON_HEART_PULSE};
        t[k++] = (ShellCommand){.id = ACT_DISPERSE, .label = "Disperse", .icon = ICON_WIND};
        t[k++] = (ShellCommand){.id = ACT_FAMILY, .label = "Family", .icon = ICON_SITEMAP};
        t[k++] = (ShellCommand){.id = ACT_KEEP, .label = "Keep", .icon = ICON_BOOKMARK};
    }
    c.nactions = k;
    c.draw_body = draw_body;
    c.ud = a;
    int got = shell_card(w, h, &c);
    if (got)
        app_do(a, got);
}

/* ---- the commands ---- */

#define CMD(...) c[n++] = (ShellCommand){__VA_ARGS__}

/* Everything the window can do, as SYN-BIOGENESIS lists it: under the
 * headings of SYN-OS's root menu, each with its key and its console word. */
static int commands(App *a, ShellCommand *c)
{
    Organism *b = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    bool alive = b && b->alive, dead = b && !b->alive;
    int n = 0;
    const char *s = "WORLD";
    CMD(.id = ACT_NEW, .section = s, .label = "New world", .word = "new", .icon = ICON_SEEDLING,
        .tip = "A new world from a new seed, made from the parameters");
    CMD(.id = ACT_PAUSE, .section = s, .label = a->paused ? "Resume" : "Pause", .key = "P", .word = "pause",
        .icon = a->paused ? ICON_PLAY : ICON_PAUSE);
    CMD(.id = ACT_STEP, .section = s, .label = "One frame on", .key = ".", .word = "step", .icon = ICON_STEP,
        .disabled = !a->paused, .tip = "While paused: the world one frame on");
    CMD(.id = ACT_NEXT_SEED, .section = s, .label = "Next seed", .key = "N", .word = "next", .icon = ICON_DICE,
        .tip = "The world from the seed after this one");
    CMD(.id = ACT_RESTART, .section = s, .label = "Restart", .key = "R", .word = "restart", .icon = ICON_ROTATE,
        .tip = "This seed's world again, from genesis");
    CMD(.id = ACT_OPEN, .section = s, .label = "Open a world...", .key = "Ctrl+O", .word = "open",
        .icon = ICON_OPEN);
    CMD(.id = ACT_SAVE, .section = s, .label = "Save", .key = "S", .word = "save", .icon = ICON_SAVE);
    CMD(.id = ACT_SAVE_AS, .section = s, .label = "Save as...", .key = "Ctrl+Shift+S", .word = "saveas",
        .icon = ICON_SAVE);
    s = "THE WATER";
    CMD(.id = ACT_INC_CO2, .section = s, .label = "Increase CO2", .word = "co2+", .icon = ICON_SMOG,
        .tip = "500 more carbon dioxide in the water, the same in every part of it");
    CMD(.id = ACT_DEC_CO2, .section = s, .label = "Decrease CO2", .word = "co2-", .icon = ICON_LEAF,
        .tip = "500 less carbon dioxide, no more than there is");
    s = "EVERYONE";
    CMD(.id = ACT_KILL_ALL, .section = s, .label = "Kill all", .word = "killall", .icon = ICON_SKULL);
    CMD(.id = ACT_DISPERSE_ALL, .section = s, .label = "Disperse all corpses", .word = "disperseall",
        .icon = ICON_WIND);
    s = "NEW LIFE";
    CMD(.id = ACT_CREATE_RANDOM, .section = s, .label = "Create a random organism", .word = "random",
        .icon = ICON_WAND, .tip = "Where you right-clicked, else the middle of the view");
    CMD(.id = ACT_CREATE_WINNER, .section = s, .label = "Create a winner", .word = "winner", .icon = ICON_TROPHY,
        .disabled = syn_winner_running(),
        .tip = "A being worked out for the world as it is, whose bloodline will be all that lives");
    CMD(.id = ACT_PASTE, .section = s, .label = "Paste the copied organism", .key = "Ctrl+V", .word = "paste",
        .icon = ICON_PASTE, .disabled = !a->clipboard);
    CMD(.id = ACT_IMPORT, .section = s, .label = "Import a .bgg...", .word = "import", .icon = ICON_IMPORT);
    s = "THE ORGANISM SELECTED";
    CMD(.id = ACT_FOLLOW, .section = s, .label = a->follow == FOLLOW_SELECTED ? "Stop tracking" : "Track",
        .key = "T", .word = "track", .icon = ICON_CROSSHAIRS, .disabled = !b);
    CMD(.id = ACT_FEED, .section = s, .label = "Feed", .key = "F", .word = "feed", .icon = ICON_BOLT,
        .disabled = !alive);
    CMD(.id = ACT_WEAKEN, .section = s, .label = "Weaken", .word = "weaken", .icon = ICON_HEART_CRACK,
        .disabled = !alive);
    CMD(.id = ACT_REPRODUCE, .section = s, .label = "Force reproduction", .word = "reproduce",
        .icon = ICON_SEEDLING, .disabled = !alive);
    CMD(.id = ACT_REJUVENATE, .section = s, .label = "Rejuvenate", .word = "rejuvenate", .icon = ICON_ROTATE,
        .disabled = !alive);
    CMD(.id = ACT_KILL, .section = s, .label = "Kill", .key = "Del", .word = "kill", .icon = ICON_SKULL,
        .disabled = !alive);
    CMD(.id = ACT_COPY, .section = s, .label = "Copy its genetic code", .key = "Ctrl+C", .word = "copy",
        .icon = ICON_COPY, .disabled = !b);
    CMD(.id = ACT_EXPORT, .section = s, .label = "Export its genes...", .word = "export", .icon = ICON_EXPORT,
        .disabled = !b);
    CMD(.id = ACT_KEEP, .section = s, .label = "Keep in the library", .word = "keep", .icon = ICON_BOOKMARK,
        .disabled = !b);
    CMD(.id = ACT_WHERE, .section = s, .label = "Where it lives: its ancestor and seeds", .word = "where",
        .icon = ICON_DICE, .disabled = !b);
    CMD(.id = ACT_SAVE_IMAGE, .section = s, .label = "Save its image...", .word = "image", .icon = ICON_IMAGE,
        .disabled = !alive);
    CMD(.id = ACT_GENES, .section = s, .label = "Its genes in the laboratory", .key = "G", .word = "genes",
        .icon = ICON_DNA, .disabled = !b);
    CMD(.id = ACT_FAMILY, .section = s, .label = "Its family", .word = "family", .icon = ICON_SITEMAP,
        .disabled = !b);
    CMD(.id = ACT_REVIVE, .section = s, .label = "Revive", .word = "revive", .icon = ICON_HEART_PULSE,
        .disabled = !dead);
    CMD(.id = ACT_DISPERSE, .section = s, .label = "Disperse", .word = "disperse", .icon = ICON_WIND,
        .disabled = !dead);
    CMD(.id = ACT_DESELECT, .section = s, .label = "Let go of it", .key = "Esc", .word = "release",
        .icon = ICON_XMARK, .disabled = !b);
    s = "VIEW";
    CMD(.id = ACT_ZOOM_IN, .section = s, .label = "Zoom in", .key = "+", .word = "in", .icon = ICON_ZOOM_IN);
    CMD(.id = ACT_ZOOM_OUT, .section = s, .label = "Zoom out", .key = "-", .word = "out", .icon = ICON_ZOOM_OUT);
    CMD(.id = ACT_ZOOM_1, .section = s, .label = "Actual size", .key = "0", .word = "actual", .icon = ICON_SEARCH,
        .tip = "A world unit a pixel where the camera looks");
    CMD(.id = ACT_HOME, .section = s, .label = "Fit the world", .key = "Home", .word = "fit", .icon = ICON_EXPAND,
        .tip = "The whole box in view");
    CMD(.id = ACT_MINIMAP, .section = s, .label = "Minimap", .key = "M", .word = "minimap", .icon = ICON_MAP,
        .on = minimap_on());
    CMD(.id = ACT_UNTRACK, .section = s, .label = "Stop tracking", .word = "untrack", .icon = ICON_CROSSHAIRS,
        .disabled = a->follow != FOLLOW_SELECTED);
    for (int l = 0; l < LOOK_COUNT; l++)
        CMD(.id = ACT_LOOK + l, .section = s, .label = look_name((Look)l), .key = l == 0 ? "L" : NULL,
            .icon = ICON_PALETTE, .on = look_current() == (Look)l, .tip = l == 0 ? "L goes through the looks" : NULL);
    CMD(.id = ACT_FASTER, .section = s, .label = "Faster", .key = "Up", .word = "faster", .icon = ICON_PLUS);
    CMD(.id = ACT_SLOWER, .section = s, .label = "Slower", .key = "Down", .word = "slower", .icon = ICON_MINUS);
    CMD(.id = ACT_THEME_NEXT, .section = s, .label = "Next theme", .icon = ICON_PALETTE);
    CMD(.id = ACT_HINTS, .section = s, .label = a->hints ? "Hide the key hints" : "Show the key hints", .key = "H",
        .word = "hints", .icon = ICON_KEYBOARD);
    s = "SCREENS";
    CMD(.id = ACT_WORLD_TAB, .section = s, .label = "World", .word = "world", .icon = ICON_GLOBE,
        .on = a->screen == SCREEN_WORLD);
    CMD(.id = ACT_STATS_TAB, .section = s, .label = "Statistics", .word = "stats", .icon = ICON_CHART,
        .on = a->screen == SCREEN_STATS);
    CMD(.id = ACT_LAB_TAB, .section = s, .label = "Genetic laboratory", .word = "lab", .icon = ICON_FLASK,
        .on = a->screen == SCREEN_LAB);
    CMD(.id = ACT_LIBRARY_TAB, .section = s, .label = "Being library", .word = "library", .icon = ICON_BOOK,
        .on = a->screen == SCREEN_LIBRARY);
    CMD(.id = ACT_ARENA_TAB, .section = s, .label = "Arena", .word = "arena", .icon = ICON_TROPHY,
        .on = a->screen == SCREEN_ARENA);
    CMD(.id = ACT_TELESCOPE_TAB, .section = s, .label = "Dice telescope", .word = "telescope",
        .icon = ICON_BINOCULARS, .on = a->screen == SCREEN_TELESCOPE);
    CMD(.id = ACT_PARAMS_TAB, .section = s, .label = "Parameters", .word = "parameters", .icon = ICON_SLIDERS,
        .on = a->screen == SCREEN_PARAMS);
    CMD(.id = ACT_OPTIONS, .section = s, .label = "Options", .word = "options", .icon = ICON_GEAR,
        .on = a->screen == SCREEN_OPTIONS);
    CMD(.id = ACT_MAIN_MENU, .section = s, .label = "Main menu", .key = "Esc", .word = "menu", .icon = ICON_HOUSE);
    s = "SYSTEM";
    CMD(.id = ACT_CONSOLE, .section = s, .label = "Console", .key = "`", .icon = ICON_TERMINAL,
        .on = shell_console_showing());
    CMD(.id = ACT_FULLSCREEN, .section = s, .label = "Fullscreen", .key = "F11", .word = "fullscreen",
        .icon = ICON_EXPAND, .on = a->fullscreen);
    CMD(.id = ACT_MANUAL, .section = s, .label = "User manual", .word = "manual", .icon = ICON_INFO,
        .tip = "Biogenesis's own manual, in the browser");
    CMD(.id = ACT_ABOUT, .section = s, .label = "About Biogenesis", .word = "about", .icon = ICON_INFO);
    CMD(.id = ACT_QUIT, .section = s, .label = "Quit", .key = "Ctrl+Q", .word = "quit", .icon = ICON_POWER);
    return n;
}

/* what a right click on an organism, a corpse or the water offers */
static int menu_items(App *a, Organism *b, ShellCommand *c)
{
    int n = 0;
    if (b && b->alive) {
        CMD(.id = ACT_FOLLOW, .label = a->follow == FOLLOW_SELECTED ? "Stop tracking" : "Track", .key = "T",
            .icon = ICON_CROSSHAIRS);
        CMD(.id = ACT_FEED, .label = "Feed", .key = "F", .icon = ICON_BOLT);
        CMD(.id = ACT_WEAKEN, .label = "Weaken", .icon = ICON_HEART_CRACK);
        CMD(.id = ACT_REPRODUCE, .label = "Force reproduction", .icon = ICON_SEEDLING);
        CMD(.id = ACT_REJUVENATE, .label = "Rejuvenate", .icon = ICON_ROTATE);
        CMD(.id = ACT_KILL, .label = "Kill", .key = "Del", .icon = ICON_SKULL);
        CMD(.id = 0, .label = "-");
        CMD(.id = ACT_COPY, .label = "Copy", .key = "Ctrl+C", .icon = ICON_COPY);
        CMD(.id = ACT_EXPORT, .label = "Export...", .icon = ICON_EXPORT);
        CMD(.id = ACT_KEEP, .label = "Keep in the library", .icon = ICON_BOOKMARK);
        CMD(.id = ACT_WHERE, .label = "Where it lives", .icon = ICON_DICE);
        CMD(.id = ACT_SAVE_IMAGE, .label = "Save image...", .icon = ICON_IMAGE);
        CMD(.id = 0, .label = "-");
        CMD(.id = ACT_GENES, .label = "Its genes", .key = "G", .icon = ICON_DNA);
        CMD(.id = ACT_FAMILY, .label = "Its family", .icon = ICON_SITEMAP);
    } else if (b) {
        CMD(.id = ACT_REVIVE, .label = "Revive", .icon = ICON_HEART_PULSE);
        CMD(.id = ACT_DISPERSE, .label = "Disperse", .icon = ICON_WIND);
        CMD(.id = 0, .label = "-");
        CMD(.id = ACT_FAMILY, .label = "Its family", .icon = ICON_SITEMAP);
        CMD(.id = ACT_KEEP, .label = "Keep in the library", .icon = ICON_BOOKMARK);
    } else {
        CMD(.id = ACT_PASTE, .label = "Paste", .key = "Ctrl+V", .icon = ICON_PASTE, .disabled = !a->clipboard);
        CMD(.id = ACT_CREATE_RANDOM, .label = "Create randomly", .icon = ICON_WAND);
        CMD(.id = ACT_CREATE_WINNER, .label = "Create a winner", .icon = ICON_TROPHY, .disabled = syn_winner_running());
        CMD(.id = ACT_IMPORT, .label = "Import...", .icon = ICON_IMPORT);
        CMD(.id = 0, .label = "-");
        CMD(.id = ACT_COMMANDS, .label = "Every command", .key = "/", .icon = ICON_BARS);
    }
    return n;
}
#undef CMD

void interface_context_menu(App *a, float x, float y, int organism)
{
    Organism *o = organism >= 0 ? app_find(&a->w, organism) : NULL;
    if (o) {
        a->selected = organism;
        a->selected_was_alive = o->alive;
    }
    ShellCommand items[24];
    int n = menu_items(a, o, items);
    char title[48];
    if (o)
        snprintf(title, sizeof title, "%s %d", o->alive ? "ORGANISM" : "CORPSE", organism);
    else
        snprintf(title, sizeof title, "THE WATER AT %.0f, %.0f, %.0f DEEP", a->place.x, a->place.y,
                 a->w.depth - a->place.z);
    shell_menu_open(x, y, title, items, n);
}

/* ---- the console ---- */

static const char *const k_words[] = {
    "help",  "seed",   "next",  "restart", "size",   "pitch",  "organisms", "co2",
    "import", "speed", "pause", "go",      "step",   "theme",  "themes",    "follow",
    "home",  "select", "kill",  "genes",   "hints",  "clear",  "quit",      "colour",
    "guide", "kind",   "kinds", "set",     "params", "library", "keep",      "save",
    "load",  "worlds", "goto", "commands", "info", "get", "find", "run",
};

static void print_line(const char *s)
{
    shell_print(SHELL_OUT, "%s", s);
}

static void help(void)
{
    static const char *const lines[] = {
        "WORLD",
        "  seed N             a new world from seed N        next / restart",
        "  size W H D         a new world of that box        pitch P (0-100)",
        "  organisms N        founders in a new world        co2 X: per million cubic units",
        "  import FILE [K]    a new world with K copies of a 0.9 being (.bgg)",
        "TIME",
        "  speed N|max        world frames a second          pause / go",
        "  step [N]           N frames, paused               goto T: run to time T, pause",
        "VIEW",
        "  follow / home      the camera                     theme [NAME] / themes",
        "  select ID / kill   an organism                    genes: the selected one's",
        "PARAMETERS AND BEINGS",
        "  params             every parameter and its value  set NAME VALUE",
        "  library            the beings' screen             keep: keep the selected one",
        "  save [NAME]        save the world (ctrl+s)        load NAME / worlds",
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
    char line[160];
    size_t at = (size_t)snprintf(line, sizeof line, "  drives:");
    for (int d = 0; d < DRIVE_COUNT && at < sizeof line; d++)
        at += (size_t)snprintf(line + at, sizeof line - at, " %s %+.2f", gc_drive_name(d),
                               gc->drive[d]);
    shell_print(SHELL_OUT, "%s", line);
}

static void run(App *a, const char *entered, const ShellCommand *cmds, int ncmds)
{
    char line[256];
    snprintf(line, sizeof line, "%s", entered);
    char *argv[8];
    int argc = shell_split(line, argv, 8);
    if (!argc)
        return;
    const char *c = argv[0];
    /* a command of the list, by its word (its own console words come first:
     * "save NAME", "seed N", "kill") */
    const ShellCommand *cm = shell_lookup(cmds, ncmds, c);
    bool own = !strcasecmp(c, "save") || !strcasecmp(c, "kill") || !strcasecmp(c, "next") ||
               !strcasecmp(c, "restart") || !strcasecmp(c, "library") || !strcasecmp(c, "keep") ||
               !strcasecmp(c, "quit") || !strcasecmp(c, "hints") || !strcasecmp(c, "step") ||
               !strcasecmp(c, "pause") || !strcasecmp(c, "import") || !strcasecmp(c, "genes");
    if (cm && !own) {
        if (cm->disabled)
            shell_print(SHELL_ERROR, "%s: not now (%s)", c, cm->label);
        else
            app_do(a, cm->id);
        return;
    }
    if (!strcasecmp(c, "help")) {
        help();
        shell_list(cmds, ncmds);
    } else if (!strcasecmp(c, "commands")) {
        shell_list(cmds, ncmds);
    } else if (!strcasecmp(c, "info")) {
        shell_print(SHELL_OUT, "time %lld (frame %lld), %d living, %d corpses, O2 %.1f, CO2 %.1f, CH4 %.1f, seed %lld",
                    (long long)world_time_units(&a->w), (long long)a->w.frame, a->w.population,
                    world_n_corpses(&a->w), a->w.o2, a->w.co2, a->w.ch4, (long long)a->wo.seed);
    } else if (!strcasecmp(c, "get") && argc > 1) {
        const ParamField *f = params_field(argv[1]);
        if (f)
            shell_print(SHELL_OUT, "%s = %.17g", argv[1], params_get(&P, f));
        else
            shell_print(SHELL_ERROR, "no parameter %s ('params' lists them)", argv[1]);
    } else if (!strcasecmp(c, "find") && argc > 1) {
        int id = atoi(argv[1]);
        if (app_find(&a->w, id)) {
            a->selected = id;
            a->selected_was_alive = app_find(&a->w, id)->alive;
            a->follow = FOLLOW_SELECTED;
        } else {
            shell_print(SHELL_ERROR, "no organism #%d", id);
        }
    } else if (!strcasecmp(c, "run") && argc > 1) {
        long long k = atoll(argv[1]);
        app_run_to(a, a->w.frame + (k > 0 ? k : 0));
    } else if (!strcasecmp(c, "co2") && argc > 2 && (!strcmp(argv[1], "+") || !strcmp(argv[1], "-"))) {
        double q = strtod(argv[2], NULL);
        if (argv[1][0] == '+')
            world_add_co2(&a->w, q);
        else
            world_take_co2(&a->w, q);
        shell_print(SHELL_GOOD, "CO2 %.1f", a->w.co2);
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
        P.co2_density = strtod(argv[1], NULL);
        new_world(a);
    } else if (!strcasecmp(c, "import") && argc > 1) {
        world_options_import_only(&a->wo, argv[1]);
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
    } else if (!strcasecmp(c, "goto") && argc > 1) {
        long long t = atoll(argv[1]);
        if (t * 256 <= a->w.frame)
            shell_print(SHELL_ERROR, "time %lld is behind: worlds only go forward (restart, then goto)",
                        t);
        else
            app_run_to(a, t * 256);
    } else if (!strcasecmp(c, "pause")) {
        a->paused = true;
    } else if (!strcasecmp(c, "go")) {
        a->paused = false;
    } else if (!strcasecmp(c, "step")) {
        int k = argc > 1 ? atoi(argv[1]) : 1;
        a->paused = true;
        for (int i = 0; i < k; i++)
            app_step(a);
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
        shell_print(SHELL_HEAD, "THE COLOURS");
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
    } else if (!strcasecmp(c, "save")) {
        char path[1300], err[300];
        if (app_save_world(a, argc > 1 ? argv[1] : NULL, path, sizeof path, err, sizeof err))
            shell_print(SHELL_GOOD, "saved: %s", path);
        else
            shell_print(SHELL_ERROR, "%s", err);
    } else if (!strcasecmp(c, "load") && argc > 1) {
        char err[300];
        if (app_load_world(a, argv[1], err, sizeof err)) {
            app_do(a, ACT_HOME);
            interface_note(a, false, "%s: frame %lld, %d alive", a->loaded, (long long)a->w.frame,
                           a->w.population);
        } else {
            shell_print(SHELL_ERROR, "%s", err);
        }
    } else if (!strcasecmp(c, "worlds")) {
        app_list_worlds(print_line);
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
    /* an open menu or the command list takes the pointer: what is under it waits */
    uiin_block(shell_menu_showing() || shell_launcher_showing());
    static ShellCommand cmds[128];
    int ncmds = commands(a, cmds);

    ShellTab tabs[] = {
        {"WORLD", ICON_GLOBE, ACT_WORLD_TAB, a->screen == SCREEN_WORLD, false},
        {"STATISTICS", ICON_CHART, ACT_STATS_TAB, a->screen == SCREEN_STATS, false},
        {"LABORATORY", ICON_FLASK, ACT_LAB_TAB, a->screen == SCREEN_LAB, false},
        {"LIBRARY", ICON_BOOK, ACT_LIBRARY_TAB, a->screen == SCREEN_LIBRARY, false},
        {"ARENA", ICON_TROPHY, ACT_ARENA_TAB, a->screen == SCREEN_ARENA, false},
        {"TELESCOPE", ICON_BINOCULARS, ACT_TELESCOPE_TAB, a->screen == SCREEN_TELESCOPE, false},
        {"PARAMETERS", ICON_SLIDERS, ACT_PARAMS_TAB, a->screen == SCREEN_PARAMS, false},
    };
    ShellModule mods[10];
    int nm = 0;
    double carbon = wd->o2 + wd->co2;
    mods[nm] = (ShellModule){a->paused ? ICON_PAUSE : ICON_PLAY, "", "Pause or go on (space); "
                             "up and down change the speed", a->paused ? 1 : 0, ACT_PAUSE};
    if (a->ff_target)
        snprintf(mods[nm++].text, sizeof mods[0].text, "TO %lld", (long long)(a->ff_target / 256));
    else
        snprintf(mods[nm++].text, sizeof mods[0].text, "%s", a->paused ? "PAUSED" : app_speed_text(a));
    mods[nm] = (ShellModule){ICON_USERS, "", "The living", wd->population == 0 ? 2 : wd->population < 5 ? 1 : 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%d", wd->population);
    /* the time, with the sun up or the moon, when the world has days */
    static char day_tip[96];
    snprintf(day_tip, sizeof day_tip, "Time: units of 256 world frames; the sun at %.0f%% of noon's",
             100 * wd->daylight);
    mods[nm] = (ShellModule){P.day_length > 0 ? (wd->daylight >= 0.25 ? ICON_SUN : ICON_MOON)
                                              : ICON_CLOCK,
                             "", P.day_length > 0 ? day_tip : "Time: units of 256 world frames",
                             0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%lld", (long long)(wd->frame / 256));
    mods[nm] = (ShellModule){ICON_SKULL, "", "The dead, still decaying", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%d", world_n_corpses(wd));
    mods[nm] = (ShellModule){ICON_CLOUD, "", "Oxygen: the energy all bodies hold", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "O2 %.0f", wd->o2);
    mods[nm] = (ShellModule){ICON_SMOG, "", "Carbon dioxide: what green segments live on",
                             carbon > 0 && wd->co2 < carbon * 0.02 ? 1 : 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "CO2 %.0f", wd->co2);
    mods[nm] = (ShellModule){ICON_FIRE, "", "Methane: what the dead rot into, and purple segments live on", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "CH4 %.0f", wd->ch4);
    mods[nm] = (ShellModule){ICON_CROSSHAIRS, "", "Kills since genesis", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%lld", (long long)wd->counts.kills);
    mods[nm] = (ShellModule){ICON_GAUGE, "", "Pictures a second", 0, 0};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%.0f", a->fps);
    mods[nm] = (ShellModule){ICON_PALETTE, "", "The SYN-OS theme: click for the next", 0, ACT_THEME_NEXT};
    snprintf(mods[nm++].text, sizeof mods[0].text, "%s", theme_short_name(skin_theme()));

    int got = shell_bar(w, "SYN-BIOGENESIS-3D", tabs, (int)(sizeof tabs / sizeof tabs[0]), mods, nm, ACT_MAIN_MENU);

    /* the console's words: its own and every command's */
    static const char *words[200];
    int nwords = 0;
    for (size_t i = 0; i < sizeof k_words / sizeof k_words[0] && nwords < 200; i++)
        words[nwords++] = k_words[i];
    for (int i = 0; i < ncmds && nwords < 200; i++)
        if (cmds[i].word)
            words[nwords++] = cmds[i].word;
    const char *line = shell_console(w, SHELL_BAR_H, h - (a->hints ? SHELL_HINTS_H : 0), words, nwords);
    if (line)
        run(a, line, cmds, ncmds);

    if (!shell_console_showing()) {
        if (a->screen == SCREEN_MENU)
            menu_screen(a, w, h);
        else if (a->screen == SCREEN_OPTIONS)
            options_screen(a, w, h);
        else if (a->screen == SCREEN_FILES)
            files_screen(a, w, h);
        else if (a->screen == SCREEN_LIBRARY)
            library_screen(a, w, h);
        else if (a->screen == SCREEN_PARAMS)
            params_screen(a, w, h);
        else if (a->screen == SCREEN_STATS)
            stats_screen(a, w, h);
        else if (a->screen == SCREEN_ARENA)
            arena_screen(a, w, h);
        else if (a->screen == SCREEN_LAB)
            lab_screen(a, w, h);
        else if (a->screen == SCREEN_TELESCOPE)
            telescope_screen(a, w, h);
        else {
            card(a, w, a->hints ? h : h + SHELL_HINTS_H);
            minimap_draw(a, w, h, (float)(h - (a->hints ? SHELL_HINTS_H : 0)));
        }
    }
    /* the bar's click acts once the screen has had the frame, so a number
     * being typed there is taken before the screen goes */
    if (got)
        app_do(a, got);

    if (a->hints) {
        static const char *const keys[] = {
            "drag", "turn", "wheel", "zoom", "click", "select", "right-click", "menu",
            "P", "pause", "Up/Down", "speed", "T", "track", "/", "commands",
            "`", "console", "H", "hide these"};
        char right[200];
        if (syn_winner_status())
            snprintf(right, sizeof right, "%s", syn_winner_status());
        else if (a->loaded[0])
            snprintf(right, sizeof right, "%s  %d x %d x %d", a->loaded, wd->width, wd->height,
                     wd->depth);
        else
            snprintf(right, sizeof right, "seed %lld  %d x %d x %d", (long long)a->wo.seed,
                     wd->width, wd->height, wd->depth);
        shell_hints(w, h, keys, (int)(sizeof keys / sizeof keys[0]) / 2, right);
    }
    /* under a screen's title bar when one is up */
    shell_notifications(w, SHELL_BAR_H + (a->screen != SCREEN_WORLD && !shell_console_showing() ? 44 : 0));
    uiin_block(false);
    int cmd = shell_launcher(w, h, cmds, ncmds);
    if (cmd)
        app_do(a, cmd);
    int picked = shell_menu(w, h);
    if (picked)
        app_do(a, picked);
    shell_end(w, h);
}
