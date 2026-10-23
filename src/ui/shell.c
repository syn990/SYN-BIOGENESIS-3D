/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * The bar, hints, notifications, menu, console and card follow
 * SYN-BIOGENESIS's src/syn/shell.c, redrawn here with this program's own
 * input (ui/input.c).
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

#include "ui/shell.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "compat.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/theme.h"

#define NOTES 6
#define LOG_LINES 600
#define LOG_LEN 240
#define HISTORY 48
#define MENU_ITEMS 32

static struct {
    Fonts fonts;
    ThemeList themes;
    int theme;

    struct {
        char text[200];
        bool urgent;
        Uint32 at;
    } note[NOTES];
    int nnote;

    bool console;
    bool console_used; /* opened at least once: only then does closing slide */
    Uint32 console_at;
    float console_h;
    char line[256];
    int lcur;
    char log[LOG_LINES][LOG_LEN];
    unsigned char kind[LOG_LINES];
    int nlog, head, scroll;
    char hist[HISTORY][256];
    int nhist, hpos;
    char entered[256];

    bool menu;
    float mx, my;
    char title[64];
    ShellCommand items[MENU_ITEMS];
    int nitems;
} SH;

static float text_w(TTF_Font *f, const char *s)
{
    return s && s[0] ? skin_text_width(f, s) : 0;
}

static float ink(TTF_Font *f, const char *s, float x, float y, Rgb c, Align al)
{
    return skin_text_rgb(f, s, x, y, c.r, c.g, c.b, al);
}

/* text centred on the line cy */
static float mid(TTF_Font *f, const char *s, float x, float cy, Rgb c, Align al)
{
    return ink(f, s, x, cy - skin_text_height(f) * 0.5f, c, al);
}

/* ---- fonts, themes, the saved choice ---- */

static const char *settings_path(void)
{
    static char path[1100];
    snprintf(path, sizeof path, "%s/syn-biogenesis-3d/settings", config_home());
    return path;
}

static void save_settings(void)
{
    const char *path = settings_path();
    if (!path || !SH.themes.count)
        return;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *p = dir + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(dir, 0755) != 0 && errno != EEXIST)
                return;
            *p = '/';
        }
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "theme=%s\n", SH.themes.items[SH.theme].name);
    fclose(f);
}

bool shell_init(const char *assets)
{
    if (TTF_Init() != 0) {
        fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError());
        return false;
    }
    char sans[1024], mono[1024], bold[1024], icons[1024], themes[1024];
    snprintf(sans, sizeof sans, "%s/fonts/NotoSans-Regular.ttf", assets);
    snprintf(mono, sizeof mono, "%s/fonts/SYNTerminal-Regular.ttf", assets);
    snprintf(bold, sizeof bold, "%s/fonts/SYNTerminal-Bold.ttf", assets);
    snprintf(icons, sizeof icons, "%s/fonts/fa-solid-900.otf", assets);
    snprintf(themes, sizeof themes, "%s/themes", assets);
    if (!skin_init(sans, &SH.fonts)) {
        fprintf(stderr, "syn-biogenesis-3d: no fonts in %s/fonts\n", assets);
        return false;
    }
    skin_init_desktop(mono, bold, icons, &SH.fonts);
    theme_load_dir(themes, &SH.themes);

    /* as SYN-DEBT: the saved choice, else the theme SYN-OS has on, else
     * SYN-OS-RED */
    char want[160] = "";
    const char *path = settings_path();
    FILE *f = path ? fopen(path, "r") : NULL;
    if (f) {
        char line[200];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "theme=", 6))
                snprintf(want, sizeof want, "%.150s", line + 6);
        }
        fclose(f);
    }
    if (!want[0] && !theme_system_current(want, (int)sizeof want))
        snprintf(want, sizeof want, "SYN-OS-RED");
    int i = theme_find(&SH.themes, want);
    if (i < 0)
        i = theme_find(&SH.themes, "SYN-OS-RED");
    SH.theme = i < 0 ? 0 : i;
    if (SH.themes.count)
        skin_set_theme(&SH.themes.items[SH.theme]);
    return true;
}

void shell_shutdown(void)
{
    skin_shutdown(&SH.fonts);
    theme_list_free(&SH.themes);
    TTF_Quit();
}

const Fonts *shell_fonts(void)
{
    return &SH.fonts;
}

const char *shell_theme_name(void)
{
    return skin_theme()->name;
}

