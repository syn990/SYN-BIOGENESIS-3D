/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/library.c, the SYN edition of its C port
 * of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "syn/library.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "compat.h"
#include "io/bgg.h"
#include "sim/params.h"

/* the room for a path: the root, a folder and a name */
#define FULL (LIB_PATH + 1200)
/* a .bgg read whole: Biogenesis's are a few kilobytes */
#define MAX_FILE (4 << 20)

static char g_root[1024];
static bool g_root_set;
/* the library being read, when it is not this one (the _in functions) */
static const char *g_in;

void syn_library_set_root(const char *dir)
{
    g_root_set = dir != NULL;
    snprintf(g_root, sizeof g_root, "%s", dir ? dir : "");
}

const char *syn_library_root(void)
{
    if (!g_root_set)
        snprintf(g_root, sizeof g_root, "%s/beings", params_config_dir());
    return g_root;
}

const char *syn_library_theirs_of(int sim)
{
    static char dir[2][1024];
    snprintf(dir[sim != 0], sizeof dir[0], "%s/syn-biogenesis/beings/%s", config_home(), sim ? "colour" : "0.9");
    return dir[sim != 0];
}

const char *syn_library_theirs(void)
{
    return syn_library_theirs_of(0);
}

/* ---- names ---- */

void syn_library_clean_name(const char *in, char *out, size_t n)
{
    size_t k = 0;
    for (const unsigned char *c = (const unsigned char *)in; *c && k + 1 < n && k + 1 < LIB_NAME;
         c++) {
        unsigned char ch = *c;
        if (ch < 32 || strchr("/\\:*?\"<>|", ch))
            ch = '-';
        out[k++] = (char)ch;
    }
    out[k] = '\0';
    /* no leading dots or spaces (hidden files, ".."), no trailing ones
     * (which Windows drops) */
    size_t a = 0;
    while (out[a] == '.' || out[a] == ' ')
        a++;
    memmove(out, out + a, strlen(out + a) + 1);
    k = strlen(out);
    while (k > 0 && (out[k - 1] == '.' || out[k - 1] == ' '))
        out[--k] = '\0';
    if (!*out)
        snprintf(out, n, "BEING");
}

/* a folder path kept to clean names, without empty or ".." parts */
static void clean_folder(const char *in, char *out, size_t n)
{
    out[0] = '\0';
    size_t k = 0;
    const char *p = in ? in : "";
    while (*p) {
        const char *q = strchr(p, '/');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        char part[LIB_NAME], cleaned[LIB_NAME];
        snprintf(part, sizeof part, "%.*s", (int)(len < sizeof part ? len : sizeof part - 1), p);
        p += len + (q ? 1 : 0);
        bool dots = strspn(part, ".") == strlen(part);
        if (!*part || dots)
            continue;
        syn_library_clean_name(part, cleaned, sizeof cleaned);
        int m = snprintf(out + k, n - k, "%s%s", k ? "/" : "", cleaned);
        if (m < 0 || (size_t)m >= n - k)
            break;
        k += (size_t)m;
    }
}

static void dir_path(char *out, size_t n, const char *folder)
{
    char f[LIB_PATH];
    clean_folder(folder, f, sizeof f);
    snprintf(out, n, "%s%s%s", g_in ? g_in : syn_library_root(), *f ? "/" : "", f);
}

static void file_path(char *out, size_t n, const char *folder, const char *name, const char *ext)
{
    char d[FULL], nm[LIB_NAME];
    dir_path(d, sizeof d, folder);
    syn_library_clean_name(name, nm, sizeof nm);
    /* too long for the room: no path, which nothing opens */
    if (snprintf(out, n, "%s/%s%s", d, nm, ext) >= (int)n)
        out[0] = '\0';
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* every folder from the top down to this one */
static bool make_dirs(const char *path)
{
    char p[FULL];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++) {
        if (*c != '/')
            continue;
        *c = '\0';
        if (!is_dir(p) && mkdir(p, 0755) != 0 && errno != EEXIST)
            return false;
        *c = '/';
    }
    return is_dir(p) || mkdir(p, 0755) == 0 || errno == EEXIST;
}

/* a name not taken in the folder: the one asked for, else "NAME (2)" ... */
static bool free_name(const char *folder, const char *want, char *got, size_t n)
{
    char base[LIB_NAME];
    syn_library_clean_name(want, base, sizeof base);
    for (int k = 1; k < 10000; k++) {
        char nm[LIB_NAME + 16], path[FULL];
        if (k == 1)
            snprintf(nm, sizeof nm, "%s", base);
        else
            snprintf(nm, sizeof nm, "%.*s (%d)", LIB_NAME - 10, base, k);
        file_path(path, sizeof path, folder, nm, ".bgg");
        if (!exists(path))
            return snprintf(got, n, "%s", nm) < (int)n;
    }
    return false;
}

