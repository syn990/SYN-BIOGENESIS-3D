/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/library_screen.c, the SYN edition of
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

#include "app/library.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app/app_internal.h"
#include "guide/guide.h"
#include "guide/kind.h"
#include "guide/palette.h"
#include "io/bgg.h"
#include "sim/dmath.h"
#include "sim/params.h"
#include "syn/arena.h"
#include "syn/library.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

#define PAD 24.0f
#define GAP 14.0f
#define MAXF 256
#define MAXB 2000
#define CARD_W 150.0f
#define CARD_H 178.0f
#define ROW_H 30.0f
#define CONFIRM_MS 4000

#define SIM_TITLE "Biogenesis 0.9 in three dimensions"

/* what a second click within a few seconds confirms */
enum { ASK_NONE, ASK_DELETE, ASK_EMPTY };
/* the menus open (ACT_LIBRARY_PICK + k) */
enum { PICK_LAB, PICK_COPY, PICK_PLACE, PICK_ARENA, PICK_KEEP, PICK_MOVE = 100 };
/* the folder rows: this library's, SYN-BIOGENESIS's, the ladder, the guide */
enum { R_OURS, R_GAP, R_THEIRS, R_LADDER, R_GUIDE };

static struct {
    bool ready, scanned;
    char err[300];
    char folders[MAXF][LIB_PATH];
    int counts[MAXF];
    int nf;
    char tfolders[2][MAXF][LIB_PATH]; /* SYN-BIOGENESIS's, 0.9's and Color Mod's, read only */
    int tcounts[2][MAXF];
    int ntf[2];
    char folder[LIB_PATH]; /* the one shown */
    bool theirs;           /* in SYN-BIOGENESIS's library */
    int sim;               /* which: 0 its 0.9 beings, 1 its Color Mod beings */
    bool ladder;           /* the arena's ladder shown instead */
    bool guide;            /* the field guide shown instead (guide/guide.h, guide/kind.h) */
    float gscroll;
    LibEntry *e;
    GeneticCode **gc; /* the folder's codes, read with it */
    int ne;
    int sel; /* the picked being, -1 for none */
    float fscroll, bscroll;
    Uint32 counted_at;
    char name[LIB_NAME];  /* the picked being's name, as shown */
    char fname[LIB_NAME]; /* the folder's */
    char status[400];
    Uint32 status_at;
    int ask;
    Uint32 ask_at;
    /* a double click: the card and when it was clicked */
    int click_i;
    Uint32 click_at;
    ShellCommand move_items[32];
} L = {.sel = -1, .click_i = -1};

static int g_mx, g_my;

/* ---- the little things every SYN screen has ---- */

static bool over(UIRect r)
{
    return uiin_over(r);
}

static bool button(UIRect r, const char *label, bool active, bool enabled)
{
    bool h = enabled && over(r);
    skin_button(r, shell_fonts()->bold, label, (WidgetState){.hover = h, .active = active, .disabled = !enabled},
                true);
    return h && uiin_clicked(r);
}

/* A code's body in r, on black, as the laboratory draws it. */
static void body(UIRect r, const GeneticCode *gc, double seconds)
{
    skin_fill(r, (Rgb){0, 0, 0}, 1);
    if (!gc)
        return;
    gl2d_clip_push(r.x, r.y, r.w, r.h);
    screen_code(gc, r, seconds);
    gl2d_clip_pop();
}

/* Text wrapped to width w from (x, y), drawn or only measured: its height. */
static float para_h(TTF_Font *f, const char *text, float x, float y, float w, Tone t, bool draw)
{
    char line[400];
    size_t n = 0;
    float lh = skin_text_height(f) + 4, h = 0;
    const char *p = text;
    while (*p) {
        const char *q = p;
        while (*q == ' ')
            q++;
        while (*q && *q != ' ')
            q++;
        char cand[400];
        snprintf(cand, sizeof cand, "%.*s%.*s", (int)n, line, (int)(q - p), n ? p : p + strspn(p, " "));
        if (n && skin_text_width(f, cand) > w) {
            line[n] = '\0';
            if (draw)
                skin_text(f, line, x, y + h, t, ALIGN_LEFT);
            h += lh;
            n = 0;
            p += strspn(p, " ");
            continue;
        }
        n = strlen(cand) < sizeof line ? strlen(cand) : sizeof line - 1;
        memcpy(line, cand, n);
        p = q;
    }
    if (n) {
        line[n] = '\0';
        if (draw)
            skin_text(f, line, x, y + h, t, ALIGN_LEFT);
        h += lh;
    }
    return h;
}

static void para(TTF_Font *f, const char *text, float x, float y, float w, Tone t)
{
    para_h(f, text, x, y, w, t, true);
}

static void say(const char *fmt, const char *a, const char *b)
{
    /* cut short if it must be */
    if (snprintf(L.status, sizeof L.status, fmt, a ? a : "", b ? b : "") < 0)
        L.status[0] = '\0';
    L.status_at = SDL_GetTicks();
}

/* ---- codes kept between frames ---- */

static void forget(void)
{
    for (int i = 0; i < L.ne; i++)
        gc_unref(L.gc[i]);
    free(L.e);
    free(L.gc);
    L.e = NULL;
    L.gc = NULL;
    L.ne = 0;
}