bool shell_theme_set(const char *name)
{
    int i = theme_find(&SH.themes, name);
    if (i < 0) {
        /* the short name will do: HALO-CYAN for SYN-OS-HALO-CYAN */
        for (int k = 0; k < SH.themes.count && i < 0; k++)
            if (!strcasecmp(theme_short_name(&SH.themes.items[k]), name))
                i = k;
    }
    if (i < 0)
        return false;
    SH.theme = i;
    skin_set_theme(&SH.themes.items[i]);
    save_settings();
    return true;
}

void shell_theme_step(int by)
{
    if (!SH.themes.count)
        return;
    SH.theme = ((SH.theme + by) % SH.themes.count + SH.themes.count) % SH.themes.count;
    skin_set_theme(&SH.themes.items[SH.theme]);
    save_settings();
}

void shell_theme_list(void)
{
    char line[LOG_LEN] = "";
    for (int i = 0; i < SH.themes.count; i++) {
        const char *n = theme_short_name(&SH.themes.items[i]);
        if (strlen(line) + strlen(n) + 3 >= 100) {
            shell_print(SHELL_OUT, "%s", line);
            line[0] = 0;
        }
        strcat(line, n);
        strcat(line, i == SH.theme ? "*  " : "  ");
    }
    if (line[0])
        shell_print(SHELL_OUT, "%s", line);
}

/* ---- the frame ---- */

void shell_begin(int w, int h)
{
    gl2d_begin(w, h);
}

void shell_end(int w, int h)
{
    uiin_draw_tooltip(&SH.fonts, w, h);
    gl2d_end();
    skin_end_frame();
}

/* ---- the bar ---- */

int shell_bar(int w, const char *name, const ShellTab *tabs, int ntabs, const ShellModule *mods,
              int nmods, int menu_id)
{
    const Fonts *fo = &SH.fonts;
    int got = 0;
    UIRect bar = {0, 0, (float)w, SHELL_BAR_H};
    uiin_solid(bar);
    skin_bar(bar);
    const float pad = 6, bh = SHELL_BAR_H - 2 * pad, cy = SHELL_BAR_H * 0.5f - 1;
    const float iw = 18;

    float tab_full[16], tab_icon[16];
    for (int i = 0; i < ntabs && i < 16; i++) {
        tab_icon[i] = iw + 24;
        tab_full[i] = iw + 8 + text_w(fo->bold, tabs[i].label) + 26;
    }
    float mod_w[16];
    for (int i = 0; i < nmods && i < 16; i++)
        mod_w[i] = (mods[i].icon ? iw + (mods[i].text[0] ? 6 : 0) : 0) +
                   text_w(fo->mono_small_bold, mods[i].text) + 20;
    float menu_w = text_w(fo->bold, "MENU") + 36;
    float mark_w = 30 + text_w(fo->heading, name) + 14;
    /* as the window narrows: tabs become icons, figures go from the last,
     * then the name by the glyph */
    int level = 0, shown = nmods < 16 ? nmods : 16;
    bool show_name = true;
    for (;;) {
        float left = 10 + (show_name ? mark_w : 34) + 12;
        for (int i = 0; i < ntabs && i < 16; i++)
            left += (level == 0 || (level == 1 && tabs[i].active) ? tab_full[i] : tab_icon[i]) + 4;
        float right = 10 + menu_w + 8;
        for (int i = 0; i < shown; i++)
            right += mod_w[i] + 4;
        if (left + right <= (float)w)
            break;
        if (level == 0)
            level = 1;
        else if (shown > 4)
            shown--;
        else if (show_name)
            show_name = false;
        else if (level == 1)
            level = 2;
        else if (shown > 0)
            shown--;
        else
            break;
    }

    float x = 10;
    UIRect mark = {x, pad, show_name ? mark_w : 34, bh};
    skin_glyph(x + 15, cy + 1, 17, fo->icon);
    if (show_name)
        mid(fo->heading, name, x + 30, cy, skin_tone(TONE_ACCENT), ALIGN_LEFT);
    if (uiin_over(mark)) {
        uiin_tooltip("The console   ( ` )");
        if (uiin_clicked(mark))
            shell_console_toggle();
    }
    x += mark.w + 12;

    for (int i = 0; i < ntabs && i < 16; i++) {
        bool full = level == 0 || (level == 1 && tabs[i].active);
        UIRect r = {x, pad, full ? tab_full[i] : tab_icon[i], bh};
        bool h = !tabs[i].disabled && uiin_over(r);
        Tone t = skin_tab_chrome(r, (WidgetState){.hover = h, .active = tabs[i].active});
        Rgb c = skin_tone(tabs[i].disabled ? TONE_MUTED : t);
        float tx = r.x + 13;
        if (tabs[i].icon)
            mid(fo->icon, tabs[i].icon, tx + iw * 0.5f, cy, c, ALIGN_CENTER);
        if (full)
            mid(fo->bold, tabs[i].label, tx + iw + 8, cy, c, ALIGN_LEFT);
        if (h) {
            if (!full)
                uiin_tooltip(tabs[i].label);
            if (uiin_clicked(r))
                got = tabs[i].id;
        }
        x += r.w + 4;
    }

    float rx = (float)w - 10 - menu_w;
    UIRect mb = {rx, pad, menu_w, bh};
    bool hb = uiin_over(mb);
    Tone mt = skin_button_chrome(mb, (WidgetState){.hover = hb}, false);
    mid(fo->bold, "MENU", mb.x + mb.w * 0.5f, cy, skin_tone(mt), ALIGN_CENTER);
    if (hb) {
        uiin_tooltip("Every command");
        if (uiin_clicked(mb))
            got = menu_id;
    }
    rx -= 8;
    for (int i = 0; i < shown; i++) {
        UIRect r = {rx - mod_w[i], pad, mod_w[i], bh};
        bool h = uiin_over(r);
        Tone t = skin_module(r, (WidgetState){.hover = h && mods[i].id}, mods[i].level);
        float tx = r.x + 10;
        if (mods[i].icon) {
            Rgb ic = skin_tone(mods[i].level || t == TONE_ON_ACCENT ? t : TONE_ACCENT);
            mid(fo->icon, mods[i].icon, tx + iw * 0.5f, cy, ic, ALIGN_CENTER);
            tx += iw + 6;
        }
        mid(fo->mono_small_bold, mods[i].text, tx, cy, skin_tone(t), ALIGN_LEFT);
        if (h) {
            if (mods[i].tip)
                uiin_tooltip(mods[i].tip);
            if (mods[i].id && uiin_clicked(r))
                got = mods[i].id;
        }
        rx = r.x - 4;
    }
    return got;
}