static bool in_bin(const char *folder)
{
    char f[LIB_PATH];
    clean_folder(folder, f, sizeof f);
    size_t b = strlen(LIB_BIN);
    return !strncmp(f, LIB_BIN, b) && (f[b] == '\0' || f[b] == '/');
}

/* ---- the .about beside each being ---- */

/* as much of src as fits */
static void copy(char *dst, size_t n, const char *src)
{
    size_t k = strlen(src);
    if (k >= n)
        k = n - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
}

static void about_write(const char *folder, const char *name, const char *origin, const char *saved)
{
    char path[FULL];
    file_path(path, sizeof path, folder, name, ".about");
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    char when[32];
    if (saved && *saved) {
        snprintf(when, sizeof when, "%s", saved);
    } else {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        if (!tm || !strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm))
            when[0] = '\0';
    }
    /* one line each, so no line ends inside */
    char o[200];
    snprintf(o, sizeof o, "%s", origin ? origin : "");
    for (char *c = o; *c; c++)
        if (*c == '\n' || *c == '\r')
            *c = ' ';
    fprintf(f, "origin=%s\nsaved=%s\n", o, when);
    fclose(f);
}

static void about_read(const char *folder, const char *name, LibEntry *e)
{
    e->origin[0] = e->saved[0] = '\0';
    char path[FULL];
    file_path(path, sizeof path, folder, name, ".about");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[400];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "origin=", 7))
            copy(e->origin, sizeof e->origin, line + 7);
        else if (!strncmp(line, "saved=", 6))
            copy(e->saved, sizeof e->saved, line + 6);
    }
    fclose(f);
}

/* ---- the folders ---- */

bool syn_library_ready(char *err, size_t n)
{
    const char *root = syn_library_root();
    char bin[FULL];
    dir_path(bin, sizeof bin, LIB_BIN);
    bool fresh = !is_dir(bin);
    if (!make_dirs(root)) {
        snprintf(err, n, "%s can't be made: %s", root, strerror(errno));
        return false;
    }
    if (fresh) {
        static const char *first[] = {"Saved", "Imported", "Arena", LIB_BIN};
        for (size_t i = 0; i < sizeof first / sizeof *first; i++) {
            char d[FULL];
            dir_path(d, sizeof d, first[i]);
            make_dirs(d);
        }
    }
    return true;
}

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* the names of what is in a folder: its folders, or its .bgg files
 * without the .bgg; malloc'd, sorted */
static int scan(const char *folder, bool folders, char ***out)
{
    *out = NULL;
    char d[FULL];
    dir_path(d, sizeof d, folder);
    DIR *dir = opendir(d);
    if (!dir)
        return 0;
    int n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dir))) {
        const char *nm = de->d_name;
        if (nm[0] == '.')
            continue;
        char full[FULL + 300];
        snprintf(full, sizeof full, "%s/%s", d, nm);
        size_t len = strlen(nm);
        bool want;
        if (folders)
            want = is_dir(full);
        else
            want = len > 4 && !strcasecmp(nm + len - 4, ".bgg") && !is_dir(full);
        if (!want)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            *out = realloc(*out, (size_t)cap * sizeof **out);
            if (!*out)
                abort();
        }
        (*out)[n] = strdup(nm);
        if (!(*out)[n])
            abort();
        if (!folders)
            (*out)[n][len - 4] = '\0';
        n++;
    }
    closedir(dir);
    if (n)
        qsort(*out, (size_t)n, sizeof **out, cmp_name);
    return n;
}

