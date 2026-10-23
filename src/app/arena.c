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

#include "app/arena.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app/app_internal.h"
#include "sim/params.h"
#include "ui/input.h"
#include "ui/shell.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#define WORKERS_MAX 16

static const int k_rounds[] = {6, 12, 24, 48};
static const int k_units[] = {100, 200, 400};

struct Arena {
    int pick[2];  /* library indices, -1 for none */
    float scroll[2];
    ArenaShape shape;
    int rounds_i, units_i, copies;
    /* the fight under way, or the last one */
    char path[2][1024], name[2][96];
    ArenaShape fought_shape;
    int fought_units, fought_copies;
    ArenaRound round[ARENA_MAX_ROUNDS];
    int nround;
    int next;  /* the next round a worker takes */
    int running; /* workers still going */
    bool stop;
    Uint32 started;
    SDL_mutex *lock;
    SDL_Thread *worker[WORKERS_MAX];
    int nworkers;
    char exe[1100];
};

Arena *arena_new(void)
{
    Arena *ar = calloc(1, sizeof *ar);
    if (!ar)
        abort();
    ar->pick[0] = ar->pick[1] = -1;
    ar->shape = SHAPE_DEFAULT;
    ar->rounds_i = 1;
    ar->units_i = 1;
    ar->copies = 10;
    ar->lock = SDL_CreateMutex();
    char *base = SDL_GetBasePath();
    snprintf(ar->exe, sizeof ar->exe, "%ssyn-biogenesis-3d", base ? base : "./");
    SDL_free(base);
    return ar;
}

void arena_free(Arena *ar)
{
    if (!ar)
        return;
    SDL_LockMutex(ar->lock);
    ar->stop = true;
    SDL_UnlockMutex(ar->lock);
    for (int i = 0; i < ar->nworkers; i++)
        SDL_DetachThread(ar->worker[i]); /* their rounds finish and exit on their own */
    /* the lock and the arena stay: a detached worker may still take it */
}

const char *arena_shape_name(ArenaShape s)
{
    static const char *const n[SHAPE_COUNT] = {"slab", "default", "cube"};
    return s >= 0 && s < SHAPE_COUNT ? n[s] : "";
}

/* The sweep's three shapes (docs/sweep): a slab one body thick with flat
 * genes, the default world, and a cube of its volume. */
static void shape_args(ArenaShape s, int *wd, int *ht, int *dp, int *pitch)
{
    switch (s) {
    case SHAPE_SLAB: *wd = 1000; *ht = 1000; *dp = 4; *pitch = 0; break;
    case SHAPE_CUBE: *wd = 318; *ht = 318; *dp = 317; *pitch = 50; break;
    default: *wd = 1000; *ht = 1000; *dp = 32; *pitch = 50; break;
    }
}

void arena_shape_params(ArenaShape s)
{
    params_set_defaults(&P);
    shape_args(s, &P.world_width, &P.world_height, &P.world_depth, &P.pitch_prob);
    P.initial_organisms = 0;
}

/* a path for sh, in single quotes */
static void quote(const char *s, char *out, size_t n)
{
    size_t k = 0;
    if (k + 1 < n)
        out[k++] = '\'';
    for (; *s && k + 5 < n; s++) {
        if (*s == '\'') {
            memcpy(out + k, "'\\''", 4);
            k += 4;
        } else {
            out[k++] = *s;
        }
    }
    if (k + 1 < n)
        out[k++] = '\'';
    out[k] = 0;
}

/* The round's last CSV row: the columns called marked and line2, and
 * population. */
static bool parse_result(FILE *f, ArenaRound *r)
{
    char line[1024], header[1024] = "", last[1024] = "";
    while (fgets(line, sizeof line, f)) {
        if (!header[0])
            snprintf(header, sizeof header, "%s", line);
        else
            snprintf(last, sizeof last, "%s", line);
    }
    if (!header[0] || !last[0])
        return false;
    int col_pop = -1, col_l1 = -1, col_l2 = -1, c = 0;
    for (char *p = header, *tok; (tok = strsep(&p, ",\n")); c++) {
        if (!strcmp(tok, "population"))
            col_pop = c;
        else if (!strcmp(tok, "marked"))
            col_l1 = c;
        else if (!strcmp(tok, "line2"))
            col_l2 = c;
    }
    if (col_pop < 0 || col_l1 < 0 || col_l2 < 0)
        return false;
    c = 0;
    for (char *p = last, *tok; (tok = strsep(&p, ",\n")); c++) {
        if (c == col_pop)
            r->alive = atoi(tok);
        else if (c == col_l1)
            r->line[0] = atoi(tok);
        else if (c == col_l2)
            r->line[1] = atoi(tok);
    }
    return true;
}