/* ---- the hints ---- */

void shell_hints(int w, int h, const char *const *keys, int nkeys, const char *right)
{
    const Fonts *fo = &SH.fonts;
    const Theme *t = skin_theme();
    UIRect r = {0, (float)(h - SHELL_HINTS_H), (float)w, SHELL_HINTS_H};
    uiin_solid(r);
    if (t->style == STYLE_WIN95)
        skin_fill(r, t->panel, 1);
    else if (t->style != STYLE_FLATLINE)
        skin_fill(r, t->bg, 0.86f);
    skin_separator(0, r.y, (float)w);
    float cy = r.y + r.h * 0.5f, x = 12;
    float right_w = right ? text_w(fo->mono_small, right) + 24 : 0;
    for (int i = 0; i < nkeys; i++) {
        const char *key = keys[2 * i], *does = keys[2 * i + 1];
        float kw = skin_keycap_width(fo->mono_small, key);
        float vw = text_w(fo->mono_small, does);
        if (x + kw + 6 + vw > (float)w - right_w)
            break;
        skin_keycap(ui_rect(x, cy - 9, kw, 19), fo->mono_small, key);
        x += kw + 6;
        mid(fo->mono_small, does, x, cy, skin_tone(TONE_MUTED), ALIGN_LEFT);
        x += vw + 16;
    }
    if (right && x < (float)w - right_w)
        mid(fo->mono_small, right, (float)w - 12, cy, skin_tone(TONE_MUTED), ALIGN_RIGHT);
}

/* ---- notifications ---- */

void shell_notify(bool urgent, const char *fmt, ...)
{
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (!text[0])
        return;
    if (SH.nnote == NOTES) {
        memmove(SH.note, SH.note + 1, sizeof SH.note[0] * (NOTES - 1));
        SH.nnote--;
    }
    snprintf(SH.note[SH.nnote].text, sizeof SH.note[0].text, "%s", text);
    SH.note[SH.nnote].urgent = urgent;
    SH.note[SH.nnote].at = SDL_GetTicks();
    SH.nnote++;
    shell_print(urgent ? SHELL_ERROR : SHELL_NOTE, "%s", text);
}