/* the library the shown folder is in: NULL for this one */
static const char *root(void)
{
    return L.theirs ? syn_library_theirs_of(L.sim) : NULL;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int folder_index(const char *f)
{
    int n = L.theirs ? L.ntf[L.sim] : L.nf;
    for (int i = 0; i < n; i++)
        if (!strcmp(L.theirs ? L.tfolders[L.sim][i] : L.folders[i], f))
            return i;
    return -1;
}

/* The folders and their counts again, and the shown folder's beings; the
 * picked being stays picked if it is still there (or is named keep). */
static void rescan(const char *keep)
{
    if (!L.ready)
        L.ready = syn_library_ready(L.err, sizeof L.err);
    char picked[LIB_NAME] = "";
    if (keep)
        snprintf(picked, sizeof picked, "%s", keep);
    else if (!L.ladder && L.sel >= 0 && L.sel < L.ne)
        snprintf(picked, sizeof picked, "%s", L.e[L.sel].name);
    L.nf = syn_library_folders(L.folders, MAXF);
    for (int i = 0; i < L.nf; i++)
        L.counts[i] = syn_library_count(L.folders[i]);
    for (int sim = 0; sim < 2; sim++) {
        const char *t = syn_library_theirs_of(sim);
        L.ntf[sim] = is_dir(t) ? syn_library_folders_in(t, L.tfolders[sim], MAXF) : 0;
        for (int i = 0; i < L.ntf[sim]; i++)
            L.tcounts[sim][i] = syn_library_count_in(t, L.tfolders[sim][i]);
    }
    if (L.theirs && !L.ntf[L.sim])
        L.theirs = false;
    if (folder_index(L.folder) < 0)
        L.folder[0] = '\0';
    forget();
    L.sel = -1;
    if (!L.ladder) {
        L.e = malloc(MAXB * sizeof *L.e);
        L.ne = L.e ? syn_library_list_in(root(), L.folder, L.e, MAXB) : 0;
        L.gc = calloc((size_t)(L.ne ? L.ne : 1), sizeof *L.gc);
        if (!L.gc)
            abort();
        char err[200];
        for (int i = 0; i < L.ne; i++) {
            L.gc[i] = syn_library_read_in(root(), L.folder, L.e[i].name, err, sizeof err);
            if (picked[0] && !strcmp(L.e[i].name, picked))
                L.sel = i;
        }
    }
    snprintf(L.name, sizeof L.name, "%s", L.sel >= 0 ? L.e[L.sel].name : "");
    const char *slash = strrchr(L.folder, '/');
    snprintf(L.fname, sizeof L.fname, "%s", slash ? slash + 1 : L.folder);
    L.counted_at = SDL_GetTicks();
    L.scanned = true;
}

static void show(const char *folder, bool theirs, int sim, bool ladder)
{
    snprintf(L.folder, sizeof L.folder, "%s", folder);
    L.theirs = theirs;
    L.sim = sim;
    L.ladder = ladder;
    L.guide = false;
    L.bscroll = 0;
    L.ask = ASK_NONE;
    L.click_i = -1;
    rescan("");
}

/* ---- what can be done ---- */

/* the picked being: its code, its name, and (for the ladder) its entry */
static const GeneticCode *picked(const char **name, const ArenaEntry **entry)
{
    *name = "";
    *entry = NULL;
    if (L.ladder) {
        const ArenaEntry *e = L.sel >= 0 && L.sel < syn_arena_count() ? syn_arena_at(L.sel) : NULL;
        if (!e)
            return NULL;
        *name = e->name;
        *entry = e;
        return e->gc;
    }
    if (L.sel < 0 || L.sel >= L.ne)
        return NULL;
    *name = L.e[L.sel].name;
    return L.gc[L.sel];
}

/* in the world: at its middle, else the nearest room around it */
static void place(App *a, const GeneticCode *gc, const char *name)
{
    World *w = &a->w;
    V3 mid = v3(w->width / 2.0, w->height / 2.0, w->depth / 2.0);
    for (int ring = 0; ring < 12; ring++) {
        int steps = ring ? ring * 8 : 1;
        for (int k = 0; k < steps; k++) {
            double ang = 2 * D_PI * k / steps;
            V3 p = v3(mid.x + ring * 60 * cos(ang), mid.y + ring * 60 * sin(ang), mid.z);
            Organism *o = org_new(w);
            if (!org_place_at(o, (GeneticCode *)gc, p)) {
                org_free(o);
                continue;
            }
            world_add_organism(w, o, NULL);
            a->selected = o->id;
            a->selected_was_alive = true;
            char where[64];
            snprintf(where, sizeof where, "%.0f, %.0f, %.0f deep", p.x, p.y, p.z);
            say("%s is in the world at %s", name, where);
            syn_history_user(w, 'p', 0, "You placed a being from the library");
            return;
        }
    }
    say("No room for %s near the middle of the world%s", name, "");
}

static void enter_arena(const GeneticCode *gc, const char *name)
{
    char err[200];
    const ArenaEntry *e = syn_arena_enter(gc, name, err, sizeof err);
    if (e)
        say("%s is on the arena's ladder at level 1, for the arena's settings as they are now%s",
            e->name, "");
    else
        say("Not on the ladder: %s%s", err, "");
}

static void keep_from_ladder(App *a, const ArenaEntry *e)
{
    char origin[200], st[120];
    syn_arena_standing(e, st, sizeof st);
    snprintf(origin, sizeof origin, "the arena's ladder: level %d, %d wins; %s", e->level, e->wins, st);
    app_library_keep(a, e->gc, "Arena", e->name, origin);
}

/* one of SYN-BIOGENESIS's beings, copied into this library */
static void keep_theirs(App *a, const GeneticCode *gc, const char *name)
{
    char origin[300];
    snprintf(origin, sizeof origin, "SYN-BIOGENESIS's %s library: %.100s%s%.40s%s", L.sim ? "Color Mod" : "0.9",
             L.folder, L.folder[0] ? "/" : "", name, L.sim ? " (converted from Color Mod 2.01)" : "");
    app_library_keep(a, gc, "Imported", name, origin);
}

/* ---- the folders ---- */

static void folders_view(UIRect r)
{
    const Fonts *fo = shell_fonts();
    skin_text(fo->small_bold, "FOLDERS", r.x, r.y, TONE_MUTED, ALIGN_LEFT);
    float top = r.y + skin_text_height(fo->small_bold) + 8;
    /* the folder buttons at the bottom; the name field above them */
    float by = r.y + r.h - 34, fy = by - 8 - 30;
    UIRect list = {r.x, top, r.w, fy - 10 - top};
    /* this library's folders, a gap, SYN-BIOGENESIS's, a gap, the ladder, the field guide */
    typedef struct {
        int kind, i;
    } Row;
    Row rows[3 * MAXF + 6];
    int nrows = 0;
    for (int i = 0; i < L.nf; i++)
        rows[nrows++] = (Row){R_OURS, i};
    rows[nrows++] = (Row){R_GAP, 0};
    for (int sim = 0; sim < 2; sim++)
        if (L.ntf[sim]) {
            for (int i = 0; i < L.ntf[sim]; i++)
                rows[nrows++] = (Row){R_THEIRS, sim * MAXF + i};
            rows[nrows++] = (Row){R_GAP, 0};
        }
    rows[nrows++] = (Row){R_LADDER, 0};
    rows[nrows++] = (Row){R_GUIDE, 0};
    float maxs = fmaxf(0, (float)nrows * ROW_H - list.h);
    if (over(list))
        L.fscroll -= uiin_wheel() * ROW_H;
    L.fscroll = fminf(fmaxf(L.fscroll, 0), maxs);
    gl2d_clip_push(list.x, list.y, list.w, list.h);
    for (int k = 0; k < nrows; k++) {
        int kind = rows[k].kind, i = rows[k].i, sim = 0;
        if (kind == R_THEIRS) {
            sim = i / MAXF;
            i %= MAXF;
        }
        if (kind == R_GAP)
            continue;
        UIRect row = {list.x, list.y + (float)k * ROW_H - L.fscroll, list.w, ROW_H};
        if (row.y + row.h < list.y || row.y > list.y + list.h)
            continue;
        bool on = kind == R_GUIDE    ? L.guide
                  : kind == R_LADDER ? L.ladder && !L.guide
                  : kind == R_THEIRS ? L.theirs && L.sim == sim && !L.ladder && !L.guide &&
                                           !strcmp(L.tfolders[sim][i], L.folder)
                                     : !L.theirs && !L.ladder && !L.guide && !strcmp(L.folders[i], L.folder);
        if (on)
            skin_fill(row, skin_theme()->accent, 0.22f);
        else if (over(row) && over(list))
            skin_row_hover(row);
        char label[LIB_PATH], count[16], fit[LIB_PATH];
        int depth = 0;
        bool accent = kind == R_LADDER || kind == R_GUIDE;
        if (kind == R_GUIDE) {
            snprintf(label, sizeof label, "FIELD GUIDE");
            count[0] = '\0';
        } else if (kind == R_LADDER) {
            snprintf(label, sizeof label, "ARENA LADDER");
            snprintf(count, sizeof count, "%d", syn_arena_count());
        } else {
            const char *f = kind == R_THEIRS ? L.tfolders[sim][i] : L.folders[i], *slash = strrchr(f, '/');
            for (const char *c = f; *c; c++)
                depth += *c == '/';
            depth += *f ? 1 : 0;
            if (!*f && kind == R_THEIRS) {
                snprintf(label, sizeof label, sim ? "SYN-BIOGENESIS COLOR MOD" : "SYN-BIOGENESIS 0.9");
                accent = true;
            } else {
                snprintf(label, sizeof label, "%s", !*f ? "Library" : slash ? slash + 1 : f);
            }
            snprintf(count, sizeof count, "%d", kind == R_THEIRS ? L.tcounts[sim][i] : L.counts[i]);
        }
        float x = row.x + 8 + 16.0f * (float)depth;
        float cw = skin_text_width(fo->small, count);
        skin_fit(fo->body, label, row.x + row.w - 16 - cw - x, fit, sizeof fit);
        float ty = row.y + (ROW_H - skin_text_height(fo->body)) / 2;
        skin_text(fo->body, fit, x, ty, accent ? TONE_ACCENT : TONE_TEXT, ALIGN_LEFT);
        skin_text(fo->small, count, row.x + row.w - 8, ty + 2, TONE_MUTED, ALIGN_RIGHT);
        if (over(row) && over(list)) {
            if (kind == R_LADDER)
                uiin_tooltip("The arena's beings for its settings as they are now, best first: keep "
                             "any of them in the library");
            else if (kind == R_GUIDE)
                uiin_tooltip("What every colour does, and the kinds of being they make");
            else if (kind == R_THEIRS && !L.tfolders[sim][i][0])
                uiin_tooltip(sim ? "SYN-BIOGENESIS's library of Color Mod beings, each read as one of this "
                                   "program's (its colours given the ones here that do their jobs): keep any "
                                   "of them in this one"
                                 : "SYN-BIOGENESIS's library of 0.9 beings, read as they are: keep any of "
                                   "them in this one");
            if (uiin_clicked(row)) {
                if (kind == R_GUIDE) {
                    L.guide = true;
                    L.gscroll = 0;
                } else if (kind == R_LADDER) {
                    show("", false, 0, true);
                } else {
                    show(kind == R_THEIRS ? L.tfolders[sim][i] : L.folders[i], kind == R_THEIRS, sim, false);
                }
            }
        }
    }
    gl2d_clip_pop();

    /* the shown folder's name, to change; then NEW FOLDER, REMOVE */
    bool fixed = L.ladder || L.theirs || !L.folder[0] || !strcmp(L.folder, LIB_BIN);
    UIRect field = {r.x, fy, r.w, 30};
    if (fixed) {
        skin_input(field, false, false, false);
        skin_text(fo->small,
                  L.ladder    ? "the arena's, read only"
                  : L.theirs  ? "SYN-BIOGENESIS's, read only"
                  : !L.folder[0] ? "the library itself"
                              : "the Bin",
                  field.x + 8, field.y + 7, TONE_MUTED, ALIGN_LEFT);
    } else {
        char typed[LIB_NAME];
        if (shell_text_field(&L.fname, field, fo->body, L.fname, typed, sizeof typed) && typed[0] &&
            strcmp(typed, L.fname)) {
            char got[LIB_PATH], err[300];
            if (syn_library_rename_folder(L.folder, typed, got, sizeof got, err, sizeof err)) {
                snprintf(L.folder, sizeof L.folder, "%s", got);
                say("The folder is %s now%s", got, "");
            } else {
                say("Not renamed: %s%s", err, "");
            }
            rescan(NULL);
        }
        if (over(field))
            uiin_tooltip("The folder's name: click, type a new one and press Enter");
    }
    float bw = (r.w - 8) / 2;
    UIRect nb = {r.x, by, bw, 34}, rb = {r.x + bw + 8, by, bw, 34};
    if (button(nb, "NEW FOLDER", false, !L.ladder && !L.theirs)) {
        char got[LIB_PATH], err[300];
        if (syn_library_new_folder(L.folder, "New folder", got, sizeof got, err, sizeof err)) {
            show(got, false, 0, false);
            say("Made %s: name it in the field above%s", got, "");
        } else {
            say("No folder made: %s%s", err, "");
        }
    }
    if (over(nb))
        uiin_tooltip("A folder inside the one shown");
    bool bin = !L.ladder && !L.theirs && !strcmp(L.folder, LIB_BIN);
    if (bin) {
        bool asked = L.ask == ASK_EMPTY && SDL_GetTicks() - L.ask_at < CONFIRM_MS;
        if (button(rb, asked ? "SURE?" : "EMPTY BIN", asked, L.ne > 0)) {
            if (asked) {
                char n[16];
                snprintf(n, sizeof n, "%d", syn_library_empty_bin());
                L.ask = ASK_NONE;
                rescan(NULL);
                say("%s beings removed for good%s", n, "");
            } else {
                L.ask = ASK_EMPTY;
                L.ask_at = SDL_GetTicks();
            }
        }
        if (over(rb))
            uiin_tooltip("Remove the beings in the Bin for good (click twice)");
    } else {
        bool can = !fixed && L.ne == 0;
        if (button(rb, "REMOVE", false, can)) {
            char err[300], was[LIB_PATH];
            snprintf(was, sizeof was, "%s", L.folder);
            if (syn_library_remove_folder(L.folder, err, sizeof err)) {
                const char *slash = strrchr(was, '/');
                char up[LIB_PATH];
                snprintf(up, sizeof up, "%.*s", slash ? (int)(slash - was) : 0, was);
                show(up, false, 0, false);
                say("Removed the folder %s%s", was, "");
            } else {
                say("Not removed: %s%s", err, "");
            }
        }
        if (over(rb))
            uiin_tooltip(can ? "Remove this folder" : "Only an empty folder of this library can be removed");
    }
}

/* ---- the beings ---- */

static void card_menu(void)
{
    static const ShellCommand ours[] = {
        {.id = ACT_LIBRARY_PICK + PICK_LAB, .label = "Open in the laboratory", .icon = ICON_FLASK},
        {.id = ACT_LIBRARY_PICK + PICK_COPY, .label = "Copy genetic code", .icon = ICON_COPY},
        {.id = ACT_LIBRARY_PICK + PICK_PLACE, .label = "Place in the world", .icon = ICON_PLUS},
        {.id = ACT_LIBRARY_PICK + PICK_ARENA, .label = "Put on the arena's ladder", .icon = ICON_TROPHY},
        {.id = ACT_LIBRARY_PICK + PICK_KEEP, .label = "Keep in the library", .icon = ICON_BOOKMARK},
    };
    /* the ladder's are kept rather than entered; SYN-BIOGENESIS's can be both */
    ShellCommand items[5];
    int n = 0;
    for (int k = 0; k < 5; k++) {
        if (k == PICK_ARENA && L.ladder)
            continue;
        if (k == PICK_KEEP && !L.ladder && !L.theirs)
            continue;
        items[n++] = ours[k];
    }
    const char *nm;
    const ArenaEntry *entry;
    picked(&nm, &entry);
    shell_menu_open((float)g_mx, (float)g_my, nm, items, n);
}

static void cards_view(App *a, UIRect r)
{
    const Fonts *fo = shell_fonts();
    char title[LIB_PATH + 64];
    int n = L.ladder ? syn_arena_count() : L.ne;
    if (L.ladder)
        snprintf(title, sizeof title, "THE ARENA'S LADDER  ·  %d being%s for its settings now", n,
                 n == 1 ? "" : "s");
    else
        snprintf(title, sizeof title, "%s%s  ·  %d being%s",
                 !L.theirs ? "" : L.sim ? "SYN-BIOGENESIS COLOR MOD  ·  " : "SYN-BIOGENESIS 0.9  ·  ",
                 L.folder[0] ? L.folder : L.theirs ? "ITS LIBRARY" : "LIBRARY", n, n == 1 ? "" : "s");
    char fit[LIB_PATH + 64];
    skin_text(fo->small_bold, skin_fit(fo->small_bold, title, r.w, fit, sizeof fit), r.x, r.y,
              TONE_MUTED, ALIGN_LEFT);
    float top = r.y + skin_text_height(fo->small_bold) + 8;
    UIRect grid = {r.x, top, r.w, r.y + r.h - top};
    if (n == 0) {
        const char *hint = L.ladder ? "The arena has no beings for its settings yet: start its battles."
                           : L.theirs ? "No beings here."
                           : !strcmp(L.folder, LIB_BIN)
                               ? "The Bin is empty. Deleted beings come here first."
                               : "No beings here yet. Keep one from the world (its Keep button), the "
                                 "laboratory or the arena's ladder, or IMPORT a .bgg file.";
        para(fo->body, hint, grid.x, grid.y + 10, grid.w, TONE_MUTED);
        return;
    }
    int cols = (int)floorf((grid.w + GAP) / (CARD_W + GAP));
    if (cols < 1)
        cols = 1;
    int rows = (n + cols - 1) / cols;
    float maxs = fmaxf(0, (float)rows * (CARD_H + GAP) - GAP - grid.h);
    if (over(grid))
        L.bscroll -= uiin_wheel() * (CARD_H + GAP) / 2;
    L.bscroll = fminf(fmaxf(L.bscroll, 0), maxs);
    gl2d_clip_push(grid.x, grid.y, grid.w, grid.h);
    for (int i = 0; i < n; i++) {
        UIRect c = {grid.x + (float)(i % cols) * (CARD_W + GAP),
                    grid.y + (float)(i / cols) * (CARD_H + GAP) - L.bscroll, CARD_W, CARD_H};
        if (c.y + c.h < grid.y || c.y > grid.y + grid.h)
            continue;
        const GeneticCode *gc;
        const char *nm, *sub;
        char subbuf[200];
        if (L.ladder) {
            const ArenaEntry *e = syn_arena_at(i);
            gc = e->gc;
            nm = e->name;
            snprintf(subbuf, sizeof subbuf, "level %d  ·  %d wins", e->level, e->wins);
            sub = subbuf;
        } else {
            gc = L.gc[i];
            nm = L.e[i].name;
            sub = L.e[i].origin[0] ? L.e[i].origin : L.e[i].saved;
        }
        bool on = i == L.sel;
        skin_fill(c, on ? skin_theme()->accent : skin_theme()->bg_alt, on ? 0.30f : 0.6f);
        UIRect pic = {c.x + 6, c.y + 6, c.w - 12, c.w - 22};
        /* the one picked turns */
        body(pic, gc, on ? a->seconds : 0);
        if (!gc)
            skin_text(fo->small, "can't be read", pic.x + pic.w / 2, pic.y + pic.h / 2 - 8, TONE_BAD,
                      ALIGN_CENTER);
        char f1[LIB_NAME + 8], f2[200];
        float ty = pic.y + pic.h + 6;
        skin_text(fo->bold, skin_fit(fo->bold, nm, c.w - 12, f1, sizeof f1), c.x + 6, ty, TONE_TEXT,
                  ALIGN_LEFT);
        skin_text(fo->small, skin_fit(fo->small, sub, c.w - 12, f2, sizeof f2), c.x + 6,
                  ty + skin_text_height(fo->bold) + 2, TONE_MUTED, ALIGN_LEFT);
        if (over(c) && over(grid)) {
            if (!on)
                skin_fill(c, skin_theme()->accent, 0.10f);
            static char tip[400];
            char kind[128] = "";
            if (gc)
                syn_kind_label(gc, kind, sizeof kind);
            if (L.ladder) {
                snprintf(tip, sizeof tip, "%s, %s: %s. Double-click for its genes.", nm, kind, syn_arena_at(i)->plan);
            } else {
                snprintf(tip, sizeof tip, "%s%s%s%s%s%s%s. Double-click for its genes.", nm, kind[0] ? ", " : "",
                         kind, L.e[i].origin[0] ? ", from " : "", L.e[i].origin, L.e[i].saved[0] ? ", kept " : "",
                         L.e[i].saved);
            }
            uiin_tooltip(tip);
            bool left = uiin_pressed(c, 0), right = uiin_pressed(c, 1);
            if (left || right) {
                if (L.sel != i)
                    L.ask = ASK_NONE;
                L.sel = i;
                snprintf(L.name, sizeof L.name, "%s", nm);
            }
            if (left) {
                /* a second click on it soon after: its genes */
                if (L.click_i == i && SDL_GetTicks() - L.click_at < 450 && gc)
                    lab_open(a, gc);
                L.click_i = i;
                L.click_at = SDL_GetTicks();
            }
            if (uiin_right_clicked(c) && gc)
                card_menu();
        }
    }
    gl2d_clip_pop();
}

void library_pick(App *a, int k)
{
    const char *nm;
    const ArenaEntry *entry;
    const GeneticCode *gc = picked(&nm, &entry);
    if (k >= PICK_MOVE) {
        int to = k - PICK_MOVE;
        if (!gc || L.ladder || L.theirs || to < 0 || to >= L.nf)
            return;
        char err[300], dest[LIB_PATH];
        snprintf(dest, sizeof dest, "%s", L.folders[to]);
        if (syn_library_move(L.folder, nm, dest, err, sizeof err)) {
            say("Moved to %s%s", dest[0] ? dest : "the library", "");
            rescan("");
        } else {
            say("Not moved: %s%s", err, "");
        }
        return;
    }
    if (!gc)
        return;
    if (k == PICK_LAB) {
        lab_open(a, gc);
    } else if (k == PICK_COPY) {
        gc_assign(&a->clipboard, (GeneticCode *)gc);
        say("%s copied: paste it into the world from its menu%s", nm, "");
    } else if (k == PICK_PLACE) {
        place(a, gc, nm);
    } else if (k == PICK_ARENA && !entry) {
        enter_arena(gc, nm);
    } else if (k == PICK_KEEP && entry) {
        keep_from_ladder(a, entry);
    } else if (k == PICK_KEEP && L.theirs) {
        keep_theirs(a, gc, nm);
    }
}

/* ---- the field guide ---- */

static void guide_view(UIRect r)
{
    const Fonts *fo = shell_fonts();
    char title[200], fit[200];
    snprintf(title, sizeof title, "FIELD GUIDE  \xc2\xb7  what each colour of %s does, and the kinds of being",
             SIM_TITLE);
    skin_text(fo->small_bold, skin_fit(fo->small_bold, title, r.w, fit, sizeof fit), r.x, r.y, TONE_MUTED,
              ALIGN_LEFT);
    float top = r.y + skin_text_height(fo->small_bold) + 8;
    UIRect list = {r.x, top, r.w, r.y + r.h - top};
    /* the whole length, as it was last drawn */
    static float total;
    if (over(list))
        L.gscroll -= uiin_wheel() * 80;
    L.gscroll = fminf(fmaxf(L.gscroll, 0), fmaxf(0, total - list.h));
    gl2d_clip_push(list.x, list.y, list.w, list.h);
    float x = list.x, y = list.y - L.gscroll;
    float nx = x + 34, tx = nx + 150, tw = list.x + list.w - 12 - tx;
    float hh = skin_text_height(fo->small_bold) + 8, bh = skin_text_height(fo->body) + 4;
    int last = -1;
    for (int s = 0; s < SYN_NCOL; s++) {
        const char *line = syn_guide_short(s), *more = syn_guide_long(s);
        Role ro = syn_col_role(s);
        if (!line[0])
            continue;
        if ((int)ro != last) {
            y += last >= 0 ? 14 : 0;
            skin_text(fo->small_bold, syn_guide_role(ro), x, y, TONE_ACCENT, ALIGN_LEFT);
            if (ro == ROLE_INACTIVE)
                skin_text(fo->small, "what segments turn for a while", x + 140, y, TONE_MUTED, ALIGN_LEFT);
            y += hh;
            last = (int)ro;
        }
        float rh = bh + para_h(fo->small, more, tx, 0, tw, TONE_MUTED, false) + 10;
        if (y + rh >= list.y && y <= list.y + list.h) {
            uint8_t cr, cg, cb;
            syn_col_rgb(s, &cr, &cg, &cb);
            UIRect sw = {x + 2, y + 3, 24, 14};
            skin_fill((UIRect){sw.x - 1, sw.y - 1, sw.w + 2, sw.h + 2}, skin_theme()->border, 1);
            skin_fill(sw, (Rgb){cr, cg, cb}, 1);
            skin_text(fo->bold, syn_col_name(s), nx, y, syn_guide_is_gene(s) ? TONE_TEXT : TONE_MUTED,
                      ALIGN_LEFT);
            skin_text(fo->body, line, tx, y, TONE_TEXT, ALIGN_LEFT);
            para_h(fo->small, more, tx, y + bh, tw, TONE_MUTED, true);
        }
        y += rh;
    }
    /* the kinds the colours make (guide/kind.h) */
    y += 14;
    skin_text(fo->small_bold, "KINDS OF BEING", x, y, TONE_ACCENT, ALIGN_LEFT);
    skin_text(fo->small, "what a being is called by what most of it does", x + 140, y, TONE_MUTED, ALIGN_LEFT);
    y += hh;
    for (int k = 0; k < KIND_COUNT; k++) {
        if (k >= KIND_GRAZER && k <= KIND_PARASITE)
            continue;
        if (y + bh >= list.y && y <= list.y + list.h) {
            skin_text(fo->bold, syn_kind_name((Kind)k), nx, y, TONE_TEXT, ALIGN_LEFT);
            skin_text(fo->body, syn_kind_meaning((Kind)k), tx, y, TONE_TEXT, ALIGN_LEFT);
        }
        y += bh + 6;
    }
    total = y + L.gscroll - list.y + 12;
    gl2d_clip_pop();
}

/* the picked being's name and what can be done with it */
static void picked_view(App *a, UIRect r)
{
    const Fonts *fo = shell_fonts();
    const char *nm;
    const ArenaEntry *entry;
    const GeneticCode *gc = picked(&nm, &entry);
    skin_separator(r.x, r.y, r.w);
    float y = r.y + 12;
    if (!gc) {
        para(fo->body,
             L.theirs ? "Pick a being to keep it in this library, put it on the arena's ladder or place it in "
                        "the world."
                      : "Pick a being to rename it, move it, put it on the arena's ladder or place it in the world.",
             r.x, y + 8, r.w, TONE_MUTED);
        return;
    }
    UIRect field = {r.x, y, 300, 34};
    if (entry || L.theirs) {
        char fit0[LIB_NAME + 8];
        skin_text(fo->heading, skin_fit(fo->heading, nm, field.w, fit0, sizeof fit0), r.x, y + 2, TONE_TEXT,
                  ALIGN_LEFT);
    } else {
        char typed[LIB_NAME];
        if (shell_text_field(&L.name, field, fo->body, L.name, typed, sizeof typed) && typed[0] &&
            strcmp(typed, nm)) {
            char got[LIB_NAME], err[300], was[LIB_NAME];
            snprintf(was, sizeof was, "%s", nm);
            if (syn_library_rename(L.folder, was, typed, got, sizeof got, err, sizeof err)) {
                rescan(got);
                if (strcmp(was, got))
                    say("%s is %s now", was, got);
            } else {
                say("Not renamed: %s%s", err, "");
            }
            return;
        }
        if (over(field))
            uiin_tooltip("Its name: click, type a new one and press Enter");
    }
    /* where it came from */
    char about[400];
    if (entry) {
        char st[120];
        syn_arena_standing(entry, st, sizeof st);
        snprintf(about, sizeof about, "Level %d on the ladder, %d wins, %s. %s", entry->level, entry->wins,
                 st, entry->plan);
    } else {
        const LibEntry *e = &L.e[L.sel];
        snprintf(about, sizeof about, "%s%s%s%s", e->origin[0] ? "From " : "Put here by hand",
                 e->origin, e->saved[0] ? ", kept " : "", e->saved);
    }
    char fit[400];
    float ax = field.x + field.w + 16;
    skin_text(fo->small, skin_fit(fo->small, about, r.x + r.w - ax, fit, sizeof fit), ax, y + 10,
              TONE_MUTED, ALIGN_LEFT);

    /* what kind of being it is (guide/kind.h), along the bottom */
    {
        char label[128], text[600];
        syn_kind_label(gc, label, sizeof label);
        syn_kind_describe(gc, text, sizeof text);
        float lh = skin_text_height(fo->small) + 4, ky = r.y + r.h - 2 * lh - 2;
        float kx = r.x + skin_text(fo->small_bold, label, r.x, ky, TONE_ACCENT, ALIGN_LEFT) + 10;
        char fit2[600];
        skin_text(fo->small, skin_fit(fo->small, text, r.x + r.w - kx, fit2, sizeof fit2), kx, ky, TONE_MUTED,
                  ALIGN_LEFT);
        if (over((UIRect){r.x, ky, r.w, lh})) {
            static char tip[600];
            snprintf(tip, sizeof tip, "%s", text);
            uiin_tooltip(tip);
        }
    }

    /* the actions */
    y += 34 + 12;
    bool bin = !strcmp(L.folder, LIB_BIN) && !L.theirs;
    struct {
        const char *label, *tip;
        bool show;
    } act[] = {
        {"TO THE ARENA", "On the arena's ladder at level 1, to fight others of its level", !entry},
        {"KEEP IN THE LIBRARY",
         L.theirs ? "A copy in this library's Imported folder" : "A copy in the library's Arena folder",
         entry != NULL || L.theirs},
        {"INTO THE WORLD", "Into the world, at its middle or the nearest room", true},
        {"GENES", "Its genes in the genetic laboratory", true},
        {"COPY", "Its code to paste into the world from its menu", true},
        {"MOVE TO...", "Into another folder", !entry && !L.theirs},
        {bin ? "DELETE FOR GOOD" : "DELETE", bin ? "Remove it for good (click twice)" : "To the Bin",
         !entry && !L.theirs},
        {"NEW WORLD WITH IT", "A new world from this seed, with ten of it among the founders", true},
        {"WHERE IT LIVES", "In the telescope: the seeds and arena rounds its genes were drawn in, as scenes to open",
         true},
    };
    float x = r.x;
    for (size_t i = 0; i < sizeof act / sizeof *act; i++) {
        if (!act[i].show)
            continue;
        bool asked = i == 6 && L.ask == ASK_DELETE && SDL_GetTicks() - L.ask_at < CONFIRM_MS;
        const char *label = asked ? "SURE?" : act[i].label;
        /* as wide asking as not: the row stays put under a second click */
        float bw = fmaxf(skin_text_width(fo->bold, act[i].label), skin_text_width(fo->bold, "SURE?")) + 32;
        /* a second row when the first is full */
        if (x > r.x && x + bw > r.x + r.w) {
            x = r.x;
            y += 34 + 8;
        }
        UIRect b = {x, y, bw, 34};
        x += bw + 8;
        bool hit = button(b, label, asked, true);
        if (over(b))
            uiin_tooltip(act[i].tip);
        if (!hit)
            continue;
        switch (i) {
        case 0:
            enter_arena(gc, nm);
            break;
        case 1:
            if (entry)
                keep_from_ladder(a, entry);
            else
                keep_theirs(a, gc, nm);
            break;
        case 2:
            place(a, gc, nm);
            break;
        case 3:
            lab_open(a, gc);
            break;
        case 4:
            gc_assign(&a->clipboard, (GeneticCode *)gc);
            say("%s copied: paste it into the world from its menu%s", nm, "");
            break;
        case 5: {
            int k = 0;
            for (int f = 0; f < L.nf && k < 32; f++)
                L.move_items[k++] = (ShellCommand){.id = ACT_LIBRARY_PICK + PICK_MOVE + f,
                                                   .label = L.folders[f][0] ? L.folders[f] : "Library",
                                                   .icon = ICON_FOLDER};
            shell_menu_open(b.x, b.y + b.h, "MOVE TO", L.move_items, k);
            break;
        }
        case 6: {
            if (bin && !asked) {
                L.ask = ASK_DELETE;
                L.ask_at = SDL_GetTicks();
                break;
            }
            char err[300], was[LIB_NAME];
            snprintf(was, sizeof was, "%s", nm);
            L.ask = ASK_NONE;
            if (syn_library_delete(L.folder, was, err, sizeof err)) {
                say(bin ? "%s removed for good%s" : "%s is in the Bin%s", was, "");
                rescan("");
            } else {
                say("Not deleted: %s%s", err, "");
            }
            break;
        }
        case 7: {
            char path[LIB_PATH + 1300];
            if (entry || !syn_library_path_in(root(), L.folder, nm, path, sizeof path)) {
                /* the ladder's: written where a new world's founders are read from */
                char err[300];
                snprintf(path, sizeof path, "%s/arena-founder.bgg", params_config_dir());
                if (!bgg_save(path, gc, err, sizeof err)) {
                    say("Not made: %s%s", err, "");
                    break;
                }
            }
            world_options_import_only(&a->wo, path);
            a->wo.copies = 10;
            if (app_make_world(a)) {
                interface_note(a, false, "Seed %lld with ten of %s", (long long)a->wo.seed, nm);
                app_do(a, ACT_HOME);
                a->screen = SCREEN_WORLD;
            }
            break;
        }
        case 8:
            telescope_find(a, gc, nm);
            return;
        }
        break;
    }
}

/* ---- the screen ---- */

void library_refresh(void)
{
    L.scanned = false;
}

void library_show_guide(void)
{
    L.guide = true;
    L.gscroll = 0;
}

void library_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    float mx, my;
    uiin_mouse(&mx, &my);
    g_mx = (int)mx;
    g_my = (int)my;
    if (!L.scanned)
        rescan(NULL);
    /* files put in or taken out by hand show within a few seconds */
    if (SDL_GetTicks() - L.counted_at > 3000) {
        static char f[MAXF][LIB_PATH];
        int nf = syn_library_folders(f, MAXF);
        bool changed = nf != L.nf || (!L.ladder && syn_library_count_in(root(), L.folder) != L.ne);
        for (int i = 0; i < nf && !changed; i++)
            changed = strcmp(f[i], L.folders[i]) || syn_library_count(f[i]) != L.counts[i];
        L.counted_at = SDL_GetTicks();
        /* not while a name is being typed: it would be lost */
        if (changed && !shell_field_active(&L.name) && !shell_field_active(&L.fname))
            rescan(NULL);
    }
    if (L.ladder && L.sel >= syn_arena_count())
        L.sel = -1;

    skin_fill(ui_rect(0, 0, (float)w, (float)h), (Rgb){0, 0, 0}, 0.5f);
    /* below the shell's bar, above the hints */
    float top = SHELL_BAR_H + 10;
    float pw = fminf((float)w - 32, 2400), ph = (float)h - top - (a->hints ? SHELL_HINTS_H : 0) - 12;
    UIRect panel = {((float)w - pw) / 2, top, pw, ph};
    uiin_solid(panel);
    skin_panel(panel, true);

    float x = panel.x + PAD, y = panel.y + 14, right = panel.x + panel.w - PAD;
    x += skin_text(fo->big, "LIBRARY", x, y, TONE_TEXT, ALIGN_LEFT) + 14;
    x += skin_text(fo->body, SIM_TITLE, x, y + 9, TONE_MUTED, ALIGN_LEFT) + 18;
    UIRect imp = {right - 170, y, 170, 36};
    char rootfit[600];
    skin_text(fo->small, skin_fit(fo->small, syn_library_root(), imp.x - 16 - x, rootfit, sizeof rootfit), x,
              y + 12, TONE_MUTED, ALIGN_LEFT);
    if (button(imp, "IMPORT .BGG...", false, true))
        files_open(a, FILES_IMPORT_LIBRARY);
    if (over(imp))
        uiin_tooltip("Copy a genetic code file (.bgg) into the folder shown");
    y += 56;

    float bottom = panel.y + panel.h - PAD;
    float status_h = skin_text_height(fo->small) + 8;
    float pick_h = 12 + 34 + 12 + 34 + 8 + 34 + 8 + 2 * (skin_text_height(fo->small) + 4) + 4;
    UIRect left = {panel.x + PAD, y, 290, bottom - y};
    UIRect rightr = {left.x + left.w + 24, y, right - left.x - left.w - 24, bottom - y - pick_h - status_h};
    if (!L.ready) {
        skin_text(fo->body, L.err, rightr.x, rightr.y, TONE_BAD, ALIGN_LEFT);
        return;
    }
    folders_view(left);
    if (L.guide) {
        guide_view((UIRect){rightr.x, rightr.y, rightr.w, bottom - rightr.y});
        return;
    }
    cards_view(a, rightr);
    picked_view(a, (UIRect){rightr.x, rightr.y + rightr.h + 8, rightr.w, pick_h});
    if (L.status[0] && SDL_GetTicks() - L.status_at < 8000)
        skin_text(fo->small, L.status, rightr.x, bottom - status_h + 6, TONE_ACCENT, ALIGN_LEFT);
}