static int worker(void *ud)
{
    Arena *ar = ud;
    for (;;) {
        SDL_LockMutex(ar->lock);
        int i = ar->stop || ar->next >= ar->nround ? -1 : ar->next++;
        char cmd[4200];
        if (i >= 0) {
            int wd, ht, dp, pitch;
            shape_args(ar->fought_shape, &wd, &ht, &dp, &pitch);
            char exe[1200], a[1200], b[1200];
            quote(ar->exe, exe, sizeof exe);
            quote(ar->path[0], a, sizeof a);
            quote(ar->path[1], b, sizeof b);
            snprintf(cmd, sizeof cmd,
                     "%s --headless --seed %d --frames %d --size %d %d %d --pitch %d "
                     "--organisms 0 --copies %d --import %s --import %s 2>/dev/null",
                     exe, ar->round[i].seed, ar->fought_units * 256, wd, ht, dp, pitch,
                     ar->fought_copies, a, b);
        }
        SDL_UnlockMutex(ar->lock);
        if (i < 0)
            break;
        ArenaRound r = {0};
        FILE *f = popen(cmd, "r");
        bool ok = f && parse_result(f, &r);
        if (f)
            pclose(f);
        SDL_LockMutex(ar->lock);
        ar->round[i].line[0] = r.line[0];
        ar->round[i].line[1] = r.line[1];
        ar->round[i].alive = r.alive;
        ar->round[i].failed = !ok;
        ar->round[i].done = true;
        SDL_UnlockMutex(ar->lock);
    }
    SDL_LockMutex(ar->lock);
    ar->running--;
    SDL_UnlockMutex(ar->lock);
    return 0;
}

static void fight(App *a, Arena *ar)
{
    for (int k = 0; k < 2; k++) {
        const Being *b = &a->lib.beings[ar->pick[k]];
        snprintf(ar->path[k], sizeof ar->path[k], "%s", b->path);
        snprintf(ar->name[k], sizeof ar->name[k], "%s", b->name);
    }
    for (int i = 0; i < ar->nworkers; i++)
        SDL_WaitThread(ar->worker[i], NULL); /* the last fight's, all done */
    ar->nworkers = 0;
    ar->fought_shape = ar->shape;
    ar->fought_units = k_units[ar->units_i];
    ar->fought_copies = ar->copies;
    ar->nround = k_rounds[ar->rounds_i];
    for (int i = 0; i < ar->nround; i++)
        ar->round[i] = (ArenaRound){.seed = i + 1};
    ar->next = 0;
    ar->stop = false;
    ar->started = SDL_GetTicks();
    int cores = SDL_GetCPUCount() - 1;
    cores = cores < 1 ? 1 : cores > WORKERS_MAX ? WORKERS_MAX : cores;
    ar->running = cores;
    for (int i = 0; i < cores; i++)
        ar->worker[ar->nworkers++] = SDL_CreateThread(worker, "arena", ar);
}

/* the round, set up in the window: the very world it was scored in */
static void watch(App *a, Arena *ar, int i)
{
    arena_shape_params(ar->fought_shape);
    a->wo.seed = ar->round[i].seed;
    a->wo.nimports = 0;
    for (int k = 0; k < 2; k++)
        snprintf(a->wo.import[a->wo.nimports++], sizeof a->wo.import[0], "%s", ar->path[k]);
    a->wo.copies = ar->fought_copies;
    a->warm = 0;
    if (app_make_world(a)) {
        app_do(a, ACT_HOME);
        interface_note(a, false, "Round %d, from the start: %s and %s in the %s world "
                       "(0.9's default parameters)", ar->round[i].seed, ar->name[0], ar->name[1],
                       arena_shape_name(ar->fought_shape));
        a->screen = SCREEN_WORLD;
    }
}