void shell_notifications(int w, int top)
{
    const Fonts *fo = &SH.fonts;
    Uint32 now = SDL_GetTicks();
    /* mako's: five seconds, an urgent one until it is clicked away */
    for (int i = 0; i < SH.nnote;) {
        if (!SH.note[i].urgent && now - SH.note[i].at > 5000) {
            memmove(SH.note + i, SH.note + i + 1, sizeof SH.note[0] * (size_t)(SH.nnote - i - 1));
            SH.nnote--;
        } else {
            i++;
        }
    }
    float y = (float)top + 8 + (SH.console ? SH.console_h : 0);
    int shown = 0;
    for (int i = SH.nnote - 1; i >= 0 && shown < 3; i--, shown++) {
        const char *s = SH.note[i].text;
        float tw = fminf(text_w(fo->mono_small, s), (float)w * 0.5f);
        UIRect r = {(float)w - 8 - tw - 32, y, tw + 32, skin_text_height(fo->mono_small) + 26};
        Uint32 age = now - SH.note[i].at;
        float a = !SH.note[i].urgent && age > 4600 ? 1.0f - (float)(age - 4600) / 400.0f : 1.0f;
        if (age < 120)
            a *= (float)age / 120.0f;
        uiin_solid(r);
        skin_set_alpha(a);
        skin_note(r, SH.note[i].urgent);
        char fit[200];
        ink(fo->mono_small, skin_fit(fo->mono_small, s, tw, fit, sizeof fit), r.x + 16, r.y + 13,
            skin_tone(TONE_TEXT), ALIGN_LEFT);
        skin_set_alpha(1);
        if (uiin_clicked(r)) {
            memmove(SH.note + i, SH.note + i + 1, sizeof SH.note[0] * (size_t)(SH.nnote - i - 1));
            SH.nnote--;
            return;
        }
        y += r.h + 8;
    }
}

/* ---- the card ---- */

int shell_card(int w, int h, const ShellCard *c)
{
    const Fonts *fo = &SH.fonts;
    float cw = (float)(w - 32 < 760 ? w - 32 : 760), ch = 236;
    UIRect r = {16, (float)(h - SHELL_HINTS_H - 12) - ch, cw, ch};
    int got = 0;
    uiin_solid(r);
    skin_panel(r, true);
    /* the title bar, as labwc draws a window's */
    float th = 28;
    UIRect tb = {r.x + 1, r.y + 1, r.w - 2, th};
    Rgb tc = skin_titlebar(tb);
    float tx = tb.x + 12, cy = tb.y + th * 0.5f;
    tx += mid(fo->mono_bold, c->title, tx, cy, tc, ALIGN_LEFT) + 12;
    if (c->state) {
        Rgb sc = skin_tone(c->state_tone);
        const Theme *t = skin_theme();
        if (t->style == STYLE_SLAB || t->style == STYLE_BEVEL || t->style == STYLE_WIN95)
            sc = tc;
        mid(fo->mono_small_bold, c->state, tx, cy, sc, ALIGN_LEFT);
    }
    if (c->subtitle[0]) {
        char fit[96];
        mid(fo->mono_small, skin_fit(fo->mono_small, c->subtitle, r.w * 0.45f, fit, sizeof fit),
            tb.x + tb.w - 12, cy, tc, ALIGN_RIGHT);
    }

    /* its body, and the figures beside it in their wells */
    float top = r.y + th + 10;
    UIRect body = {r.x + 10, top, 104, 104};
    skin_well(body);
    if (c->draw_body)
        c->draw_body(c->ud, body);
    float gx = r.x + 10 + 104 + 12, gw = r.x + r.w - 10 - gx;
    int cols = 4;
    float cwid = (gw - (float)(cols - 1) * 6) / (float)cols, chh = 24;
    for (int i = 0; i < c->ncells; i++) {
        UIRect cell = {gx + (float)(i % cols) * (cwid + 6), top + (float)(i / cols) * (chh + 6),
                       cwid, chh};
        skin_well(cell);
        mid(fo->small, c->cells[i].label, cell.x + 7, cell.y + chh * 0.5f, skin_tone(TONE_MUTED),
            ALIGN_LEFT);
        mid(fo->mono_small_bold, c->cells[i].value, cell.x + cell.w - 7, cell.y + chh * 0.5f,
            skin_tone(c->cells[i].tone), ALIGN_RIGHT);
    }
    /* the bars: SYN-TREK's segments, the missing ones faint */
    float by = top + (float)((c->ncells + cols - 1) / cols) * (chh + 6) + 4;
    for (int b = 0; b < 2; b++) {
        if (!c->bar_label[b])
            continue;
        float bx = gx + (float)b * (gw / 2 + 3), bw = gw / 2 - 3;
        mid(fo->small, c->bar_label[b], bx, by + 7, skin_tone(TONE_MUTED), ALIGN_LEFT);
        float lw = text_w(fo->small, c->bar_label[b]) + 8;
        int cells = 14;
        float sw = (bw - lw - (float)(cells - 1) * 2) / (float)cells;
        double f = c->bar[b] < 0 ? 0 : c->bar[b] > 1 ? 1 : c->bar[b];
        int lit = (int)lround(f * cells);
        Rgb on = skin_tone(c->bar_tone[b]);
        for (int i = 0; i < cells; i++)
            skin_fill(ui_rect(bx + lw + (float)i * (sw + 2), by + 1, sw, 12), on,
                      i < lit ? 1.0f : 0.16f);
    }
    /* what kind of being it is: the kind, then its sentence, two lines at
     * most (SYN-BIOGENESIS's wrap) */
    if (c->kind[0] || c->about[0]) {
        float lh = skin_text_height(fo->small) + 4, ky = by + 22, kx = r.x + 12, right = r.x + r.w - 12;
        float x = kx;
        if (c->kind[0])
            x += ink(fo->small_bold, c->kind, kx, ky, skin_tone(TONE_ACCENT), ALIGN_LEFT) + 10;
        const char *p = c->about;
        char line[400];
        for (int ln = 0; ln < 2 && *p; ln++) {
            size_t n = 0;
            const char *q = p;
            while (*q) {
                const char *wd = q + strspn(q, " "), *we = wd + strcspn(wd, " ");
                char cand[400];
                snprintf(cand, sizeof cand, "%.*s%s%.*s", (int)n, line, n ? " " : "",
                         (int)(we - wd), wd);
                if (n && text_w(fo->small, cand) > right - x)
                    break;
                n = strlen(cand) < sizeof line - 1 ? strlen(cand) : sizeof line - 1;
                memcpy(line, cand, n);
                q = we;
            }
            line[n] = '\0';
            if (ln == 1 && *q) {
                char fit[sizeof line + 8];
                snprintf(fit, sizeof fit, "%s ...", line);
                snprintf(line, sizeof line, "%.395s", fit);
            }
            ink(fo->small, line, x, ky + (float)ln * lh, skin_tone(TONE_MUTED), ALIGN_LEFT);
            p = q + strspn(q, " ");
            x = kx;
        }
        UIRect kr = {kx, ky - 2, right - kx, lh * 2};
        if (uiin_over(kr) && c->about[0])
            uiin_tooltip(c->about);
    }
    /* what can be done to it, along the bottom */
    float ay = r.y + r.h - 10 - 30, ax = r.x + 10;
    for (int i = 0; i < c->nactions; i++) {
        const ShellCommand *a = &c->actions[i];
        float bw = 40 + text_w(fo->small_bold, a->label);
        if (ax + bw > r.x + r.w - 10)
            break;
        UIRect b = {ax, ay, bw, 30};
        bool hv = !a->disabled && uiin_over(b);
        Tone tone = skin_button_chrome(
            b, (WidgetState){.hover = hv, .active = a->on, .disabled = a->disabled}, true);
        Rgb fg = skin_tone(a->disabled ? TONE_MUTED : tone);
        float bcy = b.y + 14;
        if (a->icon)
            mid(fo->icon, a->icon, b.x + 16, bcy, fg, ALIGN_CENTER);
        mid(fo->small_bold, a->label, b.x + 30, bcy, fg, ALIGN_LEFT);
        if (hv) {
            if (a->tip)
                uiin_tooltip(a->tip);
            if (uiin_clicked(b))
                got = a->id;
        }
        ax += bw + 6;
    }
    return got;
}

