/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/ui/filedlg.c (its SYN-OS browser).
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

/* Opening and saving, as SYN-BIOGENESIS's SYN-OS does it: syn-crypter's
 * three columns (the folder above, this one with sizes and dates, and a
 * preview that draws a .bgg's being), the path as buttons, HOME and the
 * root (on Windows the drive's), a name to type, and keys: up and down
 * pick, Enter opens, Backspace goes up a folder, Esc cancels. */

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app/app_internal.h"
#include "compat.h"
#include "io/bgg.h"
#include "io/worldfile.h"
#include "syn/library.h"
#include "syn/sidecar.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

typedef struct {
    char dir[4096];
    char ext[32]; /* "b3w", "bgg|b3w": one extension or several */
    char **entries; /* folders end in '/' */
    int n, selected;
} Listing;

static struct {
    int purpose;
    bool save;
    Screen back; /* where it goes when done */
    Listing here, parent;
    char parent_of[4096];
    char name[256];
    float scroll, pscroll;
    char last_dir[FILES_PURPOSES][4096]; /* each purpose's folder, the last one used */
    /* a double click: the entry and when it was clicked */
    int click_idx;
    Uint32 click_at;
    /* the .bgg previewed, read once */
    char preview_path[4400];
    GeneticCode *preview;
    char preview_err[200];
} F = {.click_idx = -1};

static void listing_free(Listing *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->entries[i]);
    free(l->entries);
    l->entries = NULL;
    l->n = 0;
}

static int cmp_entry(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    size_t lx = strlen(x), ly = strlen(y);
    bool dx = lx && x[lx - 1] == '/', dy = ly && y[ly - 1] == '/';
    if (dx != dy)
        return dx ? -1 : 1;
    return strcasecmp(x, y);
}

static bool has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return false;
    for (const char *e = ext; *e;) {
        size_t n = strcspn(e, "|");
        if (strlen(dot + 1) == n && !strncasecmp(dot + 1, e, n))
            return true;
        e += n + (e[n] == '|');
    }
    return false;
}

static void scan(Listing *l)
{
    listing_free(l);
    l->selected = -1;
    DIR *dir = opendir(l->dir);
    if (!dir)
        return;
    int cap = 64;
    l->entries = malloc((size_t)cap * sizeof *l->entries);
    if (!l->entries)
        abort();
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.')
            continue;
        char full[4400];
        snprintf(full, sizeof full, "%s/%s", l->dir, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        bool is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !has_ext(e->d_name, l->ext))
            continue;
        if (l->n == cap) {
            cap *= 2;
            char **ne = realloc(l->entries, (size_t)cap * sizeof *l->entries);
            if (!ne)
                abort();
            l->entries = ne;
        }
        size_t len = strlen(e->d_name);
        char *s = malloc(len + 2);
        if (!s)
            abort();
        memcpy(s, e->d_name, len);
        if (is_dir)
            s[len++] = '/';
        s[len] = '\0';
        l->entries[l->n++] = s;
    }
    closedir(dir);
    qsort(l->entries, (size_t)l->n, sizeof *l->entries, cmp_entry);
}

static void set_dir(Listing *l, const char *path)
{
    char resolved[4096];
    snprintf(l->dir, sizeof l->dir, "%.4095s", realpath(path, resolved) ? resolved : path);
    scan(l);
}

static const char *home_folder(void)
{
    const char *h = home_dir();
    return h && *h ? h : "/";
}

/* dir's root: "/", or on Windows its drive's, "C:/" */
static void root_of(const char *dir, char *out, size_t n)
{
#ifdef _WIN32
    if (dir[0] && dir[1] == ':') {
        snprintf(out, n, "%c:/", dir[0]);
        return;
    }
#else
    (void)dir;
#endif
    snprintf(out, n, "/");
}

