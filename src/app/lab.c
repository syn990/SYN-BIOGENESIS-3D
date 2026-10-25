/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from LabWindow.java of Biogenesis 0.9
 * (https://biogenesis.sourceforge.net/), Copyright (C) 2006-2010 Joan
 * Queralt Molina, by way of SYN-BIOGENESIS's src/ui/lab.c.
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

/* The Genetic Laboratory, as SYN-BIOGENESIS has it: a screen under the bar
 * where a gene sequence, its symmetry and mirroring are made by hand, the
 * body shown as it grows, and the result copied to paste into the world or
 * kept in the library. Here each gene also has its bend (pitch), the body
 * turns in 3D, and the code's drives are set beside it. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_internal.h"
#include "guide/guide.h"
#include "guide/kind.h"
#include "sim/dmath.h"
#include "sim/params.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

/* ColorComboBox order */
static const int k_lab_colors[GENE_COLORS] = {COL_RED,   COL_GREEN, COL_BLUE,   COL_CYAN,
                                              COL_WHITE, COL_GRAY,  COL_YELLOW, COL_PURPLE};

void lab_load_code(App *a, const GeneticCode *gc)
{
    Lab *l = &a->lab;
    l->ngenes = gc->ngenes < LAB_MAX_GENES ? gc->ngenes : LAB_MAX_GENES;
    memcpy(l->genes, gc->genes, (size_t)l->ngenes * sizeof(Gene));
    l->symmetry = gc->symmetry;
    l->mirror = gc->mirror;
    l->disperse = gc->disperse_children ? 1 : 0;
    memcpy(l->drive, gc->drive, sizeof l->drive);
    l->height = gc->height;
    l->plan = gc->plan;
    l->cone = gc->cone;
    l->scroll = 0;
}

void lab_open(App *a, const GeneticCode *gc)
{
    Lab *l = &a->lab;
    l->ngenes = 0;
    l->symmetry = 2;
    l->mirror = 0;
    l->disperse = 0;
    memset(l->drive, 0, sizeof l->drive);
    l->height = 0.5;
    l->plan = PLAN_DISK;
    l->cone = 0;
    l->scroll = 0;
    l->tilt = 0.55;
    if (gc)
        lab_load_code(a, gc);
    l->open = true;
    a->screen = SCREEN_LAB;
}

void lab_close(App *a)
{
    a->lab.open = false;
    if (a->screen == SCREEN_LAB)
        a->screen = SCREEN_WORLD;
}

GeneticCode *lab_code(const App *a, bool disperse)
{
    const Lab *l = &a->lab;
    if (l->ngenes == 0)
        return NULL;
    GeneticCode *gc = gc_new_from_genes(l->genes, l->ngenes, l->symmetry, l->mirror,
                                        disperse && l->disperse != 0);
    memcpy(gc->drive, l->drive, sizeof gc->drive);
    gc->height = l->height;
    gc->plan = (uint8_t)l->plan;
    gc->cone = l->cone;
    return gc;
}

static void insert_gene(Lab *l, int at)
{
    if (l->ngenes >= LAB_MAX_GENES)
        return;
    memmove(&l->genes[at + 1], &l->genes[at], (size_t)(l->ngenes - at) * sizeof(Gene));
    l->genes[at] = (Gene){.length = 2.0, .theta = 0.0, .pitch = 0.0, .color = COL_GREEN};
    l->ngenes++;
}

static void delete_gene(Lab *l, int at)
{
    memmove(&l->genes[at], &l->genes[at + 1], (size_t)(l->ngenes - at - 1) * sizeof(Gene));
    l->ngenes--;
}

static float mid(TTF_Font *f, const char *s, float x, float cy, Tone t, Align al)
{
    return skin_text(f, s, x, cy - skin_text_height(f) * 0.5f, t, al);
}

static bool choice(UIRect r, const char *label, bool on, const char *tip)
{
    bool hv = uiin_over(r);
    skin_button(r, shell_fonts()->small_bold, label, (WidgetState){.hover = hv, .active = on}, true);
    if (hv && tip)
        uiin_tooltip(tip);
    return hv && uiin_clicked(r);
}

/* A number typed, or stepped with - and +: true when *v changed. Values
 * outside lo..hi are refused, or wrapped round when wrap is set (a turn). */
static bool number(App *a, const void *id, UIRect r, double *v, double lo, double hi, double step,
                   int decimals, bool wrap, const char *what)
{
    const Fonts *fo = shell_fonts();
    float bw = 22;
    UIRect field = {r.x, r.y, r.w - 2 * (bw + 2), r.h};
    char shown[32], typed[64];
    snprintf(shown, sizeof shown, "%.*f", decimals, *v);
    double nv = *v;
    bool changed = false;
    if (shell_field(id, field, fo->mono_small_bold, shown, typed, sizeof typed) &&
        typed[strspn(typed, " ")]) {
        char *end;
        double t = strtod(typed, &end);
        while (*end == ' ')
            end++;
        if (end == typed || *end || !isfinite(t) || (!wrap && (t < lo || t > hi)))
            interface_note(a, true, "%s takes a number from %g to %g", what, lo, hi);
        else {
            nv = t;
            changed = true;
        }
    }
    float bx = field.x + field.w + 2;
    if (screen_button(ui_rect(bx, r.y, bw, r.h), ICON_MINUS, "", false, NULL)) {
        nv = *v - step;
        changed = true;
    }
    if (screen_button(ui_rect(bx + bw + 2, r.y, bw, r.h), ICON_PLUS, "", false, NULL)) {
        nv = *v + step;
        changed = true;
    }
    if (!changed)
        return false;
    if (wrap) {
        double span = hi - lo + step;
        nv = fmod(nv - lo, span);
        if (nv < 0)
            nv += span;
        nv += lo;
    } else {
        nv = fmin(fmax(nv, lo), hi);
    }
    double k = pow(10, decimals);
    *v = round(nv * k) / k;
    return true;
}

/* the seven colours a gene can be, the field guide's line on each */
static void colours(UIRect r, Gene *g)
{
    float sw = r.w / GENE_COLORS;
    for (int k = 0; k < GENE_COLORS; k++) {
        UIRect s = {r.x + (float)k * sw + 1, r.y + 2, sw - 2, r.h - 4};
        uint8_t cr, cg, cb;
        bio_color_rgb(k_lab_colors[k], &cr, &cg, &cb);
        bool on = g->color == k_lab_colors[k], hv = uiin_over(s);
        if (on)
            skin_fill(ui_rect(s.x - 1, s.y - 2, s.w + 2, s.h + 4), skin_tone(TONE_TEXT), 1);
        skin_fill(s, (Rgb){cr, cg, cb}, on || hv ? 1.0f : 0.45f);
        if (hv) {
            static char tip[200];
            snprintf(tip, sizeof tip, "%s: %s", syn_col_name(k_lab_colors[k]),
                     syn_guide_short(k_lab_colors[k]));
            uiin_tooltip(tip);
            if (uiin_clicked(s))
                g->color = (uint8_t)k_lab_colors[k];
        }
    }
}

static const char *const k_drive_tips[DRIVE_COUNT] = {
    "Toward the light (1 seeks the surface's, -1 the dark): how strongly its cilia swim there",
    "Toward water with more dissolved CO2 (or away, below 0)",
    "Toward living bodies, by how green they are (or away)",
    "Toward living bodies, by how much red, gray and white they carry (or away: below 0 it flees)",
    "Toward corpses (or away)",
    "Toward any living body (or away)",
};

void lab_screen(App *a, int w, int h)
{
    const Fonts *fo = shell_fonts();
    Lab *l = &a->lab;
    UIRect in = screen_panel(w, h, a->hints, "GENETIC LABORATORY",
                             "a being's genes by hand; its body in 3D");
    GeneticCode *code = lab_code(a, true);
    float rh = 28;
    char buf[128];

    /* the general panel */
    float y = in.y, cy = y + rh * 0.5f;
    mid(fo->small, "Segments:", in.x, cy, TONE_MUTED, ALIGN_LEFT);
    snprintf(buf, sizeof buf, "%d", l->ngenes * l->symmetry);
    mid(fo->mono_small_bold, buf, in.x + 76, cy, TONE_TEXT, ALIGN_LEFT);
    mid(fo->small, "Energy to reproduce:", in.x + 150, cy, TONE_MUTED, ALIGN_LEFT);
    snprintf(buf, sizeof buf, "%d", 40 + 3 * l->symmetry * l->ngenes);
    mid(fo->mono_small_bold, buf, in.x + 290, cy, TONE_TEXT, ALIGN_LEFT);
    mid(fo->small, "Life expectancy:", in.x + 370, cy, TONE_MUTED, ALIGN_LEFT);
    snprintf(buf, sizeof buf, "%d", P.max_age);
    mid(fo->mono_small_bold, buf, in.x + 482, cy, TONE_TEXT, ALIGN_LEFT);
    if (code) {
        char kind[96];
        syn_kind_label(code, kind, sizeof kind);
        snprintf(buf, sizeof buf, "%s  ·  %s", kind,
                 gc_is_flat(code) ? "flat, as 0.9 grew them" : "bent out of the flat");
        mid(fo->small_bold, buf, in.x + in.w, cy, TONE_ACCENT, ALIGN_RIGHT);
    }
    y += rh + 6;
    cy = y + rh * 0.5f;
    mid(fo->small, "Symmetry:", in.x, cy, TONE_MUTED, ALIGN_LEFT);
    for (int s = 1; s <= 8; s++) {
        char n[4];
        snprintf(n, sizeof n, "%d", s);
        if (choice(ui_rect(in.x + 76 + (float)(s - 1) * 30, y, 28, rh), n, l->symmetry == s,
                   s == 1 ? "One arm" : NULL))
            l->symmetry = s;
    }
    float x = in.x + 76 + 8 * 30 + 24;
    mid(fo->small, "Mirror:", x, cy, TONE_MUTED, ALIGN_LEFT);
    if (choice(ui_rect(x + 56, y, 44, rh), "No", !l->mirror, NULL))
        l->mirror = 0;
    if (choice(ui_rect(x + 102, y, 44, rh), "Yes", l->mirror, "Every other arm mirrored"))
        l->mirror = 1;
    x += 170;
    mid(fo->small, "Disperse children:", x, cy, TONE_MUTED, ALIGN_LEFT);
    float dx = x + skin_text_width(fo->small, "Disperse children:") + 10;
    if (choice(ui_rect(dx, y, 44, rh), "No", !l->disperse, NULL))
        l->disperse = 0;
    if (choice(ui_rect(dx + 46, y, 44, rh), "Yes", l->disperse,
               "Its children are born away from it, not beside it"))
        l->disperse = 1;
    y += rh + 6;
    /* how its arms stand */
    cy = y + rh * 0.5f;
    mid(fo->small, "Body:", in.x, cy, TONE_MUTED, ALIGN_LEFT);
    static const char *const plan_tips[PLAN_COUNT] = {
        "0.9's: the arms around the body's axis, in one plane",
        "Every arm tilted out of the disk by the cone's angle, an umbrella or a jellyfish",
        "The arms spread over a sphere: two up and down, four a tetrahedron, six an octahedron, eight a cube"};
    for (int k = 0; k < PLAN_COUNT; k++) {
        char nm[16];
        snprintf(nm, sizeof nm, "%c%s", gc_plan_name(k)[0] - 32, gc_plan_name(k) + 1);
        if (choice(ui_rect(in.x + 76 + (float)k * 72, y, 68, rh), nm, l->plan == k, plan_tips[k]))
            l->plan = k;
    }
    if (l->plan == PLAN_CONE) {
        float cx0 = in.x + 76 + PLAN_COUNT * 72 + 24;
        mid(fo->small, "Tilt (deg):", cx0, cy, TONE_MUTED, ALIGN_LEFT);
        double deg = round(l->cone * 180.0 / D_PI);
        if (number(a, &l->cone, ui_rect(cx0 + 84, y, 116, rh), &deg, -90, 90, 5, 0, false, "A tilt"))
            l->cone = deg * D_PI / 180.0;
    }
    y += rh + 12;

    /* the genes */
    float bottom = in.y + in.h - 34 - 12;
    bool wide = in.w >= 1100;
    float tw = fminf(wide ? 782 : 672, in.w - 260);
    UIRect table = {in.x, y, tw, bottom - y};
    skin_well(table);
    float c_gene = table.x + 10, c_len = table.x + 54, c_turn = c_len + 124, c_bend = c_turn + 124;
    float c_col = c_bend + 124, c_ins = c_col + 188, insw = wide ? 64 : 28;
    float hy = y + 14;
    mid(fo->small_bold, "Gene", c_gene, hy, TONE_MUTED, ALIGN_LEFT);
    mid(fo->small_bold, "Length", c_len, hy, TONE_MUTED, ALIGN_LEFT);
    mid(fo->small_bold, "Turn (deg)", c_turn, hy, TONE_MUTED, ALIGN_LEFT);
    mid(fo->small_bold, "Bend (deg)", c_bend, hy, TONE_MUTED, ALIGN_LEFT);
    mid(fo->small_bold, "Colour", c_col, hy, TONE_MUTED, ALIGN_LEFT);
    float row_h = rh + 4;
    UIRect rows = {table.x + 1, y + 28, table.w - 2, table.h - 29};
    int visible = (int)(rows.h / row_h);
    int total = l->ngenes + 1; /* and the Add row */
    if (uiin_over(table))
        l->scroll -= (int)(uiin_wheel() * 2);
    if (l->scroll > total - visible)
        l->scroll = total - visible;
    if (l->scroll < 0)
        l->scroll = 0;
    gl2d_clip_push(rows.x, rows.y, rows.w, rows.h);
    int act_insert = -1, act_delete = -1;
    for (int i = l->scroll; i < total && i < l->scroll + visible; i++) {
        float ry = rows.y + (float)(i - l->scroll) * row_h;
        if (i == l->ngenes) {
            if (screen_button(ui_rect(c_gene, ry, 120, rh), ICON_PLUS, "Add", l->ngenes >= LAB_MAX_GENES,
                              "A gene at the end"))
                insert_gene(l, l->ngenes);
            break;
        }
        Gene *g = &l->genes[i];
        snprintf(buf, sizeof buf, "%d:", i);
        mid(fo->mono_small, buf, c_gene, ry + rh * 0.5f, TONE_TEXT, ALIGN_LEFT);
        number(a, &g->length, ui_rect(c_len, ry, 116, rh), &g->length, 2.0, 18.0, 0.1, 1, false,
               "A length");
        /* ThetaSpinner: whole degrees 0..359, wrapping */
        double deg = round(g->theta * 180.0 / D_PI);
        while (deg < 0)
            deg += 360;
        while (deg > 359)
            deg -= 360;
        if (number(a, &g->theta, ui_rect(c_turn, ry, 116, rh), &deg, 0, 359, 1, 0, true, "A turn"))
            g->theta = deg * D_PI / 180.0;
        /* the bend: whole degrees, straight down to straight up */
        double bend = round(g->pitch * 180.0 / D_PI);
        if (number(a, &g->pitch, ui_rect(c_bend, ry, 116, rh), &bend, -90, 90, 5, 0, false, "A bend"))
            g->pitch = bend * D_PI / 180.0;
        colours(ui_rect(c_col, ry, 176, rh), g);
        if (screen_button(ui_rect(c_ins, ry, insw, rh), wide ? NULL : ICON_PLUS, wide ? "Insert" : "",
                          l->ngenes >= LAB_MAX_GENES, "A gene before this one"))
            act_insert = i;
        if (screen_button(ui_rect(c_ins + insw + 4, ry, insw, rh), wide ? NULL : ICON_TRASH,
                          wide ? "Delete" : "", false, "This gene, gone"))
            act_delete = i;
    }
    gl2d_clip_pop();
    if (act_insert >= 0)
        insert_gene(l, act_insert);
    else if (act_delete >= 0)
        delete_gene(l, act_delete);

    /* its drives, and its body */
    float px = table.x + table.w + 12, pw = in.x + in.w - px;
    float dh = 26 + (DRIVE_COUNT + 1) * row_h + 4 + rh;
    UIRect preview = {px, y, pw, fmaxf(80, bottom - y - dh - 10)};
    skin_fill(preview, (Rgb){0, 0, 0}, 1);
    if (uiin_over(preview)) {
        /* still under the pointer, and dragged round and over */
        float mx, my;
        uiin_mouse(&mx, &my);
        if (uiin_mouse_down(0) && l->dragging) {
            l->turn += (mx - l->drag_x) * 0.01;
            l->tilt = fmin(1.55, fmax(-1.55, l->tilt + (my - l->drag_y) * 0.01));
        }
        l->dragging = uiin_mouse_down(0);
        l->drag_x = mx;
        l->drag_y = my;
        uiin_tooltip("Its body as it grows: drag to turn it and tilt it");
    } else {
        l->dragging = false;
        l->turn += (a->seconds - l->last_seconds) * 0.6;
    }
    l->last_seconds = a->seconds;
    if (code) {
        gl2d_clip_push(preview.x, preview.y, preview.w, preview.h);
        screen_code_view(code, preview, l->turn, l->tilt);
        gl2d_clip_pop();
    } else {
        mid(fo->small, "No genes yet: Add one, or Import a being", preview.x + preview.w * 0.5f,
            preview.y + preview.h * 0.5f, TONE_MUTED, ALIGN_CENTER);
    }

    float dy = preview.y + preview.h + 10;
    skin_text(fo->mono_small_bold, "DRIVES", px, dy, TONE_ACCENT, ALIGN_LEFT);
    skin_text(fo->small, "what it steers by: -1 away, 1 toward", px + 64, dy, TONE_MUTED, ALIGN_LEFT);
    dy += 24;
    /* the height its line keeps to */
    {
        UIRect r = {px, dy, pw, rh};
        mid(fo->small, "height", r.x + 4, r.y + rh * 0.5f, TONE_TEXT, ALIGN_LEFT);
        if (uiin_over(ui_rect(r.x, r.y, 90, rh)))
            uiin_tooltip("Where its line keeps, 0 the floor to 1 the surface: cilia swim it there, a float takes "
                         "a body without them");
        float fw = fminf(150, pw - 96);
        number(a, &l->height, ui_rect(r.x + pw - fw, r.y, fw, rh), &l->height, 0, 1, 0.05, 2, false, "A height");
        dy += row_h;
    }
    for (int d = 0; d < DRIVE_COUNT; d++) {
        UIRect r = {px, dy, pw, rh};
        mid(fo->small, gc_drive_name(d), r.x + 4, r.y + rh * 0.5f, TONE_TEXT, ALIGN_LEFT);
        if (uiin_over(ui_rect(r.x, r.y, 90, rh)))
            uiin_tooltip(k_drive_tips[d]);
        float fw = fminf(150, pw - 96);
        number(a, &l->drive[d], ui_rect(r.x + pw - fw, r.y, fw, rh), &l->drive[d], -1, 1, 0.1, 2, false,
               "A drive");
        dy += row_h;
    }
    float hb = (pw - 8) / 2;
    if (screen_button(ui_rect(px, dy + 4, hb, rh), ICON_DNA, "From its body", !code,
                      "The height and drives its make-up asks for, as a 0.9 being is given: leaves high, "
                      "seeking light and keeping from hunters; eaters seeking plants and the dead; methane "
                      "eaters and leafless eaters low") &&
        code) {
        gc_instincts(code);
        memcpy(l->drive, code->drive, sizeof l->drive);
        l->height = gc_body_height(code);
    }
    if (screen_button(ui_rect(px + hb + 8, dy + 4, hb, rh), ICON_XMARK, "None", false,
                      "Every drive 0: its cilia push at random, as 0.9's did"))
        memset(l->drive, 0, sizeof l->drive);

    /* what is done with it */
    float by = in.y + in.h - 34, bx = in.x;
    if (screen_button(ui_rect(bx, by, 190, 34), ICON_COPY, "Copy to clipboard", !code,
                      "Its genetic code copied: right-click the world and Paste") &&
        code) {
        gc_assign(&a->clipboard, code);
        interface_note(a, false, "Genetic code copied: right-click the world and Paste");
        lab_close(a);
    }
    bx += 198;
    if (screen_button(ui_rect(bx, by, 110, 34), ICON_XMARK, "Cancel", false,
                      "The laboratory closed, its genes let go  (Esc)"))
        lab_close(a);
    bx += 118;
    if (screen_button(ui_rect(bx, by, 110, 34), ICON_TRASH, "Clear", false, "Every gene gone"))
        l->ngenes = 0;
    bx += 118;
    if (screen_button(ui_rect(bx, by, 130, 34), ICON_IMPORT, "Import...", false,
                      "A being's .bgg into the laboratory"))
        files_open(a, FILES_IMPORT_LAB);
    bx += 138;
    if (screen_button(ui_rect(bx, by, 110, 34), ICON_BOOKMARK, "Keep", !code,
                      "Its genetic code kept in the being library") &&
        code) {
        app_library_keep(a, code, "Saved", "LABORATORY", "the genetic laboratory");
    }
    gc_unref(code);
}