static void scan_free(char **v, int n)
{
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static void walk(const char *folder, int depth, char (*out)[LIB_PATH], int max, int *n)
{
    if (*n >= max)
        return;
    snprintf(out[(*n)++], LIB_PATH, "%s", folder);
    if (depth >= 8)
        return;
    char **sub;
    int k = scan(folder, true, &sub);
    for (int i = 0; i < k; i++) {
        char child[LIB_PATH];
        snprintf(child, sizeof child, "%s%s%s", folder, *folder ? "/" : "", sub[i]);
        walk(child, depth + 1, out, max, n);
    }
    scan_free(sub, k);
}

int syn_library_folders(char (*out)[LIB_PATH], int max)
{
    int n = 0;
    if (max > 0)
        walk("", 0, out, max, &n);
    return n;
}

int syn_library_list(const char *folder, LibEntry *out, int max)
{
    char **v;
    int k = scan(folder, false, &v), n = 0;
    for (int i = 0; i < k && n < max; i++) {
        snprintf(out[n].name, sizeof out[n].name, "%s", v[i]);
        about_read(folder, v[i], &out[n]);
        n++;
    }
    scan_free(v, k);
    return n;
}

int syn_library_count(const char *folder)
{
    char **v;
    int k = scan(folder, false, &v);
    scan_free(v, k);
    return k;
}

/* ---- beings ---- */

GeneticCode *syn_library_read(const char *folder, const char *name, char *err, size_t n)
{
    char path[FULL];
    file_path(path, sizeof path, folder, name, ".bgg");
    return bgg_load(path, err, (int)n);
}

bool syn_library_path_in(const char *root, const char *folder, const char *name, char *out, size_t n)
{
    g_in = root;
    file_path(out, n, folder, name, ".bgg");
    g_in = NULL;
    return out[0] != '\0';
}

int syn_library_folders_in(const char *root, char (*out)[LIB_PATH], int max)
{
    g_in = root;
    int n = syn_library_folders(out, max);
    g_in = NULL;
    return n;
}

int syn_library_list_in(const char *root, const char *folder, LibEntry *out, int max)
{
    g_in = root;
    int n = syn_library_list(folder, out, max);
    g_in = NULL;
    return n;
}

int syn_library_count_in(const char *root, const char *folder)
{
    g_in = root;
    int n = syn_library_count(folder);
    g_in = NULL;
    return n;
}

GeneticCode *syn_library_read_in(const char *root, const char *folder, const char *name, char *err,
                                 size_t n)
{
    g_in = root;
    GeneticCode *gc = syn_library_read(folder, name, err, n);
    g_in = NULL;
    return gc;
}

bool syn_library_put(const char *folder, const char *name, const GeneticCode *gc,
                     const char *origin, char *got, size_t gotn, char *err, size_t n)
{
    char d[FULL], nm[LIB_NAME], path[FULL];
    dir_path(d, sizeof d, folder);
    if (!make_dirs(d)) {
        snprintf(err, n, "%s can't be made: %s", d, strerror(errno));
        return false;
    }
    if (!free_name(folder, name, nm, sizeof nm)) {
        snprintf(err, n, "no free name for %s", name);
        return false;
    }
    file_path(path, sizeof path, folder, nm, ".bgg");
    char why[300];
    if (!bgg_save(path, gc, why, sizeof why)) {
        snprintf(err, n, "%s", why);
        return false;
    }
    about_write(folder, nm, origin, NULL);
    if (got)
        snprintf(got, gotn, "%s", nm);
    return true;
}

bool syn_library_import(const char *path, const char *folder, char *got, size_t gotn, char *err,
                        size_t n)
{
    /* read as this program reads any Biogenesis's being, and kept as one
     * of its own, with what was done to it in its .about */
    BggInfo info;
    GeneticCode *gc = bgg_load_info(path, &info, err, (int)n);
    if (!gc)
        return false;
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs && (!base || bs > base))
        base = bs;
#endif
    char stem[LIB_NAME];
    snprintf(stem, sizeof stem, "%s", base ? base + 1 : path);
    size_t sl = strlen(stem);
    if (sl > 4 && !strcasecmp(stem + sl - 4, ".bgg"))
        stem[sl - 4] = '\0';
    char origin[200];
    if (info.note[0])
        snprintf(origin, sizeof origin, "imported from %.50s (%.120s)", base ? base + 1 : path, info.note);
    else
        snprintf(origin, sizeof origin, "imported from %.180s", path);
    bool ok = syn_library_put(folder, stem, gc, origin, got, gotn, err, n);
    gc_unref(gc);
    return ok;
}

/* a being's two files from one folder and name to another */
static bool move_files(const char *folder, const char *name, const char *to, const char *to_name,
                       char *got, size_t gotn, char *err, size_t n)
{
    char from[FULL], d[FULL], nm[LIB_NAME], dest[FULL];
    file_path(from, sizeof from, folder, name, ".bgg");
    if (!exists(from)) {
        snprintf(err, n, "%s isn't there", name);
        return false;
    }
    dir_path(d, sizeof d, to);
    if (!make_dirs(d) || !free_name(to, to_name, nm, sizeof nm)) {
        snprintf(err, n, "%s can't take it", d);
        return false;
    }
    file_path(dest, sizeof dest, to, nm, ".bgg");
    if (rename(from, dest) != 0) {
        snprintf(err, n, "%s can't be moved: %s", name, strerror(errno));
        return false;
    }
    char afrom[FULL], adest[FULL];
    file_path(afrom, sizeof afrom, folder, name, ".about");
    file_path(adest, sizeof adest, to, nm, ".about");
    if (exists(afrom))
        rename(afrom, adest);
    if (got)
        snprintf(got, gotn, "%s", nm);
    return true;
}

bool syn_library_move(const char *folder, const char *name, const char *to, char *err, size_t n)
{
    return move_files(folder, name, to, name, NULL, 0, err, n);
}