/* ---- the menu (labwc's) ---- */

void shell_menu_open(float x, float y, const char *title, const ShellCommand *items, int n)
{
    SH.menu = true;
    SH.mx = x;
    SH.my = y;
    snprintf(SH.title, sizeof SH.title, "%s", title ? title : "");
    SH.nitems = n < MENU_ITEMS ? n : MENU_ITEMS;
    memcpy(SH.items, items, sizeof items[0] * (size_t)SH.nitems);
}

bool shell_menu_showing(void)
{
    return SH.menu;
}

int shell_menu(int w, int h)
{
    if (!SH.menu)
        return 0;
    const Fonts *fo = &SH.fonts;
    const Theme *t = skin_theme();
    TTF_Font *f = fo->mono_small;
    float row = skin_text_height(f) + 12, head = skin_text_height(fo->mono_small_bold) + 12;
    float mw = text_w(fo->mono_small_bold, SH.title) + 36;
    float mh = SH.title[0] ? head + 4 : 4;
    for (int i = 0; i < SH.nitems; i++) {
        const ShellCommand *c = &SH.items[i];
        float iw = 42 + text_w(f, c->label) + (c->key ? skin_keycap_width(f, c->key) + 30 : 16);
        mw = fmaxf(mw, iw);
        mh += !strcmp(c->label, "-") ? 9 : c->id == 0 ? head : row;
    }
    mh += 4;
    mw = fmaxf(mw, 220);
    float x = SH.mx, y = SH.my;
    if (x + mw > (float)w - 4)
        x = (float)w - 4 - mw;
    if (y + mh > (float)h - 4)
        y = fmaxf(4, (float)h - 4 - mh);
    UIRect m = {x, y, mw, mh};
    UIRect all = {0, 0, (float)w, (float)h};
    uiin_solid(all); /* while it is open, the whole window is the menu's */
    int got = 0;
    skin_panel(m, false);
    float cy = y + 2;
    if (SH.title[0]) {
        UIRect tb = {x + 2, cy, mw - 4, head};
        Rgb tc = skin_titlebar(tb);
        mid(fo->mono_small_bold, SH.title, tb.x + 14, tb.y + head * 0.5f, tc, ALIGN_LEFT);
        cy += head + 2;
    }
    for (int i = 0; i < SH.nitems; i++) {
        const ShellCommand *c = &SH.items[i];
        if (!strcmp(c->label, "-")) {
            skin_separator(x + 8, cy + 4, mw - 16);
            cy += 9;
            continue;
        }
        if (c->id == 0) {
            mid(fo->mono_small_bold, c->label, x + 14, cy + head * 0.5f, skin_tone(TONE_ACCENT),
                ALIGN_LEFT);
            cy += head;
            continue;
        }
        UIRect r = {x + 2, cy, mw - 4, row};
        bool hv = !c->disabled && uiin_over(r);
        Rgb fg = c->disabled ? skin_tone(TONE_MUTED) : t->text;
        if (hv) {
            skin_fill(r, t->accent, 1);
            fg = skin_tone(TONE_ON_ACCENT);
        }
        float my = cy + row * 0.5f;
        if (c->icon)
            mid(fo->icon, c->icon, r.x + 20, my, hv ? fg : skin_tone(TONE_MUTED), ALIGN_CENTER);
        mid(f, c->label, r.x + 40, my, fg, ALIGN_LEFT);
        if (c->key) {
            float kw = skin_keycap_width(f, c->key);
            skin_keycap(ui_rect(r.x + r.w - 10 - kw, my - 9, kw, 19), f, c->key);
        }
        if (hv && uiin_clicked(r))
            got = c->id;
        cy += row;
    }
    if (got || uiin_key(SDLK_ESCAPE) ||
        ((uiin_clicked(all) || uiin_right_clicked(all)) && !uiin_over(m)))
        SH.menu = false;
    return got;
}