/* the folder dir is in, into out; false when dir is a root */
static bool parent_of(const char *dir, char *out, size_t n)
{
    snprintf(out, n, "%s", dir);
    char *slash = strrchr(out, '/');
    if (!slash || !strcmp(out, "/"))
        return false;
#ifdef _WIN32
    if (out[1] == ':' && slash == out + 2) {
        if (!out[3])
            return false; /* C:/ */
        out[3] = '\0';
        return true;
    }
#endif
    if (slash == out)
        out[1] = '\0';
    else
        *slash = '\0';
    return true;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *title(void)
{
    switch (F.purpose) {
    case FILES_OPEN_WORLD: return "OPEN A WORLD";
    case FILES_SAVE_WORLD: return "SAVE THE WORLD";
    case FILES_IMPORT_PASTE: return "A BEING INTO THE WORLD";
    case FILES_EXPORT_GENES: return "EXPORT ITS GENES";
    case FILES_SAVE_IMAGE: return "SAVE ITS IMAGE";
    case FILES_IMPORT_LAB: return "A BEING INTO THE LABORATORY";
    case FILES_IMPORT_LIBRARY: return "A BEING INTO THE LIBRARY";
    case FILES_TELESCOPE_WORLD: return "A WORLD FOR THE TELESCOPE";
    default: return F.save ? "SAVE" : "OPEN";
    }
}

void files_open(App *a, int purpose)
{
    static const char *const ext[] = {WORLDFILE_EXT, WORLDFILE_EXT, "bgg", "bgg", "png", "bgg", "bgg", WORLDFILE_EXT};
    if (purpose < 0 || purpose >= FILES_PURPOSES)
        return;
    Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    if ((purpose == FILES_EXPORT_GENES && !o) || (purpose == FILES_SAVE_IMAGE && !(o && o->alive))) {
        interface_note(a, true, "Select an organism first");
        return;
    }
    F.purpose = purpose;
    F.save = purpose == FILES_SAVE_WORLD || purpose == FILES_EXPORT_GENES || purpose == FILES_SAVE_IMAGE;
    if (a->screen != SCREEN_FILES)
        F.back = a->screen == SCREEN_MENU ? SCREEN_WORLD : a->screen;
    snprintf(F.here.ext, sizeof F.here.ext, "%s", ext[purpose]);
    /* where it starts: where it was last, else the folder for such files */
    char dir[4096];
    snprintf(dir, sizeof dir, "%s", F.last_dir[purpose]);
    if (!is_dir(dir)) {
        if (purpose == FILES_OPEN_WORLD || purpose == FILES_SAVE_WORLD || purpose == FILES_TELESCOPE_WORLD) {
            app_worlds_dir(dir, sizeof dir);
        } else if (purpose == FILES_SAVE_IMAGE) {
            snprintf(dir, sizeof dir, "%s/Pictures", home_folder());
        } else {
            snprintf(dir, sizeof dir, "%s/beings", params_config_dir());
            if (!is_dir(dir))
                snprintf(dir, sizeof dir, "%s", syn_library_theirs());
        }
        if (!is_dir(dir))
            snprintf(dir, sizeof dir, "%s", home_folder());
    }
    set_dir(&F.here, dir);
    F.scroll = 0;
    F.name[0] = '\0';
    if (purpose == FILES_SAVE_WORLD)
        snprintf(F.name, sizeof F.name, "seed %lld time %lld", (long long)a->wo.seed,
                 (long long)world_time_units(&a->w));
    else if (purpose == FILES_EXPORT_GENES || purpose == FILES_SAVE_IMAGE)
        snprintf(F.name, sizeof F.name, "organism %d", o->id);
    if (F.save)
        shell_field_focus(&F.name, F.name);
    a->screen = SCREEN_FILES;
}

static void done(App *a)
{
    a->screen = F.back;
    listing_free(&F.parent);
    F.parent_of[0] = '\0';
}

/* what was chosen, done */
static void chosen(App *a, const char *path)
{
    char err[400];
    Organism *o = a->selected >= 0 ? app_find(&a->w, a->selected) : NULL;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    switch (F.purpose) {
    case FILES_OPEN_WORLD:
        if (app_load_world(a, path, err, sizeof err)) {
            app_do(a, ACT_HOME);
            interface_note(a, false, "%s: time %lld, %d alive", a->loaded, (long long)world_time_units(&a->w),
                           a->w.population);
            F.back = SCREEN_WORLD;
        } else {
            interface_note(a, true, "%s", err);
        }
        break;
    case FILES_SAVE_WORLD:
        if (worldfile_save(&a->w, path, err, sizeof err)) {
            syn_sidecar_save(&a->w, path);
            snprintf(a->world_path, sizeof a->world_path, "%s", path);
            snprintf(a->loaded, sizeof a->loaded, "%.120s", base);
            interface_note(a, false, "Saved: %s", base);
        } else {
            interface_note(a, true, "%s", err);
        }
        break;
    case FILES_IMPORT_PASTE: {
        GeneticCode *gc = bgg_load(path, err, sizeof err);
        if (!gc) {
            interface_note(a, true, "%s", err);
            break;
        }
        if (app_paste(a, gc, app_take_place(a))) {
            interface_note(a, false, "#%d: %s, into the world", a->selected, base);
            syn_history_user(&a->w, 'i', 0, "You imported a being");
        }
        gc_unref(gc);
        F.back = SCREEN_WORLD;
        break;
    }
    case FILES_IMPORT_LAB: {
        GeneticCode *gc = bgg_load(path, err, sizeof err);
        if (!gc) {
            interface_note(a, true, "%s", err);
            break;
        }
        lab_load_code(a, gc);
        gc_unref(gc);
        interface_note(a, false, "%s, in the laboratory", base);
        F.back = SCREEN_LAB;
        break;
    }
    case FILES_IMPORT_LIBRARY:
        library_imported(a, path);
        break;
    case FILES_TELESCOPE_WORLD:
        telescope_chosen(a, path);
        break;
    case FILES_EXPORT_GENES:
        if (o && bgg_save(path, o->gc, err, sizeof err))
            interface_note(a, false, "#%d's genes: %s", o->id, base);
        else
            interface_note(a, true, "%s", o ? err : "nothing selected");
        break;
    case FILES_SAVE_IMAGE:
        if (o && organism_image(a, o, 512, path, err, sizeof err))
            interface_note(a, false, "#%d's image: %s", o->id, base);
        else
            interface_note(a, true, "%s", o ? err : "nothing selected");
        break;
    }
    snprintf(F.last_dir[F.purpose], sizeof F.last_dir[0], "%s", F.here.dir);
    done(a);
}

static void accept(App *a)
{
    if (!F.name[0])
        return;
    char path[4400];
    if (path_absolute(F.name))
        snprintf(path, sizeof path, "%s", F.name);
    else
        snprintf(path, sizeof path, "%s%s%s", F.here.dir, F.here.dir[strlen(F.here.dir) - 1] == '/' ? "" : "/",
                 F.name);
    if (is_dir(path)) {
        set_dir(&F.here, path);
        F.name[0] = '\0';
        return;
    }
    struct stat st;
    if (F.save) {
        /* the extension added when it has none, as 0.8 did */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (!strchr(base, '.')) {
            size_t len = strlen(path);
            snprintf(path + len, sizeof path - len, ".%.*s", (int)strcspn(F.here.ext, "|"), F.here.ext);
        }
    } else if (stat(path, &st) != 0) {
        return;
    }
    chosen(a, path);
}

static void up_dir(void)
{
    char p[4096];
    if (parent_of(F.here.dir, p, sizeof p))
        set_dir(&F.here, p);
}

static void human_size(long long n, char *out, size_t cap)
{
    if (n < 1024)
        snprintf(out, cap, "%lld B", n);
    else if (n < 1024 * 1024)
        snprintf(out, cap, "%.1f KB", n / 1024.0);
    else
        snprintf(out, cap, "%.1f MB", n / (1024.0 * 1024.0));
}

static void entry_path(const Listing *l, const char *e, char *out, size_t cap)
{
    size_t len = strlen(e);
    if (len && e[len - 1] == '/')
        len--;
    snprintf(out, cap, "%s%s%.*s", l->dir, l->dir[strlen(l->dir) - 1] == '/' ? "" : "/", (int)len, e);
}

/* a list of entries in r: folders in the accent, the one picked filled;
 * the one clicked, or -1 */
static int column(Listing *l, UIRect r, int picked, float *scroll, bool details)
{
    const Fonts *fo = shell_fonts();
    const Theme *t = skin_theme();
    float rh = skin_text_height(fo->mono_small) + 12;
    int clicked = -1;
    float content = (float)l->n * rh, most = fmaxf(0, content - r.h);
    if (uiin_over(r))
        *scroll -= uiin_wheel() * rh * 3;
    if (picked >= 0) {
        float py = (float)picked * rh;
        if (py < *scroll)
            *scroll = py;
        if (py + rh > *scroll + r.h)
            *scroll = py + rh - r.h;
    }
    *scroll = fminf(fmaxf(*scroll, 0), most);
    gl2d_clip_push(r.x, r.y, r.w, r.h);
    for (int i = (int)(*scroll / rh); i < l->n; i++) {
        float y = r.y + (float)i * rh - *scroll;
        if (y > r.y + r.h)
            break;
        UIRect it = {r.x, y, r.w, rh};
        const char *e = l->entries[i];
        size_t len = strlen(e);
        bool dir = len && e[len - 1] == '/', hv = uiin_over(it) && uiin_over(r);
        Rgb fg = dir ? skin_tone(TONE_ACCENT) : t->text;
        if (i == picked) {
            skin_fill(it, t->accent, 1);
            fg = skin_tone(TONE_ON_ACCENT);
        } else if (hv) {
            skin_row_hover(it);
        }
        float cy = y + rh * 0.5f;
        skin_text_rgb(fo->icon, dir ? ICON_FOLDER : ICON_FILE, r.x + 14, cy - skin_text_height(fo->icon) * 0.5f,
                      fg.r, fg.g, fg.b, ALIGN_CENTER);
        char name[300], fit[300];
        snprintf(name, sizeof name, "%.*s", (int)(dir ? len - 1 : len), e);
        float nw = r.w - 34 - (details ? 170 : 8);
        skin_text_rgb(fo->mono_small, skin_fit(fo->mono_small, name, nw, fit, sizeof fit), r.x + 30,
                      cy - skin_text_height(fo->mono_small) * 0.5f, fg.r, fg.g, fg.b, ALIGN_LEFT);
        if (details && !dir) {
            char path[4400], sz[32], when[32];
            entry_path(l, e, path, sizeof path);
            struct stat st;
            if (stat(path, &st) == 0) {
                human_size((long long)st.st_size, sz, sizeof sz);
                struct tm *tm = localtime(&st.st_mtime);
                if (!tm || !strftime(when, sizeof when, "%d %b %Y", tm))
                    when[0] = '\0';
                Rgb dim = i == picked ? fg : skin_tone(TONE_MUTED);
                float ty = cy - skin_text_height(fo->mono_small) * 0.5f;
                skin_text_rgb(fo->mono_small, sz, r.x + r.w - 104, ty, dim.r, dim.g, dim.b, ALIGN_RIGHT);
                skin_text_rgb(fo->mono_small, when, r.x + r.w - 8, ty, dim.r, dim.g, dim.b, ALIGN_RIGHT);
            }
        }
        if (hv && uiin_clicked(it))
            clicked = i;
    }
    gl2d_clip_pop();
    if (!l->n)
        skin_text(fo->mono_small, "(nothing here)", r.x + 12, r.y + 8, TONE_MUTED, ALIGN_LEFT);
    return clicked;
}

void files_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    Listing *d = &F.here;
    UIRect all = {0, 0, (float)w, (float)h};
    uiin_solid(all);
    skin_fill(all, skin_theme()->bg, 0.55f);
    float bottom_room = a->hints ? SHELL_HINTS_H : 0;
    float pw = fminf(1120, (float)w - 32), ph = fminf(740, (float)h - SHELL_BAR_H - 26 - bottom_room);
    UIRect panel = {((float)w - pw) / 2, fmaxf(SHELL_BAR_H + 10, ((float)h - bottom_room - ph) / 2), pw, ph};
    skin_panel(panel, false);
    float tbh = skin_text_height(fo->mono_small_bold) + 10;
    UIRect tb = {panel.x + 1, panel.y + 1, panel.w - 2, tbh};
    Rgb tc = skin_titlebar(tb);
    skin_text_rgb(fo->mono_small_bold, title(), tb.x + 12, tb.y + 5, tc.r, tc.g, tc.b, ALIGN_LEFT);
    char filt[64] = "";
    for (const char *e = d->ext; *e;) {
        size_t n = strcspn(e, "|");
        snprintf(filt + strlen(filt), sizeof filt - strlen(filt), "%s*.%.*s", filt[0] ? "  " : "", (int)n, e);
        e += n + (e[n] == '|');
    }
    skin_text_rgb(fo->mono_small, filt, tb.x + tb.w - 12, tb.y + 5, tc.r, tc.g, tc.b, ALIGN_RIGHT);

    /* places, then the path a segment a button */
    float x = panel.x + 16, y = panel.y + tbh + 12, bh = 30;
    char root[8];
    root_of(d->dir, root, sizeof root);
    struct {
        const char *label, *path;
    } places[] = {{"HOME", home_folder()}, {root, root}};
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        float bw = skin_text_width(fo->small_bold, places[i].label) + 24;
        UIRect b = {x, y, bw, bh};
        bool hv = uiin_over(b);
        skin_button(b, fo->small_bold, places[i].label, (WidgetState){.hover = hv}, false);
        if (hv && uiin_clicked(b))
            set_dir(d, places[i].path);
        x += bw + 6;
    }
    x += 10;
    char segs[4096];
    snprintf(segs, sizeof segs, "%s", d->dir);
    char *parts[128];
    int np = 0;
    for (char *p = strtok(segs, "/"); p && np < 128; p = strtok(NULL, "/"))
        parts[np++] = p;
    /* from the right, as many segments as fit */
    int from = 0;
    float avail = panel.x + panel.w - 16 - x, need = 0;
    for (int i = np - 1; i >= 0; i--) {
        need += skin_text_width(fo->mono_small, parts[i]) + 22;
        if (need > avail) {
            from = i + 1;
            break;
        }
    }
    for (int i = from; i < np; i++) {
        float bw = skin_text_width(fo->mono_small, parts[i]) + 16;
        UIRect b = {x, y, bw, bh};
        bool last = i == np - 1, hv = !last && uiin_over(b);
        if (hv)
            skin_row_hover(b);
        skin_text(fo->mono_small, parts[i], b.x + 8, y + (bh - skin_text_height(fo->mono_small)) / 2,
                  last ? TONE_TEXT : hv ? TONE_ACCENT : TONE_MUTED, ALIGN_LEFT);
        if (hv && uiin_clicked(b)) {
            char p[4096] = "";
            bool drive = false; /* on Windows the first is the drive, C:, with no / before */
#ifdef _WIN32
            drive = parts[0][0] && parts[0][1] == ':' && !parts[0][2];
#endif
            for (int k = 0; k <= i; k++) {
                if (k || !drive)
                    strncat(p, "/", sizeof p - strlen(p) - 1);
                strncat(p, parts[k], sizeof p - strlen(p) - 1);
            }
            if (drive && i == 0)
                strncat(p, "/", sizeof p - strlen(p) - 1);
            set_dir(d, p);
            break;
        }
        x += bw;
        if (!last)
            skin_text(fo->mono_small, "/", x, y + (bh - skin_text_height(fo->mono_small)) / 2, TONE_MUTED,
                      ALIGN_LEFT);
        x += 6;
    }
    y += bh + 12;

    /* the columns: the folder above, this one, the preview */
    float bottom = panel.y + panel.h - 16 - 34 - 12 - skin_text_height(fo->mono_small) - 10;
    float colh = bottom - y, gap = 10;
    float w0 = (panel.w - 32 - 2 * gap) * 0.22f, w1 = (panel.w - 32 - 2 * gap) * 0.46f;
    float w2 = panel.w - 32 - 2 * gap - w0 - w1;
    UIRect c0 = {panel.x + 16, y, w0, colh}, c1 = {c0.x + w0 + gap, y, w1, colh},
           c2 = {c1.x + w1 + gap, y, w2, colh};
    skin_well(c0);
    skin_well(c1);
    skin_well(c2);

    char parent[4096];
    bool has_parent = parent_of(d->dir, parent, sizeof parent);
    if (strcmp(F.parent_of, d->dir)) {
        snprintf(F.parent_of, sizeof F.parent_of, "%s", d->dir);
        snprintf(F.parent.ext, sizeof F.parent.ext, "%s", d->ext);
        if (has_parent)
            set_dir(&F.parent, parent);
        else
            listing_free(&F.parent);
        F.scroll = 0;
    }
    /* the parent's pick: the folder this is */
    int ppick = -1;
    const char *here = strrchr(d->dir, '/');
    for (int i = 0; here && i < F.parent.n; i++)
        if (!strncmp(F.parent.entries[i], here + 1, strlen(here + 1)) &&
            F.parent.entries[i][strlen(here + 1)] == '/')
            ppick = i;
    int pc = column(&F.parent, c0, ppick, &F.pscroll, false);
    int cc = column(d, c1, d->selected, &F.scroll, true);

    /* the keys: up and down pick, Enter opens, Backspace goes up */
    bool typing = shell_field_active(&F.name);
    bool down = d->n && uiin_key(SDLK_DOWN), up = d->n && uiin_key(SDLK_UP);
    if (down)
        d->selected = d->selected + 1 < d->n ? d->selected + 1 : d->n - 1;
    if (up)
        d->selected = d->selected > 0 ? d->selected - 1 : 0;
    int open_idx = -1;
    if (cc >= 0) {
        /* a second click on it soon after opens it */
        if (cc == F.click_idx && SDL_GetTicks() - F.click_at < 450)
            open_idx = cc;
        F.click_idx = cc;
        F.click_at = SDL_GetTicks();
        d->selected = cc;
    }
    /* a file picked is the name */
    if ((cc >= 0 || down || up) && d->selected >= 0) {
        const char *e = d->entries[d->selected];
        if (e[strlen(e) - 1] != '/') {
            snprintf(F.name, sizeof F.name, "%s", e);
            if (typing)
                shell_field_focus(&F.name, F.name);
        }
    }
    if (!typing && uiin_key(SDLK_BACKSPACE) && has_parent)
        up_dir();
    if (!typing && (uiin_key(SDLK_RETURN) || uiin_key(SDLK_KP_ENTER)) && d->selected >= 0)
        open_idx = d->selected;

    /* the preview */
    float px = c2.x + 14, py = c2.y + 12;
    if (d->selected >= 0 && d->selected < d->n) {
        const char *e = d->entries[d->selected];
        size_t len = strlen(e);
        bool dir = e[len - 1] == '/';
        char path[4400];
        entry_path(d, e, path, sizeof path);
        char nm[300], fit[300];
        snprintf(nm, sizeof nm, "%.*s", (int)(dir ? len - 1 : len), e);
        skin_text(fo->bold, skin_fit(fo->bold, nm, c2.w - 28, fit, sizeof fit), px, py, TONE_TEXT, ALIGN_LEFT);
        py += skin_text_height(fo->bold) + 4;
        struct stat st;
        if (stat(path, &st) == 0) {
            char info[160], sz[32], when[48];
            human_size((long long)st.st_size, sz, sizeof sz);
            struct tm *tm = localtime(&st.st_mtime);
            if (!tm || !strftime(when, sizeof when, "%d %b %Y, %H:%M", tm))
                when[0] = '\0';
            const char *kind = dir                           ? "a folder"
                               : has_ext(e, WORLDFILE_EXT) ? "a world"
                               : has_ext(e, "bgg")         ? "a being's genetic code"
                               : has_ext(e, "png")         ? "an image"
                                                           : "a file";
            if (dir)
                snprintf(info, sizeof info, "%s, changed %s", kind, when);
            else
                snprintf(info, sizeof info, "%s, %s, changed %s", kind, sz, when);
            skin_text(fo->small, skin_fit(fo->small, info, c2.w - 28, fit, sizeof fit), px, py, TONE_MUTED,
                      ALIGN_LEFT);
            py += skin_text_height(fo->small) + 12;
        }
        /* a being: drawn, as the library draws it */
        if (!dir && has_ext(e, "bgg")) {
            if (strcmp(F.preview_path, path)) {
                snprintf(F.preview_path, sizeof F.preview_path, "%s", path);
                gc_unref(F.preview);
                F.preview = bgg_load(path, F.preview_err, sizeof F.preview_err);
            }
            float side = fminf(c2.w - 28, c2.y + c2.h - py - 12);
            if (F.preview && side > 40) {
                UIRect box = {px, py, side, side};
                skin_fill(box, (Rgb){0, 0, 0}, 1);
                screen_code(F.preview, box, a->seconds);
            } else if (!F.preview) {
                skin_text(fo->small, F.preview_err[0] ? F.preview_err : "not a genetic code this program reads",
                          px, py, TONE_BAD, ALIGN_LEFT);
            }
        }
    } else {
        skin_text(fo->small, F.save ? "Pick a folder, then name the file below." : "Pick a file.", px, py,
                  TONE_MUTED, ALIGN_LEFT);
    }

    /* the name, and the buttons */
    float ny = panel.y + panel.h - 16 - 34, bw = 120;
    UIRect okb = {panel.x + panel.w - 16 - bw, ny, bw, 34};
    UIRect cancel = {okb.x - 8 - bw, ny, bw, 34};
    skin_text(fo->small_bold, F.save ? "NAME" : "FILE", panel.x + 16, ny + 9, TONE_MUTED, ALIGN_LEFT);
    UIRect namebox = {panel.x + 70, ny, cancel.x - 16 - (panel.x + 70), 34};
    char typed[256];
    bool entered = shell_text_field(&F.name, namebox, fo->mono_small, F.name, typed, sizeof typed);
    if (entered) {
        snprintf(F.name, sizeof F.name, "%s", typed);
        if (uiin_key(SDLK_RETURN) || uiin_key(SDLK_KP_ENTER))
            accept(a);
    }
    bool okh = F.name[0] && uiin_over(okb);
    skin_button(okb, fo->bold, F.save ? "SAVE" : "OPEN",
                (WidgetState){.hover = okh, .active = F.name[0] != 0, .disabled = !F.name[0]}, false);
    if (okh && uiin_clicked(okb)) {
        accept(a);
        return;
    }
    bool ch = uiin_over(cancel);
    skin_button(cancel, fo->bold, "CANCEL", (WidgetState){.hover = ch}, false);
    if ((ch && uiin_clicked(cancel)) || (!typing && uiin_key(SDLK_ESCAPE))) {
        done(a);
        return;
    }
    /* SYN-OS's TUIs' hints */
    static const char *const hints[] = {"Up/Down", "pick", "Enter", "opens", "Backspace", "up a folder", "Esc",
                                        "cancels"};
    float hx = panel.x + 16, hy = ny - 12 - skin_text_height(fo->mono_small);
    for (int i = 0; i < 8; i += 2) {
        float kw = skin_keycap_width(fo->mono_small, hints[i]);
        skin_keycap(ui_rect(hx, hy - 1, kw, 19), fo->mono_small, hints[i]);
        hx += kw + 6;
        hx += skin_text(fo->mono_small, hints[i + 1], hx, hy, TONE_MUTED, ALIGN_LEFT) + 16;
    }

    if (pc >= 0) {
        const char *e = F.parent.entries[pc];
        size_t len = strlen(e);
        if (e[len - 1] == '/') {
            char p[4400];
            snprintf(p, sizeof p, "%s/%.*s", F.parent.dir, (int)(len - 1), e);
            set_dir(d, p);
        }
    }
    if (open_idx >= 0 && open_idx < d->n) {
        const char *e = d->entries[open_idx];
        size_t len = strlen(e);
        if (e[len - 1] == '/') {
            char p[4400];
            snprintf(p, sizeof p, "%s/%.*s", d->dir, (int)(len - 1), e);
            set_dir(d, p);
        } else {
            snprintf(F.name, sizeof F.name, "%s", e);
            accept(a);
        }
    }
}
