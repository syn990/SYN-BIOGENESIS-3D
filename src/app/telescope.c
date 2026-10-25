/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/telescope_screen.c, the SYN edition
 * of its C port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

/* TELESCOPE, the dice telescope's screen (syn/telescope.h): the seed a
 * saved world was made from, found from its genes and proved by replaying
 * it, as `--telescope world` finds it; and where a being lives, as
 * `--telescope find` finds it: the seeds and arena rounds its genes were
 * drawn in, as scenes to open.
 *
 * The searches run in this program started again, never in the window's
 * own world: the screen shows what they say as they go. A save here keeps
 * the generator's state, so a world opened from one goes on as the world
 * it was; once its seed is found, it knows it too. */

#include "app/telescope.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app/app_internal.h"
#include "io/bgg.h"
#include "io/worldfile.h"
#include "sim/params.h"
#include "syn/arena.h"
#include "syn/arena_os.h"
#include "syn/crack.h"
#include "syn/family.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

#define PAD 24.0f
#define LOG 400
#define LINE 240
#define HITS 400

/* what FORGE A WINNER says of itself: the fit's numbers are docs/crack's */
#define FORGE_TEXT                                                                                                     \
    "A winner from the dice. Each seed's founders are made without running its world and weighed by their codes "   \
    "alone, as fitted on replayed worlds (docs/crack): the fittest founder's bloodline led its world at time 300 in " \
    "33% of the worlds the fit wasn't made on, against 3% by chance. The seeds whose likeliest bloodline is "    \
    "likeliest are run, and the being their leading bloodline leads as is the winner. Each forging goes on through "  \
    "the seeds where the last stopped."

enum { M_SEED, M_BEING, M_FORGE };

/* A scene the finder found: a frame of a world where the being, or one of
 * its genes, is. */
typedef struct {
    bool round;    /* an arena round of the record, else a seed's world */
    int set;       /* the seed's: which parameters (F.set) */
    int index;     /* the round's place in the record */
    int cycle;     /* the round's number */
    int64_t seed;
    int64_t frame;
    char kind[12];
    int n, d;
} Hit;

static struct {
    char path[1200];  /* the world searched, or to be */
    char shown[1200]; /* how it is named here */
    bool running, done, found;
    ArenaProc proc;
    char part[4096]; /* a line not yet ended */
    size_t npart;
    char log[LOG][LINE];
    int nlog, first; /* a ring of the last LOG lines */
    char head[LINE];  /* what the search says of the world first */
    int lengths, replays, turned;
    int64_t replay_at, replay_of; /* the replay going on: its frame, of how many */
    int64_t seed;
    uint64_t state;
    double started, took;
    char status[400];
    Uint32 status_at;
    float scroll;
    bool follow; /* the log kept at its end */
    int mode;    /* M_SEED, M_BEING */
} T = {.follow = true};

/* where a being lives: the search, its scenes, and the jump into one */
static struct {
    char name[80];
    char path[1200]; /* the being as a .bgg file, for the search */
    GeneticCode *gc;
    bool running, done;
    ArenaProc proc;
    char part[4096];
    size_t npart;
    char log[8][LINE];
    int nlog;
    struct {
        int w, h, d, n;
    } set[4];
    int nsets;
    Hit hit[HITS];
    int nhits;
    int cur_round, cur_cycle; /* the last "@round" told */
    int prog_k, prog_n;       /* the seed being run, of how many */
    int64_t frame_now, frame_of;
    int64_t cur_seed;
    /* what the engine read of it (syn/crack.h) */
    bool anc_told;
    uint64_t anc_start;
    int anc_proof, anc_kept, anc_changes;
    GeneticCode *anc; /* NULL once told: none, its seed has faded */
    struct {
        int gene;
        int64_t place;
        char kind; /* l: its length drawn again, w: a gene added whole, a: its turn */
    } draw[24];
    int ndraws;
    int basin[4]; /* the seeds that make the ancestor, per set */
    double started, took;
    float scroll;
    /* the jump: a scene being made, then opened */
    bool jumping;
    ArenaProc jproc;
    char jpart[256];
    size_t njpart;
    int jhit; /* its row among the scenes */
    Hit jh;   /* the scene being made */
    int64_t jframe_now;
    char jpath[1200];
    uint64_t jstate;
    bool jat;
} F;

/* a winner from the dice: seeds weighed by their founders alone, the
 * likeliest run, the being their leading bloodline leads as (syn/crack.h) */
#define FORGED 32
static struct {
    bool running, done;
    ArenaProc proc;
    char part[4096];
    size_t npart;
    int64_t scan, from, next_from;
    int top;
    int64_t scanned;
    int prog_k, prog_n;
    int64_t frame_now, frame_of;
    int w, h, d, n; /* the world forged in: the arena's */
    char dir[1200];
    struct {
        int64_t seed;
        int win, lead, t, copies, changes;
        bool lone;
        double chance, share;
        GeneticCode *gc;
        bool entered; /* put on the arena's ladder */
    } r[FORGED];
    int nr;
    char last[LINE];
    double started, took;
    float scroll;
} G = {.scan = 20000, .top = 8, .next_from = 1};

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

/* Text wrapped to width w from (x, y); returns the y after it. */
static float para(TTF_Font *f, const char *text, float x, float y, float w, Tone t)
{
    char line[400];
    size_t n = 0;
    float lh = skin_text_height(f) + 4;
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
            skin_text(f, line, x, y, t, ALIGN_LEFT);
            y += lh;
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
        skin_text(f, line, x, y, t, ALIGN_LEFT);
        y += lh;
    }
    return y;
}

static void say(const char *s)
{
    snprintf(T.status, sizeof T.status, "%s", s);
    T.status_at = SDL_GetTicks();
}

static const char *log_line(int i)
{
    return T.log[(T.first + i) % LOG];
}

/* as much of src as fits */
static void copy(char *dst, size_t n, const char *src)
{
    size_t k = strlen(src);
    if (k >= n)
        k = n - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
}

static void log_add(const char *s)
{
    if (T.nlog < LOG) {
        copy(T.log[(T.first + T.nlog) % LOG], LINE, s);
        T.nlog++;
    } else {
        copy(T.log[T.first], LINE, s);
        T.first = (T.first + 1) % LOG;
    }
}

/* the configuration folder, made if it isn't there yet (a first run):
 * the searches' files go in it */
static void config_dir_make(void)
{
    const char *dir = params_config_dir();
    char parent[1024];
    snprintf(parent, sizeof parent, "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        mkdir(parent, 0755);
    }
    mkdir(dir, 0755);
}

/* lines from a child, a whole line at a time; false when it has ended */
static bool child_lines(ArenaProc *p, char *part, size_t cap, size_t *np, void (*line)(const char *))
{
    char buf[4096];
    for (;;) {
        long m = arena_os_read(p, buf, sizeof buf);
        if (m == 0)
            return true;
        if (m < 0) {
            if (*np) {
                part[*np] = '\0';
                line(part);
                *np = 0;
            }
            return false;
        }
        for (long i = 0; i < m; i++) {
            if (buf[i] == '\n' || *np + 1 >= cap) {
                part[*np] = '\0';
                line(part);
                *np = 0;
            } else if (buf[i] != '\r') {
                part[(*np)++] = buf[i];
            }
        }
    }
}

