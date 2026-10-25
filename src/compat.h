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

/* What Windows (MinGW-w64) lacks or does its own way, under the POSIX names
 * the rest of the source uses, as SYN-BIOGENESIS's src/compat.h does it.
 * Elsewhere it adds only the path helpers, which there change nothing.
 *
 * Paths are kept with '/' between their parts, which Windows takes too, and
 * the code finds a path's parts by '/'; a path Windows hands over goes
 * through path_slashes first. The program runs in the UTF-8 code page
 * (src/win/syn-biogenesis-3d.manifest), so these narrow names take UTF-8,
 * as SDL gives it. */
#ifndef COMPAT_H
#define COMPAT_H

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* '\' to '/' on Windows; elsewhere '\' is part of a name and stays */
static inline char *path_slashes(char *p)
{
#ifdef _WIN32
    for (char *c = p; *c; c++)
        if (*c == '\\')
            *c = '/';
#endif
    return p;
}

/* Where programs keep their settings: $XDG_CONFIG_HOME, or on Windows the
 * roaming AppData (as SYN-BIOGENESIS keeps its own there), or ~/.config.
 * SYN-BIOGENESIS's library is found under the same root. */
static inline const char *config_home(void)
{
    static char dir[512];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (!(xdg && *xdg) && appdata && *appdata)
        xdg = appdata;
#endif
    if (xdg && *xdg)
        snprintf(dir, sizeof dir, "%s", xdg);
    else
        snprintf(dir, sizeof dir, "%s/.config", home && *home ? home : ".");
    return path_slashes(dir);
}

/* from the root: "/x", and on Windows "C:/x" */
static inline bool path_absolute(const char *p)
{
#ifdef _WIN32
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        return true;
#endif
    return p[0] == '/';
}

/* the user's own folder: on Windows their profile (C:/Users/name), not the
 * $HOME a Unix-like shell there may set */
static inline const char *home_dir(void)
{
#ifdef _WIN32
    static char home[1024];
    const char *p = getenv("USERPROFILE");
    if (p && *p) {
        snprintf(home, sizeof home, "%s", p);
        return path_slashes(home);
    }
#endif
    return getenv("HOME");
}

#ifdef _WIN32
#include <direct.h>

/* "C:" and folders already there fail _mkdir in ways errno does not always
 * call EEXIST: anything that is a folder afterwards counts as made. */
static inline int compat_mkdir(const char *path)
{
    if (_mkdir(path) == 0)
        return 0;
    struct stat st;
    if (stat(path, &st) == 0 && (st.st_mode & S_IFDIR))
        errno = EEXIST;
    return -1;
}
#define mkdir(path, mode) compat_mkdir(path)

static inline char *realpath(const char *path, char *resolved)
{
    return _fullpath(resolved, path, 4096) ? path_slashes(resolved) : NULL;
}

static inline char *strsep(char **sp, const char *delim)
{
    char *s = *sp;
    if (!s)
        return NULL;
    char *end = s + strcspn(s, delim);
    *sp = *end ? end + 1 : NULL;
    *end = '\0';
    return s;
}
#endif

#endif