static float mid(TTF_Font *f, const char *s, float x, float cy, Rgb c, Align al)
{
    return skin_text_rgb(f, s, x, cy - skin_text_height(f) * 0.5f, c.r, c.g, c.b, al);
}

/* A list of every being in the library to pick one from */
static void picker(App *a, Arena *ar, int k, UIRect r, const char *title, bool locked)
{
    const Fonts *fo = shell_fonts();
    const Theme *t = skin_theme();
    mid(fo->mono_small_bold, title, r.x, r.y + 8, skin_tone(TONE_ACCENT), ALIGN_LEFT);
    UIRect list = {r.x, r.y + 20, r.w, r.h - 20};
    skin_well(list);
    float row = 36;
    if (uiin_over(list))
        ar->scroll[k] -= uiin_wheel() * row;
    float most = fmaxf(0, (float)a->lib.nbeings * row - list.h + 8);
    ar->scroll[k] = fminf(fmaxf(ar->scroll[k], 0), most);
    for (int i = 0; i < a->lib.nbeings; i++) {
        float y = list.y + 4 + (float)i * row - ar->scroll[k];
        if (y < list.y || y + row > list.y + list.h)
            continue;
        const Being *b = &a->lib.beings[i];
        UIRect rr = {list.x + 4, y, list.w - 8, row - 4};
        bool hv = !locked && uiin_over(rr), on = ar->pick[k] == i;
        if (on)
            skin_fill(rr, t->accent, 0.85f);
        else if (hv)
            skin_fill(rr, t->panel_hover, 0.8f);
        Rgb fg = on ? skin_tone(TONE_ON_ACCENT) : t->text;
        Rgb mu = on ? skin_tone(TONE_ON_ACCENT) : skin_tone(TONE_MUTED);
        char fit[128];
        skin_text_rgb(fo->mono_small_bold, skin_fit(fo->mono_small_bold, b->name, rr.w * 0.55f, fit, sizeof fit),
                      rr.x + 8, rr.y + 3, fg.r, fg.g, fg.b, ALIGN_LEFT);
        skin_text_rgb(fo->small, skin_fit(fo->small, b->kind, rr.w - 16, fit, sizeof fit), rr.x + 8,
                      rr.y + 17, mu.r, mu.g, mu.b, ALIGN_LEFT);
        /* its folder, at the right */
        const char *folder = "";
        for (int f = 0; f < a->lib.nfolders; f++)
            if (i >= a->lib.folders[f].first && i < a->lib.folders[f].first + a->lib.folders[f].count)
                folder = a->lib.folders[f].name;
        skin_text_rgb(fo->mono_small, folder, rr.x + rr.w - 8, rr.y + 3, mu.r, mu.g, mu.b, ALIGN_RIGHT);
        if (hv && uiin_clicked(rr))
            ar->pick[k] = i;
    }
}

/* a row of choices; the one picked, changed by a click */
static void choices(UIRect r, const char *label, const char *const *names, int n, int *pick,
                    bool locked)
{
    const Fonts *fo = shell_fonts();
    mid(fo->small, label, r.x, r.y + r.h * 0.5f, skin_tone(TONE_MUTED), ALIGN_LEFT);
    float x = r.x + 110, bw = (r.w - 110 - (float)(n - 1) * 4) / (float)n;
    for (int i = 0; i < n; i++) {
        UIRect b = {x + (float)i * (bw + 4), r.y, bw, r.h};
        bool hv = !locked && uiin_over(b);
        Tone tone = skin_button_chrome(b, (WidgetState){.hover = hv, .active = *pick == i,
                                                        .disabled = locked && *pick != i}, true);
        mid(fo->small_bold, names[i], b.x + b.w * 0.5f, b.y + b.h * 0.5f - 1, skin_tone(tone),
            ALIGN_CENTER);
        if (hv && uiin_clicked(b))
            *pick = i;
    }
}