/* ---- a world's seed ---- */

static void stop(void)
{
    if (T.running)
        arena_os_end(&T.proc);
    T.running = false;
}

static void begin(void)
{
    stop();
    T.nlog = T.first = 0;
    T.npart = 0;
    T.head[0] = '\0';
    T.lengths = T.replays = T.turned = 0;
    T.replay_at = T.replay_of = 0;
    T.done = T.found = false;
    T.scroll = 0;
    T.follow = true;
    const char *args[] = {"--telescope", "world", T.path, "--progress"};
    char err[300];
    if (!arena_os_spawn(&T.proc, args, 4, err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "The search didn't start: %s", err);
        say(msg);
        return;
    }
    T.running = true;
    T.started = arena_os_now();
}

static void line_in(const char *s)
{
    long long a, b;
    if (!strncmp(s, "@found ", 7)) {
        long long seed;
        unsigned long long state;
        if (sscanf(s + 7, "%lld %llx", &seed, &state) == 2) {
            T.found = true;
            T.seed = seed;
            T.state = state;
        }
        T.done = true;
        return;
    }
    if (!strcmp(s, "@none")) {
        T.done = true;
        return;
    }
    if (sscanf(s, "@replay %lld %lld", &a, &b) == 2) {
        T.replay_at = a;
        T.replay_of = b;
        return;
    }
    if (s[0] == '@')
        return;
    if (!T.head[0] && strstr(s, " organisms ("))
        copy(T.head, sizeof T.head, s);
    if (!strncmp(s, "gene length ", 12))
        T.lengths++;
    if (strstr(s, "turned away at frame")) {
        T.turned++;
        T.replays++;
    } else if (strstr(s, "replayed to frame")) {
        T.replays++;
    }
    log_add(s);
}

static void poll_search(void)
{
    if (T.running && !child_lines(&T.proc, T.part, sizeof T.part, &T.npart, line_in)) {
        arena_os_end(&T.proc);
        T.running = false;
        T.took = arena_os_now() - T.started;
        if (!T.done)
            say("The search stopped before it could say");
    }
}

static void choose(const char *path, const char *shown)
{
    stop();
    snprintf(T.path, sizeof T.path, "%s", path);
    snprintf(T.shown, sizeof T.shown, "%s", shown);
    T.done = T.found = false;
    T.nlog = T.first = 0;
    T.head[0] = '\0';
}

/* the world on screen, as it is now, saved where the search can read it */
static void this_world(App *a)
{
    char path[1200], err[256];
    config_dir_make();
    snprintf(path, sizeof path, "%s/telescope-world.%s", params_config_dir(), WORLDFILE_EXT);
    if (!worldfile_save(&a->w, path, err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "This world can't be saved for the search: %s", err);
        say(msg);
        return;
    }
    choose(path, "this world, as it is now");
}

/* the saved world opened, its seed known from now on: it goes on as the
 * world it was (a save keeps the generator), and Restart begins it again */
static void open_found(App *a)
{
    char err[400];
    if (!app_load_world(a, T.path, err, sizeof err)) {
        say(err);
        return;
    }
    a->wo.seed = T.seed;
    a->wo.nimports = 0;
    app_do(a, ACT_HOME);
    a->screen = SCREEN_WORLD;
    interface_note(a, false, "The world of seed %" PRId64 ", at time %" PRId64, T.seed, world_time_units(&a->w));
}

/* ---- where a being lives ---- */

static void find_log(const char *s)
{
    if (F.nlog == 8) {
        memmove(F.log[0], F.log[1], sizeof F.log - sizeof F.log[0]);
        F.nlog--;
    }
    copy(F.log[F.nlog++], LINE, s);
}

static void find_stop(void)
{
    if (F.running)
        arena_os_end(&F.proc);
    F.running = false;
}

static void jump_stop(void)
{
    if (F.jumping)
        arena_os_end(&F.jproc);
    F.jumping = false;
}

static void find_begin(void)
{
    find_stop();
    F.nhits = F.nlog = F.nsets = 0;
    F.done = false;
    F.scroll = 0;
    F.npart = 0;
    F.cur_round = -1;
    F.prog_k = F.prog_n = 0;
    F.frame_now = F.frame_of = 0;
    F.anc_told = false;
    gc_unref(F.anc);
    F.anc = NULL;
    F.ndraws = 0;
    for (int i = 0; i < 4; i++)
        F.basin[i] = -1;
    /* the parameters as saved, and the arena's world if it differs */
    int w, h, d, n;
    syn_arena_world(&w, &h, &d, &n);
    char ws[16], hs[16], ds[16], ns[16];
    snprintf(ws, sizeof ws, "%d", w);
    snprintf(hs, sizeof hs, "%d", h);
    snprintf(ds, sizeof ds, "%d", d);
    snprintf(ns, sizeof ns, "%d", n);
    bool other = w != P.world_width || h != P.world_height || d != P.world_depth || n != P.initial_organisms;
    const char *args[] = {"--telescope", "find", F.path, "--progress", "--saved", "--world", ws, hs, ds, ns};
    char err[300];
    if (!arena_os_spawn(&F.proc, args, other ? 10 : 5, err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "The search didn't start: %s", err);
        say(msg);
        return;
    }
    F.running = true;
    F.started = arena_os_now();
}

void telescope_find(App *a, const GeneticCode *gc, const char *name)
{
    find_stop();
    jump_stop();
    T.mode = M_BEING;
    a->screen = SCREEN_TELESCOPE;
    snprintf(F.name, sizeof F.name, "%s", name && name[0] ? name : "this being");
    config_dir_make();
    snprintf(F.path, sizeof F.path, "%s/telescope-being.bgg", params_config_dir());
    char err[300];
    gc_unref(F.gc);
    F.gc = NULL;
    if (bgg_save(F.path, gc, err, sizeof err))
        F.gc = gc_ref((GeneticCode *)gc);
    F.nhits = F.nlog = F.nsets = 0;
    F.done = false;
    F.scroll = 0;
    F.anc_told = false;
    gc_unref(F.anc);
    F.anc = NULL;
    F.ndraws = 0;
    if (!F.gc)
        say("The being couldn't be kept for the search");
    else
        find_begin();
}