/* ---- the console (a drop-down foot) ---- */

void shell_console_toggle(void)
{
    SH.console = !SH.console;
    SH.console_used = true;
    SH.console_at = SDL_GetTicks();
    SH.scroll = 0;
    uiin_set_typing(SH.console);
}

bool shell_console_showing(void)
{
    return SH.console;
}

static void log_line(ShellKind kind, const char *s)
{
    int at;
    if (SH.nlog < LOG_LINES) {
        at = (SH.head + SH.nlog) % LOG_LINES;
        SH.nlog++;
    } else {
        at = SH.head;
        SH.head = (SH.head + 1) % LOG_LINES;
    }
    size_t n = strlen(s);
    if (n > LOG_LEN - 1)
        n = LOG_LEN - 1;
    memcpy(SH.log[at], s, n);
    SH.log[at][n] = '\0';
    SH.kind[at] = (unsigned char)kind;
}

void shell_print(ShellKind kind, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    char *p = buf;
    for (;;) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = '\0';
        log_line(kind, p);
        if (!nl)
            break;
        p = nl + 1;
    }
}

int shell_split(char *line, char **argv, int max)
{
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            argv[n++] = ++p;
            while (*p && *p != '"')
                p++;
        } else {
            argv[n++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
        }
        if (*p)
            *p++ = '\0';
    }
    return n;
}

static Rgb kind_colour(int kind)
{
    switch (kind) {
    case SHELL_ECHO:
    case SHELL_HEAD:
        return skin_tone(TONE_ACCENT);
    case SHELL_ERROR:
        return skin_tone(TONE_BAD);
    case SHELL_NOTE:
        return skin_tone(TONE_MUTED);
    case SHELL_GOOD:
        return skin_tone(TONE_GOOD);
    default:
        return skin_theme()->text;
    }
}