void arena_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    Arena *ar = a->arena;
    if (!a->lib_read) {
        library_scan(&a->lib);
        a->lib_read = true;
        ar->pick[0] = ar->pick[1] = -1;
    }
    SDL_LockMutex(ar->lock);
    int done = 0, wins[2] = {0, 0}, both = 0, none = 0, failed = 0;
    for (int i = 0; i < ar->nround; i++) {
        const ArenaRound *r = &ar->round[i];
        if (!r->done)
            continue;
        done++;
        if (r->failed)
            failed++;
        else if (r->line[0] && !r->line[1])
            wins[0]++;
        else if (r->line[1] && !r->line[0])
            wins[1]++;
        else if (r->line[0] && r->line[1])
            both++;
        else
            none++;
    }
    bool running = ar->running > 0;
    SDL_UnlockMutex(ar->lock);

    char sub[96];
    if (ar->nround)
        snprintf(sub, sizeof sub, "%d of %d rounds%s", done, ar->nround, running ? ", fighting" : "");
    else
        snprintf(sub, sizeof sub, "two beings, round after round");
    UIRect in = screen_panel(w, h, a->hints, "ARENA", sub);

    /* the two beings, and how they fight */
    float lw = fminf(360, in.w * 0.3f);
    /* two pickers, then three rows of choices and the button */
    float ph = (in.h - 3 * 34 - 34 - 36) / 2;
    picker(a, ar, 0, ui_rect(in.x, in.y, lw, ph), "FIRST", running);
    picker(a, ar, 1, ui_rect(in.x, in.y + ph + 8, lw, ph), "SECOND", running);
    float oy = in.y + 2 * ph + 22;
    static const char *const shapes[] = {"slab", "default", "cube"};
    int shape = ar->shape;
    choices(ui_rect(in.x, oy, lw, 28), "world", shapes, 3, &shape, running);
    ar->shape = (ArenaShape)shape;
    if (uiin_over(ui_rect(in.x, oy, lw, 28)))
        uiin_tooltip("slab 1000 x 1000 x 4, flat genes;  default 1000 x 1000 x 32;  cube 318 x 318 "
                     "x 317 (the default's volume)");
    static const char *const rounds[] = {"6", "12", "24", "48"};
    choices(ui_rect(in.x, oy + 34, lw, 28), "rounds", rounds, 4, &ar->rounds_i, running);
    static const char *const units[] = {"100", "200", "400"};
    choices(ui_rect(in.x, oy + 68, lw, 28), "time units", units, 3, &ar->units_i, running);
    bool ready = !running && ar->pick[0] >= 0 && ar->pick[1] >= 0 && ar->pick[0] != ar->pick[1];
    if (screen_button(ui_rect(in.x, in.y + in.h - 34, lw, 34), ICON_TROPHY,
                      running ? "Fighting..." : "Fight", !ready,
                      "Ten of each, no one else, a round per seed, on every core but this one's"))
        fight(a, ar);

    /* the rounds */
    float rx = in.x + lw + 16, rw = in.x + in.w - rx;
    if (!ar->nround) {
        skin_text(fo->body, "Choose two beings and fight.", rx, in.y + 8, TONE_MUTED, ALIGN_LEFT);
        skin_text(fo->small,
                  "Each round is a world of its own seed with ten of each and nothing else. "
                  "The one whose line is alive at the end wins it.",
                  rx, in.y + 34, TONE_MUTED, ALIGN_LEFT);
        return;
    }
    char head[400];
    snprintf(head, sizeof head, "%s %d    %s %d    both alive %d    neither %d%s", ar->name[0],
             wins[0], ar->name[1], wins[1], both, none, failed ? "    failed" : "");
    skin_text(fo->heading, head, rx, in.y, TONE_TEXT, ALIGN_LEFT);
    char sub2[128];
    snprintf(sub2, sizeof sub2, "%s world, %d time units, %d of each%s", arena_shape_name(ar->fought_shape),
             ar->fought_units, ar->fought_copies,
             running ? "" : "   (watch a round: the window makes that very world)");
    skin_text(fo->small, sub2, rx, in.y + 30, TONE_MUTED, ALIGN_LEFT);
    float row = 30, y0 = in.y + 56;
    int per_col = (int)((in.y + in.h - y0) / row);
    if (per_col < 1)
        per_col = 1;
    int ncols = (ar->nround + per_col - 1) / per_col;
    float cw = (rw - (float)(ncols - 1) * 10) / (float)ncols;
    Rgb good = skin_tone(TONE_GOOD), bad = skin_tone(TONE_BAD), accent = skin_tone(TONE_ACCENT);
    for (int i = 0; i < ar->nround; i++) {
        SDL_LockMutex(ar->lock);
        ArenaRound r = ar->round[i];
        SDL_UnlockMutex(ar->lock);
        float x = rx + (float)(i / per_col) * (cw + 10), y = y0 + (float)(i % per_col) * row;
        UIRect rr = {x, y, cw, row - 4};
        skin_well(rr);
        char s[32];
        snprintf(s, sizeof s, "seed %d", r.seed);
        mid(fo->mono_small, s, rr.x + 8, rr.y + rr.h * 0.5f, skin_tone(TONE_MUTED), ALIGN_LEFT);
        float bx = rr.x + 78, bw = rr.w - 78 - 70;
        if (!r.done) {
            mid(fo->mono_small, (Uint32)(SDL_GetTicks() / 400) % 2 ? "..." : "..", bx, rr.y + rr.h * 0.5f,
                skin_tone(TONE_MUTED), ALIGN_LEFT);
            continue;
        }
        if (r.failed) {
            mid(fo->mono_small, "failed", bx, rr.y + rr.h * 0.5f, bad, ALIGN_LEFT);
            continue;
        }
        /* the two lines' shares of what is left of them */
        int tot = r.line[0] + r.line[1];
        if (tot > 0) {
            float f = (float)r.line[0] / (float)tot;
            skin_fill(ui_rect(bx, rr.y + 7, bw * f, rr.h - 14), accent, 0.9f);
            skin_fill(ui_rect(bx + bw * f, rr.y + 7, bw * (1 - f), rr.h - 14), good, 0.9f);
        } else {
            mid(fo->mono_small, "both lines gone", bx, rr.y + rr.h * 0.5f, skin_tone(TONE_MUTED), ALIGN_LEFT);
        }
        char n[32];
        snprintf(n, sizeof n, "%d:%d", r.line[0], r.line[1]);
        if (tot > 0)
            mid(fo->mono_small_bold, n, bx + 6, rr.y + rr.h * 0.5f, skin_tone(TONE_ON_ACCENT), ALIGN_LEFT);
        if (!running && screen_button(ui_rect(rr.x + rr.w - 64, rr.y + 2, 60, rr.h - 4), ICON_EYE, "",
                                      false, "Watch this round from the start"))
            watch(a, ar, i);
    }
    /* the colours of the bars */
    float ky = in.y + 30;
    float kx = in.x + in.w;
    kx -= mid(fo->small, ar->name[1], kx, ky + 7, skin_tone(TONE_MUTED), ALIGN_RIGHT) + 6;
    skin_fill(ui_rect(kx - 10, ky + 2, 10, 10), good, 1);
    kx -= 24;
    kx -= mid(fo->small, ar->name[0], kx, ky + 7, skin_tone(TONE_MUTED), ALIGN_RIGHT) + 6;
    skin_fill(ui_rect(kx - 10, ky + 2, 10, 10), accent, 1);
}

static int find_being(const App *a, const char *what)
{
    for (int i = 0; i < a->lib.nbeings; i++)
        if (!strcasecmp(a->lib.beings[i].name, what) || !strcmp(a->lib.beings[i].path, what))
            return i;
    return -1;
}

bool arena_start(App *a, const char *first, const char *second)
{
    Arena *ar = a->arena;
    if (!a->lib_read) {
        library_scan(&a->lib);
        a->lib_read = true;
    }
    ar->pick[0] = find_being(a, first);
    ar->pick[1] = find_being(a, second);
    if (ar->pick[0] < 0 || ar->pick[1] < 0 || ar->pick[0] == ar->pick[1])
        return false;
    fight(a, ar);
    a->screen = SCREEN_ARENA;
    return true;
}