static void find_line(const char *s)
{
    Hit h;
    memset(&h, 0, sizeof h);
    long long seed, frame, fo;
    int a, b, c, d, e;
    char kind[12];
    if (sscanf(s, "@progress %d %d %d", &a, &b, &c) == 3) {
        F.prog_k = b;
        F.prog_n = c;
        F.frame_now = 0;
        return;
    }
    if (sscanf(s, "@frame %lld %lld", &frame, &fo) == 2) {
        F.frame_now = frame;
        F.frame_of = fo;
        return;
    }
    unsigned long long st;
    if (!strcmp(s, "@ancestor none")) {
        F.anc_told = true;
        return;
    }
    if (sscanf(s, "@ancestor %llx %d %d %d", &st, &a, &b, &c) == 4) {
        F.anc_told = true;
        F.anc_start = st;
        F.anc_proof = a;
        F.anc_kept = b;
        F.anc_changes = c;
        gc_unref(F.anc);
        F.anc = crack_founder_at(st);
        return;
    }
    char dk;
    if (sscanf(s, "@draw %d %lld %c", &a, &frame, &dk) == 3) {
        if (F.ndraws < 24) {
            F.draw[F.ndraws].gene = a;
            F.draw[F.ndraws].place = frame;
            F.draw[F.ndraws++].kind = dk;
        }
        return;
    }
    if (sscanf(s, "@basin %d %d", &a, &b) == 2) {
        if (a >= 0 && a < 4)
            F.basin[a] = b;
        return;
    }
    if (!strncmp(s, "@seed ", 6) || !strncmp(s, "@rounds ", 8))
        return;
    if (sscanf(s, "@set %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
        if (a >= 0 && a < 4) {
            F.set[a].w = b;
            F.set[a].h = c;
            F.set[a].d = d;
            F.set[a].n = e;
            F.nsets = a + 1 > F.nsets ? a + 1 : F.nsets;
        }
        return;
    }
    if (sscanf(s, "@scene %d %lld %lld %11s %d %d", &a, &seed, &frame, kind, &c, &d) == 6) {
        h.set = a;
        h.seed = seed;
    } else if (sscanf(s, "@round %d %d %lld", &a, &b, &seed) == 3) {
        F.cur_round = a;
        F.cur_cycle = b;
        F.cur_seed = seed;
        return;
    } else if (sscanf(s, "@rscene %d %lld %11s %d %d", &a, &frame, kind, &c, &d) == 5) {
        h.round = true;
        h.index = a;
        h.cycle = a == F.cur_round ? F.cur_cycle : 0;
        h.seed = a == F.cur_round ? F.cur_seed : 0;
    } else {
        if (!strcmp(s, "@done") || !strcmp(s, "@none"))
            F.done = true;
        else if (s[0] != '@')
            find_log(s);
        return;
    }
    h.frame = frame;
    snprintf(h.kind, sizeof h.kind, "%s", kind);
    h.n = c;
    h.d = d;
    if (F.nhits == HITS)
        return;
    /* a world's scenes come together: in it, by frame */
    int at = F.nhits;
    while (at > 0 && F.hit[at - 1].round == h.round && F.hit[at - 1].seed == h.seed &&
           F.hit[at - 1].index == h.index && F.hit[at - 1].frame > h.frame)
        at--;
    memmove(&F.hit[at + 1], &F.hit[at], (size_t)(F.nhits - at) * sizeof *F.hit);
    F.hit[at] = h;
    F.nhits++;
}

static void poll_find(void)
{
    if (F.running && !child_lines(&F.proc, F.part, sizeof F.part, &F.npart, find_line)) {
        arena_os_end(&F.proc);
        F.running = false;
        F.took = arena_os_now() - F.started;
        if (!F.done)
            say("The search stopped before it could say");
    }
}

static void jump_line(const char *s)
{
    unsigned long long st;
    long long fr, of;
    if (sscanf(s, "@at %llx", &st) == 1) {
        F.jstate = st;
        F.jat = true;
    } else if (sscanf(s, "@frame %lld %lld", &fr, &of) == 2) {
        F.jframe_now = fr;
    }
}

/* The scene made by this program again, then opened: a round of the
 * record, or a seed's world (whdn: its size and first organisms, NULL: the
 * parameters as saved). */
static void jump_spawn(const Hit *h, const int *whdn, int row)
{
    jump_stop();
    config_dir_make();
    snprintf(F.jpath, sizeof F.jpath, "%s/telescope-scene.%s", params_config_dir(), WORLDFILE_EXT);
    char a1[32], a2[32], ws[16], hs[16], ds[16], ns[16];
    const char *args[14];
    int n = 0;
    args[n++] = "--telescope";
    args[n++] = "scene";
    if (h->round) {
        args[n++] = "--round";
        snprintf(a1, sizeof a1, "%d", h->index);
    } else {
        snprintf(a1, sizeof a1, "%" PRId64, h->seed);
    }
    snprintf(a2, sizeof a2, "%" PRId64, h->frame);
    args[n++] = a1;
    args[n++] = a2;
    args[n++] = F.jpath;
    args[n++] = "--progress";
    if (!h->round && whdn) {
        snprintf(ws, sizeof ws, "%d", whdn[0]);
        snprintf(hs, sizeof hs, "%d", whdn[1]);
        snprintf(ds, sizeof ds, "%d", whdn[2]);
        snprintf(ns, sizeof ns, "%d", whdn[3]);
        args[n++] = "--world";
        args[n++] = ws;
        args[n++] = hs;
        args[n++] = ds;
        args[n++] = ns;
    }
    char err[300];
    F.jat = false;
    if (!arena_os_spawn(&F.jproc, args, n, err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "The scene couldn't be made: %s", err);
        say(msg);
        return;
    }
    F.jumping = true;
    F.jhit = row;
    F.jh = *h;
    F.jframe_now = 0;
}

static void jump_begin(int i)
{
    const Hit *h = &F.hit[i];
    int whdn[4];
    bool other = !h->round && h->set > 0 && h->set < F.nsets;
    if (other) {
        whdn[0] = F.set[h->set].w;
        whdn[1] = F.set[h->set].h;
        whdn[2] = F.set[h->set].d;
        whdn[3] = F.set[h->set].n;
    }
    jump_spawn(h, other ? whdn : NULL, i);
}

static void poll_jump(App *a)
{
    if (!F.jumping || child_lines(&F.jproc, F.jpart, sizeof F.jpart, &F.njpart, jump_line))
        return;
    arena_os_end(&F.jproc);
    F.jumping = false;
    if (!F.jat) {
        say("The scene couldn't be made");
        return;
    }
    /* the save keeps the generator: the world goes on as it went on */
    const Hit *h = &F.jh;
    char err[400];
    if (!app_load_world(a, F.jpath, err, sizeof err)) {
        say(err);
        return;
    }
    if (!h->round) {
        a->wo.seed = h->seed;
        a->wo.nimports = 0;
    }
    a->loaded[0] = 0;
    a->world_path[0] = 0;
    app_do(a, ACT_HOME);
    a->screen = SCREEN_WORLD;
    if (h->round)
        interface_note(a, false, "Arena round %d (seed %" PRId64 ") at frame %" PRId64, h->cycle, h->seed, h->frame);
    else
        interface_note(a, false, "Seed %" PRId64 " at frame %" PRId64 " (time %" PRId64 ")", h->seed, h->frame,
                       h->frame / 256);
}

static void hit_text(const Hit *h, char *out, size_t n)
{
    if (!strcmp(h->kind, "founder"))
        snprintf(out, n, "its gene %d, in a founder at the world's start", h->n);
    else if (!strcmp(h->kind, "gene"))
        snprintf(out, n, "its gene %d drawn here", h->n);
    else if (!strcmp(h->kind, "bend"))
        snprintf(out, n, "its gene %d's bend drawn here", h->n);
    else if (!strcmp(h->kind, "drive"))
        snprintf(out, n, "its %s drive drawn here", gc_drive_name(h->n - 1));
    else if (!strcmp(h->kind, "height") || !strcmp(h->kind, "cone"))
        snprintf(out, n, "its %s drawn here", h->kind);
    else if (!strcmp(h->kind, "alive"))
        snprintf(out, n, "alive: %d of it", h->n);
    else if (!strcmp(h->kind, "most"))
        snprintf(out, n, "the most of it: %d", h->n);
    else if (!strcmp(h->kind, "nearest"))
        snprintf(out, n, "its nearest relative: %d change%s", h->d, h->d == 1 ? "" : "s");
    else if (!strcmp(h->kind, "final"))
        snprintf(out, n, "the round ended with it (%d living)", h->n);
    else if (!strcmp(h->kind, "genesis"))
        snprintf(out, n, "its bloodline began the round as it");
    else if (!strcmp(h->kind, "ancestor"))
        snprintf(out, n, "its genesis ancestor, founder %d of the world", h->n);
    else if (!strcmp(h->kind, "lone"))
        snprintf(out, n, "its ancestor's bloodline alone: all %d living descend from it", h->n);
    else if (!strcmp(h->kind, "reigns"))
        snprintf(out, n, "it reigns: %d of it alive, its bloodline the only one", h->n);
    else if (!strcmp(h->kind, "gone"))
        snprintf(out, n, "its ancestor's bloodline dies out");
    else
        snprintf(out, n, "%s", h->kind);
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

static void being_view(App *a, float x, float y, float tw, float bottom)
{
    const Fonts *fo = shell_fonts();
    char line[400], fit[1200];
    y = para(fo->body,
             "Its genes read back to where they were drawn: every gene length is one draw of the world's dice, "
             "so it points to a place in a seed's stream. A gene its founders had gives the seed; the arena's "
             "rounds, whose seeds are in the record, are searched for the rest. Each scene opens that world at "
             "that frame, its dice where they were.",
             x, y, tw, TONE_MUTED) +
        12;
    UIRect pic = {x, y, 150, 110};
    body(pic, F.gc, a->seconds);
    float bx = pic.x + pic.w + 18;
    skin_text(fo->heading, F.gc ? F.name : "No being chosen", bx, y + 2, F.gc ? TONE_TEXT : TONE_MUTED, ALIGN_LEFT);
    if (F.gc) {
        char plan[200];
        syn_family_plan(F.gc, plan, sizeof plan);
        skin_text(fo->small, skin_fit(fo->small, plan, x + tw - bx, fit, sizeof fit), bx,
                  y + skin_text_height(fo->heading) + 8, TONE_MUTED, ALIGN_LEFT);
    } else {
        skin_text(fo->small, "Pick one in the library, or an organism in the world, and choose WHERE IT LIVES.", bx,
                  y + skin_text_height(fo->heading) + 8, TONE_MUTED, ALIGN_LEFT);
    }
    float by = y + 110 - 38;
    UIRect go = {bx, by, 250, 38}, lib = {go.x + go.w + 8, by, 130, 38};
    if (button(go, F.running ? "STOP" : "FIND WHERE IT LIVES", F.running, F.running || F.gc)) {
        if (F.running) {
            find_stop();
            say("Stopped");
        } else {
            find_begin();
        }
    }
    if (button(lib, "LIBRARY", false, true)) {
        app_do(a, ACT_LIBRARY_TAB);
        return;
    }
    double secs = F.running ? arena_os_now() - F.started : F.took;
    if (F.running || F.done) {
        char now[120] = "";
        if (F.running && F.prog_n && F.frame_of)
            snprintf(now, sizeof now, "  ·  running seed %d of %d, time %" PRId64 " of %" PRId64, F.prog_k, F.prog_n,
                     F.frame_now / 256, F.frame_of / 256);
        else if (F.running && F.prog_n)
            snprintf(now, sizeof now, "  ·  running seed %d of %d", F.prog_k, F.prog_n);
        else if (F.running)
            snprintf(now, sizeof now, "  ·  searching");
        snprintf(line, sizeof line, "%d scene%s  ·  %.0f s%s", F.nhits, F.nhits == 1 ? "" : "s", secs, now);
        skin_text(fo->body, line, lib.x + lib.w + 20, by + 10, F.running ? TONE_ACCENT : TONE_MUTED, ALIGN_LEFT);
    }
    y += 110 + 16;

    /* what the engine read of it: its genesis ancestor and its later draws */
    if (F.anc_told && F.anc) {
        UIRect ap = {x, y, 150, 78};
        body(ap, F.anc, 0);
        float ax = ap.x + ap.w + 18, aw = x + tw - ax;
        float lh2 = skin_text_height(fo->small) + 4;
        skin_text(fo->small_bold, "ITS GENESIS ANCESTOR", ax, y, TONE_ACCENT, ALIGN_LEFT);
        float ty = y + skin_text_height(fo->small_bold) + 5;
        char plan[200];
        syn_family_plan(F.anc, plan, sizeof plan);
        skin_text(fo->small, skin_fit(fo->small, plan, aw, fit, sizeof fit), ax, ty, TONE_TEXT, ALIGN_LEFT);
        ty += lh2;
        snprintf(line, sizeof line, "drawn again from state 0x%012" PRIx64 "  ·  %d of its %d genes kept, %d mutation%s at"
                 " the fewest", F.anc_start, F.anc_kept, F.gc ? F.gc->ngenes : 0, F.anc_changes,
                 F.anc_changes == 1 ? "" : "s");
        for (int i = 0; i < F.nsets && i < 4; i++)
            if (F.basin[i] >= 0) {
                size_t k = strlen(line);
                snprintf(line + k, sizeof line - k, "%sa founder in %d seed%s in %d x %d x %d with %d first organisms",
                         k ? "  ·  " : "", F.basin[i], F.basin[i] == 1 ? "" : "s", F.set[i].w, F.set[i].h,
                         F.set[i].d, F.set[i].n);
            }
        if (line[0])
            skin_text(fo->small, skin_fit(fo->small, line, aw, fit, sizeof fit), ax, ty, TONE_MUTED, ALIGN_LEFT);
        ty += lh2;
        line[0] = '\0';
        for (int i = 0; i < F.ndraws; i++) {
            size_t k = strlen(line);
            char c = F.draw[i].kind, who[48];
            const char *what = c == 'a' ? "turn" : c == 'b' ? "bend" : c == 'w' ? "gene, added" : "length";
            if (c == 'd')
                snprintf(who, sizeof who, "its %s drive", gc_drive_name(F.draw[i].gene - 1));
            else if (c == 'h' || c == 'k')
                snprintf(who, sizeof who, "its %s", c == 'h' ? "height" : "cone");
            else
                snprintf(who, sizeof who, "gene %d's %s", F.draw[i].gene, what);
            if (F.draw[i].place >= 0)
                snprintf(line + k, sizeof line - k, "%s%s at draw %" PRId64, k ? "  ·  " : "Drawn since: ", who,
                         F.draw[i].place);
            else
                snprintf(line + k, sizeof line - k, "%s%s in another stream", k ? "  ·  " : "Drawn since: ", who);
        }
        if (!F.ndraws)
            snprintf(line, sizeof line, "Nothing of it drawn since: it is in every one of those worlds from frame 0,"
                                        " or comes of it by what can't be placed (colours, arms, genes dropped)");
        skin_text(fo->small, skin_fit(fo->small, line, aw, fit, sizeof fit), ax, ty, TONE_MUTED, ALIGN_LEFT);
        y += ap.h + 14;
    } else if (F.anc_told) {
        y = para(fo->small,
                 "No genesis ancestor: none of its genes is still a founder's with another of its draws to prove it. "
                 "Its seed has faded from it; only the arena's rounds can place it.",
                 x, y, tw, TONE_MUTED) +
            10;
    }

    /* the search's last words: fewer once there are scenes to show */
    float lh = skin_text_height(fo->small) + 3;
    int nlog = F.nhits && F.nlog > 3 ? 3 : F.nlog;
    float logh = (float)nlog * lh;
    /* the scenes, by world */
    UIRect list = {x, y, tw, bottom - y - logh - 10};
    if (list.h > 60 && F.nhits) {
        float rh = 40, hh = skin_text_height(fo->small_bold) + 14;
        float total = 0;
        for (int i = 0; i < F.nhits; i++) {
            bool head = !i || F.hit[i].round != F.hit[i - 1].round || F.hit[i].seed != F.hit[i - 1].seed ||
                        F.hit[i].index != F.hit[i - 1].index;
            total += rh + (head ? hh : 0);
        }
        float maxs = fmaxf(0, total - list.h);
        if (over(list))
            F.scroll -= uiin_wheel() * rh * 2;
        F.scroll = fminf(fmaxf(F.scroll, 0), maxs);
        gl2d_clip_push(list.x, list.y, list.w, list.h);
        float ry = list.y - F.scroll;
        for (int i = 0; i < F.nhits; i++) {
            const Hit *hh_ = &F.hit[i];
            bool head = !i || hh_->round != F.hit[i - 1].round || hh_->seed != F.hit[i - 1].seed ||
                        hh_->index != F.hit[i - 1].index;
            if (head) {
                if (hh_->round)
                    snprintf(line, sizeof line, "ARENA ROUND %d  ·  seed %" PRId64 "  ·  as the round began: its seed and fighters",
                             hh_->cycle, hh_->seed);
                else if (hh_->set >= 0 && hh_->set < F.nsets)
                    snprintf(line, sizeof line, "SEED %" PRId64 "  ·  %d x %d x %d, %d first organisms", hh_->seed,
                             F.set[hh_->set].w, F.set[hh_->set].h, F.set[hh_->set].d, F.set[hh_->set].n);
                else
                    snprintf(line, sizeof line, "SEED %" PRId64, hh_->seed);
                if (ry + hh > list.y && ry < list.y + list.h)
                    skin_text(fo->small_bold, line, list.x, ry + 8, TONE_ACCENT, ALIGN_LEFT);
                ry += hh;
            }
            if (ry + rh > list.y && ry < list.y + list.h) {
                UIRect row = {list.x, ry, list.w, rh - 4};
                skin_fill(row, skin_theme()->accent, over(row) ? 0.12f : 0.05f);
                char what[200];
                hit_text(hh_, what, sizeof what);
                skin_text(fo->body, what, row.x + 12, row.y + 9,
                          !strcmp(hh_->kind, "reigns") ? TONE_GOOD : !strcmp(hh_->kind, "gone") ? TONE_MUTED : TONE_TEXT,
                          ALIGN_LEFT);
                snprintf(line, sizeof line, "frame %" PRId64 "  ·  time %" PRId64, hh_->frame, hh_->frame / 256);
                skin_text(fo->small, line, row.x + row.w - 150, row.y + 11, TONE_MUTED, ALIGN_RIGHT);
                UIRect jb = {row.x + row.w - 130, row.y + 3, 120, row.h - 6};
                bool busy = F.jumping && F.jhit == i;
                if (button(jb, busy ? "MAKING..." : "JUMP", busy, !F.jumping))
                    jump_begin(i);
                if (over(jb) && !F.jumping)
                    uiin_tooltip("Make this world at this frame and open it, its dice where they were");
            }
            ry += rh;
        }
        gl2d_clip_pop();
    } else if (F.done && !F.nhits) {
        para(fo->body,
             "Nowhere found. Every gene it has was drawn after some world's start, in no round of the record: "
             "a gene drawn later says where in a stream it was drawn, not which stream. Its seed has faded.",
             x, y, tw, TONE_WARN);
    }
    float ly = bottom - logh;
    for (int i = 0; i < nlog; i++)
        skin_text(fo->small, skin_fit(fo->small, F.log[F.nlog - nlog + i], tw, fit, sizeof fit), x,
                  ly + (float)i * lh, TONE_MUTED, ALIGN_LEFT);
    if (F.jumping) {
        snprintf(line, sizeof line, "Making the scene: frame %" PRId64 " of %" PRId64, F.jframe_now, F.jh.frame);
        say(line);
    }
}

/* ---- a winner from the dice ---- */

static void forge_stop(void)
{
    if (G.running)
        arena_os_end(&G.proc);
    G.running = false;
}

static void forge_clear(void)
{
    for (int i = 0; i < G.nr; i++)
        gc_unref(G.r[i].gc);
    G.nr = 0;
}

static void forge_begin(void)
{
    forge_stop();
    forge_clear();
    G.done = false;
    G.scanned = 0;
    G.prog_k = G.prog_n = 0;
    G.frame_now = G.frame_of = 0;
    G.npart = 0;
    G.last[0] = '\0';
    G.scroll = 0;
    /* each forging goes on where the last left off: the seeds in order */
    G.from = G.next_from;
    syn_arena_world(&G.w, &G.h, &G.d, &G.n);
    config_dir_make();
    snprintf(G.dir, sizeof G.dir, "%s/telescope-forge", params_config_dir());
    if (mkdir(G.dir, 0755) != 0 && errno != EEXIST) {
        say("The forge has nowhere to put its winners");
        return;
    }
    char from[32], scan[32], top[16], ws[16], hs[16], ds[16], ns[16];
    snprintf(from, sizeof from, "%" PRId64, G.from);
    snprintf(scan, sizeof scan, "%" PRId64, G.scan);
    snprintf(top, sizeof top, "%d", G.top);
    snprintf(ws, sizeof ws, "%d", G.w);
    snprintf(hs, sizeof hs, "%d", G.h);
    snprintf(ds, sizeof ds, "%d", G.d);
    snprintf(ns, sizeof ns, "%d", G.n);
    const char *args[] = {"--telescope", "forge", "--progress", "--from", from, "--scan", scan, "--top", top,
                          "--out",       G.dir,   "--world",    ws,       hs,   ds,     ns};
    char err[300];
    if (!arena_os_spawn(&G.proc, args, (int)(sizeof args / sizeof *args), err, sizeof err)) {
        char msg[400];
        snprintf(msg, sizeof msg, "The forge didn't start: %s", err);
        say(msg);
        return;
    }
    G.running = true;
    G.started = arena_os_now();
    G.next_from = G.from + G.scan;
}

static void forge_line(const char *s)
{
    long long seed, scanned, total, fr, of;
    int win, lead, t, copies, changes, lone, a, b, c;
    double chance, share;
    char path[1200];
    if (sscanf(s, "@scan %lld %lld", &scanned, &total) == 2) {
        G.scanned = scanned;
    } else if (sscanf(s, "@progress %d %d %d", &a, &b, &c) == 3) {
        G.prog_k = b;
        G.prog_n = c;
        G.frame_now = 0;
    } else if (sscanf(s, "@frame %lld %lld", &fr, &of) == 2) {
        G.frame_now = fr;
        G.frame_of = of;
    } else if (sscanf(s, "@forge %lld %d %d %d %lf %d %d %d %lf", &seed, &win, &lead, &t, &chance, &copies, &changes,
                      &lone, &share) == 9) {
        if (G.nr < FORGED) {
            G.r[G.nr].seed = seed;
            G.r[G.nr].win = win;
            G.r[G.nr].lead = lead;
            G.r[G.nr].t = t;
            G.r[G.nr].copies = copies;
            G.r[G.nr].changes = changes;
            G.r[G.nr].lone = lone != 0;
            G.r[G.nr].chance = chance;
            G.r[G.nr].share = share;
            G.r[G.nr].entered = false;
            G.r[G.nr++].gc = NULL;
        }
    } else if (sscanf(s, "@file %lld %1199[^\n]", &seed, path) == 2) {
        for (int i = 0; i < G.nr; i++)
            if (G.r[i].seed == seed && !G.r[i].gc) {
                char err[200];
                G.r[i].gc = bgg_load(path, err, sizeof err);
            }
    } else if (!strcmp(s, "@done")) {
        G.done = true;
    } else if (s[0] != '@') {
        copy(G.last, sizeof G.last, s);
    }
}

static void poll_forge(void)
{
    if (G.running && !child_lines(&G.proc, G.part, sizeof G.part, &G.npart, forge_line)) {
        arena_os_end(&G.proc);
        G.running = false;
        G.took = arena_os_now() - G.started;
        if (!G.done)
            say("The forge stopped before it could say");
    }
}

/* A forged winner on the arena's ladder at level 1, under the arena's
 * settings as they are now (the forge weighs seeds in the arena's world) */
static bool forge_enter(int i)
{
    char name[64], err[200], msg[400];
    snprintf(name, sizeof name, "forged-%" PRId64, G.r[i].seed);
    const ArenaEntry *e = syn_arena_enter(G.r[i].gc, name, err, sizeof err);
    if (!e) {
        snprintf(msg, sizeof msg, "Not on the ladder: %s", err);
        say(msg);
        return false;
    }
    G.r[i].entered = true;
    snprintf(msg, sizeof msg, "%s is on the arena's ladder at level 1", e->name);
    say(msg);
    return true;
}

static void forge_view(App *a, float x, float y, float tw, float bottom)
{
    const Fonts *fo = shell_fonts();
    char line[400], fit[1200];
    y = para(fo->body, FORGE_TEXT, x, y, tw, TONE_MUTED) + 12;
    skin_text(fo->small_bold, "SEEDS TO WEIGH", x, y, TONE_MUTED, ALIGN_LEFT);
    skin_text(fo->small_bold, "TO RUN", x + 520, y, TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 6;
    static const int64_t scans[] = {20000, 200000, 2000000};
    static const char *scan_s[] = {"20,000", "200,000", "2,000,000"};
    for (int i = 0; i < 3; i++)
        if (button((UIRect){x + (float)i * 166, y, 160, 34}, scan_s[i], G.scan == scans[i], !G.running))
            G.scan = scans[i];
    static const int tops[] = {4, 8, 16};
    for (int i = 0; i < 3; i++) {
        char t[8];
        snprintf(t, sizeof t, "%d", tops[i]);
        if (button((UIRect){x + 520 + (float)i * 76, y, 70, 34}, t, G.top == tops[i], !G.running))
            G.top = tops[i];
    }
    y += 34 + 14;
    UIRect go = {x, y, 200, 40};
    if (button(go, G.running ? "STOP" : "FORGE", G.running, true)) {
        if (G.running) {
            forge_stop();
            say("Stopped");
        } else {
            forge_begin();
        }
    }
    if (over(go) && !G.running)
        uiin_tooltip("Weigh the next seeds and run the likeliest, in the arena's world");
    /* every winner not yet there, to the ladder */
    int waiting = 0;
    for (int i = 0; i < G.nr; i++)
        waiting += G.r[i].gc && G.r[i].lead >= 0 && !G.r[i].entered;
    UIRect ball = {x + tw - 230, y, 230, 40};
    if (button(ball, "ALL TO THE ARENA", false, waiting && !G.running)) {
        int put = 0;
        for (int i = 0; i < G.nr; i++)
            if (G.r[i].gc && G.r[i].lead >= 0 && !G.r[i].entered)
                put += forge_enter(i);
        char msg[200];
        snprintf(msg, sizeof msg, "%d forged winner%s on the arena's ladder at level 1", put, put == 1 ? "" : "s");
        say(msg);
    }
    if (over(ball))
        uiin_tooltip("Put each winner on the arena's ladder at level 1, to fight its way up");
    double secs = G.running ? arena_os_now() - G.started : G.took;
    if (G.running && G.prog_n)
        snprintf(line, sizeof line, "seeds %" PRId64 " to %" PRId64 " weighed  ·  running %d of %d, time %" PRId64
                 " of %" PRId64 "  ·  %.0f s", G.from, G.from + G.scan - 1, G.prog_k, G.prog_n, G.frame_now / 256,
                 G.frame_of / 256, secs);
    else if (G.running)
        snprintf(line, sizeof line, "weighing seeds from %" PRId64 ": %" PRId64 " of %" PRId64 "  ·  %.0f s", G.from,
                 G.scanned, G.scan, secs);
    else if (G.done)
        snprintf(line, sizeof line, "seeds %" PRId64 " to %" PRId64 ", %d x %d x %d with %d first organisms  ·  %.0f s",
                 G.from, G.from + G.scan - 1, G.w, G.h, G.d, G.n, secs);
    else
        snprintf(line, sizeof line, "next: from seed %" PRId64, G.next_from);
    skin_text(fo->body, skin_fit(fo->body, line, ball.x - go.x - go.w - 40, fit, sizeof fit), go.x + go.w + 20, y + 10,
              G.running ? TONE_ACCENT : TONE_MUTED, ALIGN_LEFT);
    y += 40 + 16;

    float lh = skin_text_height(fo->small) + 3;
    UIRect list = {x, y, tw, bottom - y - (G.last[0] ? lh + 6 : 0)};
    float rh = 74;
    float maxs = fmaxf(0, (float)G.nr * rh - list.h);
    if (over(list))
        G.scroll -= uiin_wheel() * rh;
    G.scroll = fminf(fmaxf(G.scroll, 0), maxs);
    gl2d_clip_push(list.x, list.y, list.w, list.h);
    for (int i = 0; i < G.nr; i++) {
        float ry = list.y + (float)i * rh - G.scroll;
        if (ry + rh < list.y || ry > list.y + list.h)
            continue;
        UIRect row = {list.x, ry, list.w, rh - 6};
        skin_fill(row, skin_theme()->accent, over(row) ? 0.12f : 0.05f);
        body((UIRect){row.x + 6, row.y + 6, 96, row.h - 12}, G.r[i].gc, 0);
        float tx = row.x + 116;
        bool acts = G.r[i].gc && G.r[i].lead >= 0;
        UIRect bj = {row.x + row.w - 126, row.y + (row.h - 34) / 2, 118, 34};
        UIRect bk = {bj.x - 104, bj.y, 98, 34}, bw = {bk.x - 180, bj.y, 174, 34};
        UIRect ba = {bw.x - 166, bj.y, 160, 34};
        float txw = (acts ? ba.x - 12 : row.x + row.w - 8) - tx;
        snprintf(line, sizeof line, "SEED %" PRId64 "  ·  founder %d expected, %.0f%%", G.r[i].seed, G.r[i].win + 1,
                 100 * G.r[i].chance);
        skin_text(fo->body, skin_fit(fo->body, line, txw, fit, sizeof fit), tx, row.y + 10, TONE_TEXT, ALIGN_LEFT);
        Tone tone = TONE_MUTED;
        if (G.r[i].lead < 0) {
            snprintf(line, sizeof line, "all died by time %d", G.r[i].t);
        } else {
            snprintf(line, sizeof line, "founder %d's bloodline %s at time %d (%.0f%% of the living), as %d of this being"
                     " (%d mutation%s from that founder)%s", G.r[i].lead + 1, G.r[i].lone ? "alone" : "leads",
                     G.r[i].t, 100 * G.r[i].share, G.r[i].copies, G.r[i].changes, G.r[i].changes == 1 ? "" : "s",
                     G.r[i].lead == G.r[i].win ? "  ·  as expected" : "");
            tone = G.r[i].lead == G.r[i].win ? TONE_GOOD : TONE_TEXT;
        }
        skin_text(fo->small, skin_fit(fo->small, line, txw, fit, sizeof fit), tx,
                  row.y + 12 + skin_text_height(fo->body), tone, ALIGN_LEFT);
        if (!acts)
            continue;
        char name[64];
        snprintf(name, sizeof name, "seed %" PRId64, G.r[i].seed);
        bool busy = F.jumping && F.jhit == -1 && F.jh.seed == G.r[i].seed;
        if (button(bj, busy ? "MAKING..." : "JUMP", busy, !F.jumping)) {
            Hit h;
            memset(&h, 0, sizeof h);
            h.seed = G.r[i].seed;
            h.frame = (int64_t)G.r[i].t * 256;
            snprintf(h.kind, sizeof h.kind, "lone");
            int whdn[4] = {G.w, G.h, G.d, G.n};
            jump_spawn(&h, whdn, -1);
        }
        if (over(bj) && !F.jumping)
            uiin_tooltip("Make this world at the time its run ended, and open it");
        if (button(bk, "KEEP", false, true)) {
            char origin[200];
            snprintf(origin, sizeof origin, "forged: seed %" PRId64 ", founder %d's bloodline %s at time %d",
                     G.r[i].seed, G.r[i].lead + 1, G.r[i].lone ? "alone" : "leading", G.r[i].t);
            app_library_keep(a, G.r[i].gc, "Forged", name, origin);
        }
        if (over(bk))
            uiin_tooltip("Keep it in the being library, in Forged");
        if (button(bw, "WHERE IT LIVES", false, true)) {
            telescope_find(a, G.r[i].gc, name);
            gl2d_clip_pop();
            return;
        }
        if (button(ba, G.r[i].entered ? "ON THE LADDER" : "TO THE ARENA", G.r[i].entered, !G.r[i].entered))
            forge_enter(i);
        if (over(ba) && !G.r[i].entered)
            uiin_tooltip("Put it on the arena's ladder at level 1, to fight its way up");
    }
    gl2d_clip_pop();
    if (G.last[0])
        skin_text(fo->small, skin_fit(fo->small, G.last, tw, fit, sizeof fit), x, bottom - lh, TONE_MUTED, ALIGN_LEFT);
    if (F.jumping && F.jhit == -1) {
        snprintf(line, sizeof line, "Making the scene: frame %" PRId64 " of %" PRId64, F.jframe_now, F.jh.frame);
        say(line);
    }
}

/* ---- the screen ---- */

void telescope_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    poll_search();
    poll_find();
    poll_forge();
    poll_jump(a);
    skin_fill(ui_rect(0, 0, (float)w, (float)h), (Rgb){0, 0, 0}, 0.5f);
    float top = SHELL_BAR_H + 10;
    float pw = fminf((float)w - 32, 1500), ph = (float)h - top - (a->hints ? SHELL_HINTS_H : 0) - 12;
    UIRect panel = {((float)w - pw) / 2, top, pw, ph};
    uiin_solid(panel);
    skin_panel(panel, true);
    float x = panel.x + PAD, y = panel.y + 14, right = panel.x + panel.w - PAD;
    x += skin_text(fo->big, "TELESCOPE", x, y, TONE_TEXT, ALIGN_LEFT) + 14;
    skin_text(fo->body, "the dice as algebra", x, y + 9, TONE_MUTED, ALIGN_LEFT);
    y += 50;
    float tw = right - (panel.x + PAD);
    x = panel.x + PAD;
    UIRect t1 = {x, y, 200, 34}, t2 = {x + 206, y, 250, 34}, t3 = {t2.x + t2.w + 6, y, 220, 34};
    if (button(t1, "A WORLD'S SEED", T.mode == M_SEED, true))
        T.mode = M_SEED;
    if (button(t2, "WHERE A BEING LIVES", T.mode == M_BEING, true))
        T.mode = M_BEING;
    if (button(t3, "FORGE A WINNER", T.mode == M_FORGE, true))
        T.mode = M_FORGE;
    y += 34 + 16;
    float status_y = panel.y + panel.h - PAD - skin_text_height(fo->small);
    if (T.mode == M_FORGE) {
        forge_view(a, x, y, tw, status_y - 10);
        if (T.status[0] && SDL_GetTicks() - T.status_at < 8000)
            skin_text(fo->small, T.status, x, status_y, TONE_ACCENT, ALIGN_LEFT);
        return;
    }
    if (T.mode == M_BEING) {
        being_view(a, x, y, tw, status_y - 10);
        if (T.status[0] && SDL_GetTicks() - T.status_at < 8000)
            skin_text(fo->small, T.status, x, status_y, TONE_ACCENT, ALIGN_LEFT);
        return;
    }
    y = para(fo->body,
             "A world's seed, read back from its save: the genes its founders drew at frame 0 still in it point "
             "to the seed, and replaying that seed from frame 0 to the saved frame, with the save's own "
             "parameters, proves it.",
             x, y, tw, TONE_MUTED) +
        14;

    /* which world */
    skin_text(fo->small_bold, "WORLD", x, y, TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 6;
    UIRect bthis = {x, y, 170, 34}, bsaved = {x + 178, y, 210, 34};
    if (button(bthis, "THIS WORLD", false, !T.running))
        this_world(a);
    if (over(bthis))
        uiin_tooltip("The world on screen, saved as it is now for the search");
    if (button(bsaved, "A SAVED WORLD...", false, !T.running))
        files_open(a, FILES_TELESCOPE_WORLD);
    if (over(bsaved))
        uiin_tooltip("A ." WORLDFILE_EXT " file");
    char fit[1200];
    skin_text(fo->body, T.path[0] ? skin_fit(fo->body, T.shown, right - bsaved.x - bsaved.w - 20, fit, sizeof fit)
                                  : "none chosen",
              bsaved.x + bsaved.w + 20, y + 7, T.path[0] ? TONE_TEXT : TONE_MUTED, ALIGN_LEFT);
    y += 34 + 18;

    UIRect go = {x, y, 200, 40};
    if (button(go, T.running ? "STOP" : "FIND ITS SEED", T.running, T.running || T.path[0])) {
        if (T.running) {
            stop();
            say("Stopped");
        } else {
            begin();
        }
    }
    char line[400];
    double secs = T.running ? arena_os_now() - T.started : T.took;
    if (T.running || T.nlog) {
        char rep[80] = "";
        if (T.running && T.replay_of)
            snprintf(rep, sizeof rep, "  ·  replaying, frame %" PRId64 " of %" PRId64, T.replay_at, T.replay_of);
        snprintf(line, sizeof line, "%d gene length%s tried  ·  %d replay%s, %d of them turned away early  ·  %.0f s%s%s",
                 T.lengths, T.lengths == 1 ? "" : "s", T.replays, T.replays == 1 ? "" : "s", T.turned, secs,
                 T.running ? "  ·  searching" : "", rep);
        skin_text(fo->body, skin_fit(fo->body, line, tw - go.w - 20, fit, sizeof fit), go.x + go.w + 20, y + 10,
                  T.running ? TONE_ACCENT : TONE_MUTED, ALIGN_LEFT);
    }
    y += 40 + 16;

    /* the answer */
    if (T.done && T.found) {
        float cy = y + 12;
        float h1 = skin_text_height(fo->big), h2 = skin_text_height(fo->body), h3 = skin_text_height(fo->small);
        UIRect card = {x, y, tw, 12 + h1 + 6 + h2 + 4 + h3 + 12 + 38 + 14};
        skin_fill(card, skin_theme()->accent, 0.14f);
        char big[80];
        snprintf(big, sizeof big, "SEED %" PRId64, T.seed);
        skin_text(fo->big, big, card.x + 16, cy, TONE_TEXT, ALIGN_LEFT);
        cy += h1 + 6;
        skin_text(fo->body, "Replayed from frame 0, it is the saved world. So is every seed equal to it mod 2^48.",
                  card.x + 16, cy, TONE_GOOD, ALIGN_LEFT);
        cy += h2 + 4;
        snprintf(line, sizeof line, "The generator at the saved frame: 0x%012" PRIx64, T.state);
        skin_text(fo->small, line, card.x + 16, cy, TONE_MUTED, ALIGN_LEFT);
        cy += h3 + 12;
        UIRect bopen = {card.x + 16, cy, 300, 38};
        UIRect bcopy = {bopen.x + bopen.w + 8, cy, 150, 38};
        if (button(bopen, "OPEN IT, ITS SEED KNOWN", false, true))
            open_found(a);
        if (over(bopen))
            uiin_tooltip("Open the saved world, which goes on as the world it was; Restart then begins it again from"
                         " its seed");
        if (button(bcopy, "COPY SEED", false, true)) {
            char sd[32];
            snprintf(sd, sizeof sd, "%" PRId64, T.seed);
            SDL_SetClipboardText(sd);
            say("The seed is on the clipboard");
        }
        y += card.h + 14;
    } else if (T.done) {
        skin_text(fo->heading, "No seed found", x, y, TONE_WARN, ALIGN_LEFT);
        skin_text(fo->small,
                  "Its founders' genes are gone, it was changed from the menus or with other parameters on the way, "
                  "or it began with beings put in. The log says what was tried.",
                  x, y + skin_text_height(fo->heading) + 4, TONE_MUTED, ALIGN_LEFT);
        y += skin_text_height(fo->heading) + skin_text_height(fo->small) + 18;
    }
    if (T.head[0]) {
        skin_text(fo->small, skin_fit(fo->small, T.head, tw, fit, sizeof fit), x, y, TONE_MUTED, ALIGN_LEFT);
        y += skin_text_height(fo->small) + 8;
    }

    /* what the search says, the newest at the end */
    float bottom = status_y - 10;
    UIRect logr = {x, y, tw, bottom - y};
    if (logr.h > 40 && T.nlog) {
        skin_fill(logr, (Rgb){0, 0, 0}, 0.35f);
        float lh = skin_text_height(fo->small) + 3;
        float maxs = fmaxf(0, (float)T.nlog * lh + 16 - logr.h);
        if (over(logr)) {
            float dy = uiin_wheel();
            if (dy != 0) {
                T.scroll -= dy * lh * 3;
                T.follow = false;
            }
        }
        if (T.follow)
            T.scroll = maxs;
        T.scroll = fminf(fmaxf(T.scroll, 0), maxs);
        if (T.scroll >= maxs - 1)
            T.follow = true;
        gl2d_clip_push(logr.x, logr.y, logr.w, logr.h);
        int i0 = (int)(T.scroll / lh);
        for (int i = i0; i < T.nlog; i++) {
            float ly = logr.y + 8 + (float)i * lh - T.scroll;
            if (ly > logr.y + logr.h)
                break;
            const char *s = log_line(i);
            Tone t = strstr(s, "identical to the saved world") ? TONE_GOOD
                     : strstr(s, "turned away") || strstr(s, "not this world") ? TONE_MUTED
                                                                                : TONE_TEXT;
            skin_text(fo->small, skin_fit(fo->small, s, logr.w - 20, fit, sizeof fit), logr.x + 10, ly, t, ALIGN_LEFT);
        }
        gl2d_clip_pop();
    }
    if (T.status[0] && SDL_GetTicks() - T.status_at < 8000)
        skin_text(fo->small, T.status, x, status_y, TONE_ACCENT, ALIGN_LEFT);
}

void telescope_chosen(App *a, const char *path)
{
    (void)a;
    const char *base = strrchr(path, '/');
    choose(path, base ? base + 1 : path);
    T.mode = M_SEED;
}

void telescope_screen_free(void)
{
    stop();
    find_stop();
    jump_stop();
    forge_stop();
    forge_clear();
    gc_unref(F.gc);
    gc_unref(F.anc);
    F.gc = F.anc = NULL;
}