bool app_library_keep(App *a, const GeneticCode *gc, const char *folder, const char *name,
                      const char *origin)
{
    char err[300], got[LIB_NAME], msg[400];
    if (!gc)
        return false;
    if (!syn_library_ready(err, sizeof err) ||
        !syn_library_put(folder, name, gc, origin, got, sizeof got, err, sizeof err)) {
        snprintf(msg, sizeof msg, "Not kept: %s", err);
        interface_note(a, true, "%s", msg);
        say("%s%s", msg, "");
        return false;
    }
    snprintf(msg, sizeof msg, "Kept in the library: %s/%s", folder, got);
    interface_note(a, false, "%s", msg);
    say("%s%s", msg, "");
    if (L.scanned)
        rescan(NULL);
    return true;
}

void library_imported(App *a, const char *path)
{
    const char *to = L.ladder || L.theirs || !L.folder[0] ? "Imported" : L.folder;
    char err[300], got[LIB_NAME];
    if (!syn_library_ready(err, sizeof err) ||
        !syn_library_import(path, to, got, sizeof got, err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "Not imported: %s", err);
        interface_note(a, true, "%s", msg);
        say("%s%s", msg, "");
        return;
    }
    char dest[LIB_PATH];
    snprintf(dest, sizeof dest, "%s", to);
    snprintf(L.folder, sizeof L.folder, "%s", dest);
    L.ladder = L.theirs = L.guide = false;
    rescan(got);
    say("Imported as %s/%s", dest, got);
}

void library_screen_free(void)
{
    forget();
    memset(&L, 0, sizeof L);
    L.sel = -1;
    L.click_i = -1;
}