/* the words starting with what is typed: one is put in, several listed */
static void complete(const char *const *words, int nwords)
{
    if (strchr(SH.line, ' '))
        return;
    const char *only = NULL;
    int count = 0;
    size_t n = strlen(SH.line), common = 0;
    for (int i = 0; i < nwords; i++) {
        if (strncasecmp(words[i], SH.line, n))
            continue;
        if (!only) {
            only = words[i];
            common = strlen(words[i]);
        } else {
            size_t k = 0;
            while (k < common &&
                   tolower((unsigned char)only[k]) == tolower((unsigned char)words[i][k]))
                k++;
            common = k;
        }
        count++;
    }
    if (count == 1) {
        snprintf(SH.line, sizeof SH.line, "%s ", only);
    } else if (count > 1) {
        if (common > n) {
            snprintf(SH.line, sizeof SH.line, "%.*s", (int)common, only);
        } else {
            char list[LOG_LEN] = "";
            for (int i = 0; i < nwords; i++)
                if (!strncasecmp(words[i], SH.line, n) && strlen(list) + strlen(words[i]) + 3 < sizeof list) {
                    strcat(list, words[i]);
                    strcat(list, "  ");
                }
            shell_print(SHELL_NOTE, "%s", list);
        }
    }
    SH.lcur = (int)strlen(SH.line);
}

/* The keys of a line being typed: SDL's text in at the cursor, and the
 * editing keys. */
static void edit_line(void)
{
    const char *typed = uiin_typed();
    for (const char *p = typed; *p; p++) {
        if (*p == '`')
            continue; /* the key that closes the console */
        size_t len = strlen(SH.line);
        if (len + 1 >= sizeof SH.line)
            break;
        memmove(SH.line + SH.lcur + 1, SH.line + SH.lcur, len - (size_t)SH.lcur + 1);
        SH.line[SH.lcur++] = *p;
    }
    size_t len = strlen(SH.line);
    if (uiin_key(SDLK_BACKSPACE) && SH.lcur > 0) {
        int from = SH.lcur - 1;
        while (from > 0 && ((unsigned char)SH.line[from] & 0xC0) == 0x80)
            from--;
        memmove(SH.line + from, SH.line + SH.lcur, len - (size_t)SH.lcur + 1);
        SH.lcur = from;
    }
    if (uiin_key(SDLK_DELETE) && (size_t)SH.lcur < len) {
        int to = SH.lcur + 1;
        while ((size_t)to < len && ((unsigned char)SH.line[to] & 0xC0) == 0x80)
            to++;
        memmove(SH.line + SH.lcur, SH.line + to, len - (size_t)to + 1);
    }
    if (uiin_key(SDLK_LEFT) && SH.lcur > 0) {
        SH.lcur--;
        while (SH.lcur > 0 && ((unsigned char)SH.line[SH.lcur] & 0xC0) == 0x80)
            SH.lcur--;
    }
    if (uiin_key(SDLK_RIGHT) && (size_t)SH.lcur < strlen(SH.line)) {
        SH.lcur++;
        while ((size_t)SH.lcur < strlen(SH.line) &&
               ((unsigned char)SH.line[SH.lcur] & 0xC0) == 0x80)
            SH.lcur++;
    }
    if (uiin_key(SDLK_HOME))
        SH.lcur = 0;
    if (uiin_key(SDLK_END))
        SH.lcur = (int)strlen(SH.line);
}

