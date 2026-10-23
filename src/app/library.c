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

#include "app/library.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "compat.h"
#include "guide/kind.h"
#include "io/bgg.h"
#include "sim/world.h"

static void config_dir(char *out, size_t n, const char *app)
{
    snprintf(out, n, "%s/%s", config_home(), app);
}

static bool mkdirs(const char *path)
{
    char dir[1300];
    if (strlen(path) >= sizeof dir)
        return false;
    memcpy(dir, path, strlen(path) + 1);
    for (char *p = dir + 1;; p++) {
        if (*p == '/' || !*p) {
            char c = *p;
            *p = 0;
            if (mkdir(dir, 0755) != 0 && errno != EEXIST)
                return false;
            if (!c)
                break;
            *p = c;
        }
    }
    return true;
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const Being *)a)->name, ((const Being *)b)->name);
}

static void read_about(const char *bgg_path, char *origin, size_t n)
{
    char path[1100];
    snprintf(path, sizeof path, "%.*s.about", (int)(strlen(bgg_path) - 4), bgg_path);
    origin[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[300];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "origin=", 7))
            snprintf(origin, n, "%.199s", line + 7);
    }
    fclose(f);
}

static void add_folder(Library *L, const char *name, const char *dir, bool ours)
{
    if (L->nfolders == LIBRARY_FOLDERS)
        return;
    Folder *f = &L->folders[L->nfolders++];
    snprintf(f->name, sizeof f->name, "%s", name);
    snprintf(f->dir, sizeof f->dir, "%s", dir);
    f->ours = ours;
    f->first = L->nbeings;
    f->count = 0;
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".bgg"))
            continue;
        char path[1024], err[256];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        GeneticCode *gc = bgg_load(path, err, sizeof err);
        if (!gc)
            continue; /* Color Mod's, or broken: not this program's */
        Being *b = realloc(L->beings, (size_t)(L->nbeings + 1) * sizeof *b);
        if (!b) {
            gc_unref(gc);
            break;
        }
        L->beings = b;
        b = &L->beings[L->nbeings++];
        memset(b, 0, sizeof *b);
        snprintf(b->name, sizeof b->name, "%.*s", (int)(len - 4), e->d_name);
        snprintf(b->path, sizeof b->path, "%s", path);
        b->gc = gc;
        syn_kind_label(gc, b->kind, sizeof b->kind);
        read_about(path, b->origin, sizeof b->origin);
        f->count++;
    }
    closedir(d);
    qsort(L->beings + f->first, (size_t)f->count, sizeof *L->beings, by_name);
}

void library_scan(Library *L)
{
    library_free(L);
    char base[1024], dir[1200];
    config_dir(base, sizeof base, "syn-biogenesis-3d");
    snprintf(dir, sizeof dir, "%s/beings/Kept", base);
    add_folder(L, "Kept", dir, true);
    config_dir(base, sizeof base, "syn-biogenesis");
    static const char *const theirs[] = {"Saved", "Arena", "Imported", "Forged"};
    for (int i = 0; i < 4; i++) {
        snprintf(dir, sizeof dir, "%s/beings/0.9/%s", base, theirs[i]);
        add_folder(L, theirs[i], dir, false);
    }
}

void library_free(Library *L)
{
    for (int i = 0; i < L->nbeings; i++)
        gc_unref(L->beings[i].gc);
    free(L->beings);
    memset(L, 0, sizeof *L);
}

bool library_keep(const Organism *o, long long seed, char *path, size_t pathlen, char *err,
                  int errlen)
{
    char base[1024], dir[1200];
    config_dir(base, sizeof base, "syn-biogenesis-3d");
    snprintf(dir, sizeof dir, "%s/beings/Kept", base);
    if (!mkdirs(dir)) {
        snprintf(err, (size_t)errlen, "%s: cannot make the folder", dir);
        return false;
    }
    char kind[96];
    syn_kind_label(o->gc, kind, sizeof kind);
    char *dot = strstr(kind, " \xc2\xb7"); /* "Predator · mobile": the kind alone */
    if (dot)
        *dot = 0;
    snprintf(path, pathlen, "%s/%s #%d.bgg", dir, kind, o->id);
    if (!bgg_save(path, o->gc, err, errlen))
        return false;
    char about[1300];
    snprintf(about, sizeof about, "%.*s.about", (int)(strlen(path) - 4), path);
    FILE *f = fopen(about, "w");
    if (f) {
        time_t now = time(NULL);
        char when[32];
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
        fprintf(f, "origin=SYN-BIOGENESIS-3D: #%d, generation %d, seed %lld, time %d\n", o->id,
                o->generation, seed, (int)(o->world->frame / 256));
        fprintf(f, "saved=%s\n", when);
        fclose(f);
    }
    return true;
}
