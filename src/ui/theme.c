/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of the SYN-OS interface SYN-BIOGENESIS-3D shares with SYN-BIOGENESIS
 * and SYN-DEBT; taken from SYN-BIOGENESIS's src/synos, where it follows
 * SYN-DEBT's src/core/theme.c.
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

#include "ui/theme.h"

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void theme_fallback(Theme *t) {
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof t->name, "SYN-OS-RED");
    snprintf(t->family, sizeof t->family, "SYN-OS-VANILLA");
    snprintf(t->group, sizeof t->group, "vanilla");
    t->bg          = (Rgb){0x00, 0x00, 0x00};
    t->bg_alt      = (Rgb){0x10, 0x00, 0x00};
    t->panel       = (Rgb){0x2c, 0x01, 0x01};
    t->panel_hover = (Rgb){0x40, 0x01, 0x01};
    t->accent      = (Rgb){0x80, 0x00, 0x00};
    t->accent_dim  = (Rgb){0x26, 0x01, 0x01};
    t->text        = (Rgb){0xf8, 0xf8, 0xf2};
    t->border      = (Rgb){0x44, 0x44, 0x44};
    t->urgent      = (Rgb){0xff, 0x55, 0x55};
    t->style = STYLE_VANILLA;
}

static bool parse_hex(const char *s, Rgb *out) {
    unsigned r, g, b;
    if (s[0] != '#' || sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) != 3) return false;
    out->r = (unsigned char)r;
    out->g = (unsigned char)g;
    out->b = (unsigned char)b;
    return true;
}

/* KEY="value" or KEY=value; returns the value in buf, or NULL if the line isn't that key. */
static const char *value_of(const char *line, const char *key, char *buf, size_t len) {
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || line[n] != '=') return NULL;
    const char *v = line + n + 1;
    if (*v == '"') v++;
    size_t i = 0;
    while (v[i] && v[i] != '"' && v[i] != '\n' && v[i] != '\r' && i < len - 1) {
        buf[i] = v[i];
        i++;
    }
    buf[i] = '\0';
    return buf;
}

/* Copies what fits. Values are short in every real theme file. */
static void set_text(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static ThemeStyle style_for(const Theme *t) {
    if (strcmp(t->name, "SYN-OS-WIN95") == 0) return STYLE_WIN95;
    if (strcmp(t->name, "SYN-OS-MATRIX") == 0) return STYLE_MATRIX;
    if (strcmp(t->family, "SYN-OS-BEVEL") == 0) return STYLE_BEVEL;
    if (strcmp(t->family, "SYN-OS-FLATLINE") == 0) return STYLE_FLATLINE;
    if (strcmp(t->family, "SYN-OS-HALO") == 0) return STYLE_HALO;
    if (strcmp(t->family, "SYN-OS-SLAB") == 0) return STYLE_SLAB;
    return STYLE_VANILLA;
}

bool theme_parse(const char *text, Theme *t) {
    theme_fallback(t);
    t->name[0] = '\0';
    t->glyph[0] = '\0';

    static const struct { const char *key; size_t offset; } COLOURS[] = {
        /* Longer keys first so SYN_PANEL doesn't swallow SYN_PANEL_HOVER. */
        { "SYN_BG_ALT", offsetof(Theme, bg_alt) },
        { "SYN_BG", offsetof(Theme, bg) },
        { "SYN_PANEL_HOVER", offsetof(Theme, panel_hover) },
        { "SYN_PANEL", offsetof(Theme, panel) },
        { "SYN_ACCENT_DIM", offsetof(Theme, accent_dim) },
        { "SYN_ACCENT", offsetof(Theme, accent) },
        { "SYN_TEXT", offsetof(Theme, text) },
        { "SYN_BORDER", offsetof(Theme, border) },
        { "SYN_URGENT", offsetof(Theme, urgent) },
    };

    const char *p = text;
    while (*p) {
        char line[512];
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof line - 1) { line[n] = p[n]; n++; }
        line[n] = '\0';
        p += n;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        char v[256];
        bool done = false;
        for (size_t i = 0; i < sizeof COLOURS / sizeof COLOURS[0] && !done; i++) {
            if (value_of(line, COLOURS[i].key, v, sizeof v)) {
                parse_hex(v, (Rgb *)((char *)t + COLOURS[i].offset));
                done = true;
            }
        }
        if (done) continue;
        if (value_of(line, "SYN_THEME_NAME", v, sizeof v)) set_text(t->name, sizeof t->name, v);
        else if (value_of(line, "SYN_THEME_FAMILY", v, sizeof v)) set_text(t->family, sizeof t->family, v);
        else if (value_of(line, "SYN_THEME_GROUP", v, sizeof v)) set_text(t->group, sizeof t->group, v);
        else if (value_of(line, "SYN_THEME_MODE", v, sizeof v)) t->light = strcmp(v, "light") == 0;
        else if (value_of(line, "SYN_GLYPH", v, sizeof v)) set_text(t->glyph, sizeof t->glyph, v);
    }
    t->style = style_for(t);
    return t->name[0] != '\0';
}

static int group_rank(const char *g) {
    static const char *ORDER[] = { "vanilla", "bevel", "flatline", "halo", "slab" };
    for (int i = 0; i < 5; i++) if (strcmp(g, ORDER[i]) == 0) return i;
    return 5;
}

static int compare_themes(const void *a, const void *b) {
    const Theme *x = a, *y = b;
    int d = group_rank(x->group) - group_rank(y->group);
    if (d) return d;
    if (x->light != y->light) return x->light ? 1 : -1;
    return strcmp(x->name, y->name);
}

int theme_load_dir(const char *dir, ThemeList *out) {
    out->items = NULL;
    out->count = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 7 || strcmp(e->d_name + n - 6, ".theme") != 0) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        char text[4096];
        size_t got = fread(text, 1, sizeof text - 1, fp);
        fclose(fp);
        text[got] = '\0';

        Theme t;
        if (!theme_parse(text, &t)) continue;
        if (out->count == cap) {
            int ncap = cap ? cap * 2 : 64;
            Theme *items = realloc(out->items, sizeof(Theme) * (size_t)ncap);
            if (!items) break;
            out->items = items;
            cap = ncap;
        }
        out->items[out->count++] = t;
    }
    closedir(d);
    if (out->count > 1) qsort(out->items, (size_t)out->count, sizeof(Theme), compare_themes);
    return out->count;
}

void theme_list_free(ThemeList *l) {
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

int theme_find(const ThemeList *l, const char *name) {
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->items[i].name, name) == 0) return i;
    return -1;
}

bool theme_system_current(char *buf, int len) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    char path[1024];
    snprintf(path, sizeof path, "%s/.config/syn-os/current-theme", home);
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    bool ok = fgets(buf, len, fp) != NULL;
    fclose(fp);
    if (!ok) return false;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = '\0';
    return n > 0;
}

const char *theme_style_name(ThemeStyle s) {
    static const char *NAMES[STYLE_COUNT] = { "VANILLA", "BEVEL", "FLATLINE", "HALO", "SLAB", "WIN95", "MATRIX" };
    return (s >= 0 && s < STYLE_COUNT) ? NAMES[s] : NAMES[0];
}

const char *theme_short_name(const Theme *t) {
    return strncmp(t->name, "SYN-OS-", 7) == 0 ? t->name + 7 : t->name;
}