bool syn_library_rename(const char *folder, const char *name, const char *to_name, char *got,
                        size_t gotn, char *err, size_t n)
{
    char a[LIB_NAME], b[LIB_NAME];
    syn_library_clean_name(name, a, sizeof a);
    syn_library_clean_name(to_name, b, sizeof b);
    if (!strcmp(a, b)) {
        if (got)
            snprintf(got, gotn, "%s", a);
        return true;
    }
    return move_files(folder, name, folder, to_name, got, gotn, err, n);
}

bool syn_library_delete(const char *folder, const char *name, char *err, size_t n)
{
    if (!in_bin(folder))
        return move_files(folder, name, LIB_BIN, name, NULL, 0, err, n);
    char path[FULL];
    file_path(path, sizeof path, folder, name, ".bgg");
    if (remove(path) != 0) {
        snprintf(err, n, "%s can't be removed: %s", name, strerror(errno));
        return false;
    }
    file_path(path, sizeof path, folder, name, ".about");
    remove(path);
    return true;
}

bool syn_library_new_folder(const char *parent, const char *name, char *got, size_t gotn,
                            char *err, size_t n)
{
    char p[LIB_PATH], nm[LIB_NAME];
    clean_folder(parent, p, sizeof p);
    syn_library_clean_name(name, nm, sizeof nm);
    for (int k = 1; k < 1000; k++) {
        char rel[LIB_PATH], d[FULL];
        int m = k == 1 ? snprintf(rel, sizeof rel, "%s%s%s", p, *p ? "/" : "", nm)
                       : snprintf(rel, sizeof rel, "%s%s%s (%d)", p, *p ? "/" : "", nm, k);
        if (m < 0 || m >= (int)sizeof rel)
            break;
        dir_path(d, sizeof d, rel);
        if (exists(d))
            continue;
        if (!make_dirs(d)) {
            snprintf(err, n, "%s can't be made: %s", d, strerror(errno));
            return false;
        }
        if (got)
            snprintf(got, gotn, "%s", rel);
        return true;
    }
    snprintf(err, n, "no free name for %s", nm);
    return false;
}

bool syn_library_remove_folder(const char *folder, char *err, size_t n)
{
    char f[LIB_PATH], d[FULL];
    clean_folder(folder, f, sizeof f);
    if (!*f || !strcmp(f, LIB_BIN)) {
        snprintf(err, n, "%s stays", *f ? "the Bin" : "the library");
        return false;
    }
    dir_path(d, sizeof d, f);
    DIR *dir = opendir(d);
    if (!dir) {
        snprintf(err, n, "%s isn't there", f);
        return false;
    }
    bool empty = true;
    struct dirent *de;
    while (empty && (de = readdir(dir)))
        empty = !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..");
    closedir(dir);
    if (!empty) {
        snprintf(err, n, "%s isn't empty", f);
        return false;
    }
    if (rmdir(d) != 0) {
        snprintf(err, n, "%s can't be removed: %s", f, strerror(errno));
        return false;
    }
    return true;
}

bool syn_library_rename_folder(const char *folder, const char *to_name, char *got, size_t gotn,
                               char *err, size_t n)
{
    char f[LIB_PATH], nm[LIB_NAME];
    clean_folder(folder, f, sizeof f);
    syn_library_clean_name(to_name, nm, sizeof nm);
    if (!*f || !strcmp(f, LIB_BIN)) {
        snprintf(err, n, "%s keeps its name", *f ? "the Bin" : "the library");
        return false;
    }
    const char *slash = strrchr(f, '/');
    char parent[LIB_PATH];
    snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - f) : 0, f);
    if (!strcmp(slash ? slash + 1 : f, nm)) {
        if (got)
            snprintf(got, gotn, "%s", f);
        return true;
    }
    for (int k = 1; k < 1000; k++) {
        char rel[LIB_PATH], from[FULL], to[FULL];
        int m = k == 1 ? snprintf(rel, sizeof rel, "%s%s%s", parent, *parent ? "/" : "", nm)
                       : snprintf(rel, sizeof rel, "%s%s%s (%d)", parent, *parent ? "/" : "", nm, k);
        if (m < 0 || m >= (int)sizeof rel)
            break;
        dir_path(to, sizeof to, rel);
        if (exists(to))
            continue;
        dir_path(from, sizeof from, f);
        if (rename(from, to) != 0) {
            snprintf(err, n, "%s can't be renamed: %s", f, strerror(errno));
            return false;
        }
        if (got)
            snprintf(got, gotn, "%s", rel);
        return true;
    }
    snprintf(err, n, "no free name for %s", nm);
    return false;
}

int syn_library_empty_bin(void)
{
    char **v;
    int k = scan(LIB_BIN, false, &v), gone = 0;
    char err[200];
    for (int i = 0; i < k; i++)
        gone += syn_library_delete(LIB_BIN, v[i], err, sizeof err);
    scan_free(v, k);
    return gone;
}