const char *shell_console(int w, int top, int h, const char *const *words, int nwords)
{
    const Fonts *fo = &SH.fonts;
    const Theme *t = skin_theme();
    Uint32 since = SDL_GetTicks() - SH.console_at;
    float slide = since >= 160 ? 1.0f : (float)since / 160.0f;
    if (!SH.console) {
        if (since >= 160 || !SH.console_used) {
            SH.console_h = 0;
            return NULL;
        }
        slide = 1.0f - slide;
    }
    float full = fmaxf(240, (float)(h - top) * 0.46f);
    float ch = full * (1.0f - (1.0f - slide) * (1.0f - slide));
    SH.console_h = ch;
    UIRect r = {0, (float)top, (float)w, ch};
    uiin_solid(r);
    skin_fill(r, t->bg, 0.9f);
    skin_fill(ui_rect(0, r.y + r.h - 2, (float)w, 2), t->accent, 1);
    const char *ret = NULL;
    TTF_Font *f = fo->mono;
    float lh = skin_text_height(f) + 1, cw = text_w(f, "M");
    if (cw <= 0)
        cw = 8;
    int cols = (int)(((float)w - 16) / cw);
    if (cols < 20)
        cols = 20;

    if (SH.console) {
        int page = (int)(ch / lh) - 3;
        if (uiin_key(SDLK_BACKQUOTE) || uiin_key(SDLK_ESCAPE)) {
            shell_console_toggle();
        } else if (uiin_key(SDLK_RETURN) || uiin_key(SDLK_KP_ENTER)) {
            snprintf(SH.entered, sizeof SH.entered, "%s", SH.line);
            shell_print(SHELL_ECHO, ": %s", SH.line);
            if (SH.line[0] && (!SH.nhist || strcmp(SH.hist[SH.nhist - 1], SH.line))) {
                if (SH.nhist == HISTORY) {
                    memmove(SH.hist, SH.hist + 1, sizeof SH.hist[0] * (HISTORY - 1));
                    SH.nhist--;
                }
                snprintf(SH.hist[SH.nhist++], sizeof SH.hist[0], "%s", SH.line);
            }
            SH.hpos = SH.nhist;
            SH.line[0] = '\0';
            SH.lcur = 0;
            SH.scroll = 0;
            ret = SH.entered;
        } else if (uiin_key(SDLK_TAB)) {
            complete(words, nwords);
        } else if (uiin_key(SDLK_UP) && SH.hpos > 0) {
            snprintf(SH.line, sizeof SH.line, "%s", SH.hist[--SH.hpos]);
            SH.lcur = (int)strlen(SH.line);
        } else if (uiin_key(SDLK_DOWN) && SH.hpos < SH.nhist) {
            SH.hpos++;
            snprintf(SH.line, sizeof SH.line, "%s", SH.hpos < SH.nhist ? SH.hist[SH.hpos] : "");
            SH.lcur = (int)strlen(SH.line);
        } else if (uiin_key(SDLK_PAGEUP)) {
            SH.scroll += page;
        } else if (uiin_key(SDLK_PAGEDOWN)) {
            SH.scroll = SH.scroll > page ? SH.scroll - page : 0;
        } else if (uiin_key(SDLK_l) && (uiin_mods() & KMOD_CTRL)) {
            SH.nlog = SH.head = SH.scroll = 0;
        } else {
            edit_line();
        }
    }

    /* the line being typed at the bottom, the log above it, newest last */
    float y = r.y + r.h - 8 - lh;
    float px = 8;
    px += ink(f, ":", px, y, skin_tone(TONE_ACCENT), ALIGN_LEFT) + cw;
    ink(f, SH.line, px, y, t->text, ALIGN_LEFT);
    char before[256];
    snprintf(before, sizeof before, "%.*s", SH.lcur, SH.line);
    if (SH.console && (SDL_GetTicks() / 530) % 2 == 0)
        skin_fill(ui_rect(px + text_w(f, before), y, cw, lh - 1), skin_tone(TONE_ACCENT), 0.8f);
    float tbh = skin_text_height(fo->mono_small_bold) + 8;
    UIRect tb = {0, r.y, (float)w, tbh};
    if (r.h > tbh) {
        Rgb tc = skin_titlebar(tb);
        float tcy = tb.y + tbh * 0.5f;
        mid(fo->mono_small_bold, "CONSOLE", 10, tcy, tc, ALIGN_LEFT);
        static const char *const keys[] = {"help", "lists", "PgUp", "scrolls", "Up", "history",
                                           "Tab", "completes", "`", "closes"};
        float kx = (float)w - 10;
        for (int i = 8; i >= 0; i -= 2) {
            kx -= mid(fo->mono_small, keys[i + 1], kx, tcy, tc, ALIGN_RIGHT) + 6;
            float kw = skin_keycap_width(fo->mono_small, keys[i]);
            skin_keycap(ui_rect(kx - kw, tcy - 9, kw, 18), fo->mono_small, keys[i]);
            kx -= kw + 16;
            if (kx < 140)
                break;
        }
    }
    y -= lh + 4;
    int skip = SH.scroll;
    int rows_fit = (int)((y - r.y - tbh - 4) / lh) + 1;
    int drawn = 0;
    for (int i = SH.nlog - 1; i >= 0 && drawn < rows_fit; i--) {
        int at = (SH.head + i) % LOG_LINES;
        const char *s = SH.log[at];
        int len = (int)strlen(s);
        int starts[64], ns = 0, count = 0;
        starts[ns++] = 0;
        for (int b = 0; b < len; b++) {
            if (((unsigned char)s[b] & 0xC0) == 0x80)
                continue;
            if (count == cols && ns < 64) {
                starts[ns++] = b;
                count = 0;
            }
            count++;
        }
        for (int row = ns - 1; row >= 0 && drawn < rows_fit; row--) {
            if (skip > 0) {
                skip--;
                continue;
            }
            int end = row + 1 < ns ? starts[row + 1] : len;
            char part[LOG_LEN];
            snprintf(part, sizeof part, "%.*s", end - starts[row], s + starts[row]);
            TTF_Font *pf = SH.kind[at] == SHELL_HEAD ? fo->mono_bold : f;
            ink(pf, part, 8, y, kind_colour(SH.kind[at]), ALIGN_LEFT);
            y -= lh;
            drawn++;
        }
    }
    if (SH.scroll > 0 && skip > 0)
        SH.scroll -= skip;
    return ret;
}
