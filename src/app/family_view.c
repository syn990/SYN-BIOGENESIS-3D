/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/family_view.c, the SYN edition of its C port
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

#include "app/family_view.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app/app_internal.h"
#include "compat.h"
#include "io/bgg.h"
#include "syn/family.h"
#include "syn/history.h"
#include "ui/gl2d.h"
#include "ui/input.h"
#include "ui/shell.h"
#include "ui/skin.h"

int syn_stats_epoch_span(const History *hi);

#define NODE_W 176.0f
#define NODE_H 172.0f
#define TEXT_H 54.0f   /* under the body, at full size */
#define COL_GAP 250.0f /* room for what changed, on each edge */
#define ROW_GAP 28.0f
#define CARD_W 350.0f
#define LIVING_MAX 40   /* LIVING: the biggest this many */
#define NOTABLE_MAX 150 /* EVERY NOTABLE: the biggest peaks, this many */
#define TEXT_ZOOM 0.55f /* below this the boxes are too small for words */

typedef enum { SHOW_LIVING, SHOW_BEST, SHOW_NOTABLE, SHOW_COUNT } Show;
static const char *const k_show[SHOW_COUNT] = {"LIVING", "BEST OF EACH EPOCH", "EVERY NOTABLE"};

typedef struct {
    uint32_t id;
    int up;          /* the node it hangs from, or -1 */
    int skipped;     /* ancestors folded into the edge from up */
    int kid0, nkids; /* its children: V.kid[kid0 .. kid0 + nkids) */
    int ep0, ep1;    /* the epochs it is the best of, as numbered on screen (0: none) */
    float x, y;      /* centre, in the graph */
} Node;

static struct {
    Show show;
    uint32_t focus;
    bool bring; /* move the focus into view at the next layout */
    int fit;    /* at the next layout: FIT_ALL the whole tree, FIT_READ as much as can be read */
    float cam_x, cam_y;  /* the graph point at the canvas' centre */
    float zoom;
    bool drag;
    int drag_mx, drag_my;
    float drag_cx, drag_cy;
    Uint32 click_at; /* a double click: the node and when */
    uint32_t click_id;
    Node *nodes;
    int n, cap;
    int *kid;
    int kid_cap;
    bool *path; /* nodes from the focus up to its root */
    int path_cap;
    float x0, y0, x1, y1; /* the tree's bounds */
    unsigned laid_version;
    Show laid_show;
    uint32_t laid_focus;
    int laid_span, laid_epochs;
    bool laid;
    int epoch_span;
} V = {.show = SHOW_BEST, .fit = 2, .zoom = 1};

enum { FIT_NONE, FIT_ALL, FIT_READ };

/* ---- small helpers ---- */

static bool over(UIRect r)
{
    return uiin_over(r);
}

static bool button(UIRect r, const char *label, bool active)
{
    bool h = over(r);
    skin_button(r, shell_fonts()->bold, label, (WidgetState){.hover = h, .active = active}, true);
    return h && uiin_clicked(r);
}

static void stroke(float x0, float y0, float x1, float y1, float hw, SDL_Color c)
{
    skin_line(x0, y0, x1, y1, 2 * hw, (Rgb){c.r, c.g, c.b}, (float)c.a / 255.0f);
}

static void outline(UIRect r, float t, Rgb c, float a)
{
    skin_fill(ui_rect(r.x, r.y, r.w, t), c, a);
    skin_fill(ui_rect(r.x, r.y + r.h - t, r.w, t), c, a);
    skin_fill(ui_rect(r.x, r.y + t, t, r.h - 2 * t), c, a);
    skin_fill(ui_rect(r.x + r.w - t, r.y + t, t, r.h - 2 * t), c, a);
}

static void tnum(char *out, size_t n, int64_t t)
{
    char v[24];
    fmt_num(v, sizeof v, (double)t);
    snprintf(out, n, "t %s", v);
}

static const Species *species(const Node *nd)
{
    return syn_family_get(nd->id);
}

/* A named family begins with it, away from a root. */
static bool new_family(const Species *s)
{
    return s->family == s->id && s->parent && s->name_no > 0;
}

/* "family VELORAX, from OMICRON", "began family ZEPHYRA, from VELORAX" */
static void family_line(const Species *s, char *out, size_t n)
{
    const Species *head = syn_family_head(s), *from = syn_family_parent_head(s);
    char fam[40] = "", pf[40] = "";
    if (head)
        syn_family_title(head->name_no, fam, sizeof fam);
    if (from)
        syn_family_title(from->name_no, pf, sizeof pf);
    bool began = head == s;
    if (!fam[0])
        snprintf(out, n, "%s a family not yet named", began ? "began" : "of");
    else
        snprintf(out, n, "%sfamily %s%s%s", began ? "began " : "", fam, pf[0] ? ", from " : "", pf);
}

/* A code's body in r, on black, as the library draws it (a gone species'
 * dimmed). */
static void body(UIRect r, const GeneticCode *gc, Uint8 alpha)
{
    skin_fill(r, (Rgb){0, 0, 0}, 1);
    if (!gc)
        return;
    gl2d_clip_push(r.x, r.y, r.w, r.h);
    screen_code(gc, r, 0);
    gl2d_clip_pop();
    if (alpha < 255)
        skin_fill(r, (Rgb){0, 0, 0}, 1 - (float)alpha / 255.0f);
}

static void epochs_text(const Node *nd, char *out, size_t n)
{
    if (!nd->ep0)
        out[0] = '\0';
    else if (nd->ep0 == nd->ep1)
        snprintf(out, n, "EPOCH %d", nd->ep0);
    else
        snprintf(out, n, "EPOCHS %d-%d", nd->ep0, nd->ep1);
}

/* ---- the layout ---- */

static int by_alive(const void *a, const void *b)
{
    const Species *x = syn_family_at(*(const int *)a), *y = syn_family_at(*(const int *)b);
    return (y->alive > x->alive) - (y->alive < x->alive);
}

static int by_peak(const void *a, const void *b)
{
    const Species *x = syn_family_at(*(const int *)a), *y = syn_family_at(*(const int *)b);
    return (y->peak > x->peak) - (y->peak < x->peak);
}

static int parent_index(int i)
{
    const Species *s = syn_family_at(i);
    return s && s->parent ? syn_family_index(s->parent) : -1;
}

/* Left to right by generation of the shown tree; leaves one under
 * another, each parent centred on its children. */
static void place(int k, int col, float *y)
{
    Node *nd = &V.nodes[k];
    nd->x = (float)col * (NODE_W + COL_GAP);
    if (nd->nkids == 0) {
        nd->y = *y + NODE_H / 2;
        *y += NODE_H + ROW_GAP;
        return;
    }
    for (int j = 0; j < nd->nkids; j++)
        place(V.kid[nd->kid0 + j], col + 1, y);
    nd->y = (V.nodes[V.kid[nd->kid0]].y + V.nodes[V.kid[nd->kid0 + nd->nkids - 1]].y) / 2;
}

enum { PICK = 1, SEEN = 2, KEEP = 4 };

static void layout(const History *hi)
{
    int ns = syn_family_count();
    unsigned char *f = calloc((size_t)ns + 1, 1);
    int *vk = calloc((size_t)ns + 1, sizeof *vk);
    int *node_of = malloc(((size_t)ns + 1) * sizeof *node_of);
    int *ep0 = calloc((size_t)ns + 1, sizeof *ep0), *ep1 = calloc((size_t)ns + 1, sizeof *ep1);
    int *order = malloc(((size_t)ns + 1) * sizeof *order);
    if (!f || !vk || !node_of || !ep0 || !ep1 || !order)
        abort();

    /* the best of each epoch, numbered as the EPOCHS tab numbers them */
    int span = syn_stats_epoch_span(hi);
    V.epoch_span = span;
    if (span > 0 && hi->nepochs > 0) {
        int64_t k0 = hi->t_start / span, k1 = hi->t_now / span;
        for (int64_t k = k0; k <= k1; k++) {
            Epoch ep;
            if (!syn_history_merge(k * span, (k + 1) * span, &ep) || !ep.best)
                continue;
            int i = syn_family_index(ep.best);
            if (i < 0)
                continue;
            int num = (int)(k - k0 + 1);
            if (!ep0[i])
                ep0[i] = num;
            ep1[i] = num;
            if (V.show == SHOW_BEST)
                f[i] |= PICK;
        }
    }
    /* the others' picks */
    int m = 0;
    for (int i = 0; i < ns; i++) {
        const Species *s = syn_family_at(i);
        if ((V.show == SHOW_LIVING && s->alive > 0) || (V.show == SHOW_NOTABLE && s->notable))
            order[m++] = i;
    }
    if (V.show == SHOW_LIVING) {
        qsort(order, (size_t)m, sizeof *order, by_alive);
        if (m > LIVING_MAX)
            m = LIVING_MAX;
    } else if (V.show == SHOW_NOTABLE) {
        qsort(order, (size_t)m, sizeof *order, by_peak);
        if (m > NOTABLE_MAX)
            m = NOTABLE_MAX;
    }
    for (int j = 0; j < m; j++)
        f[order[j]] |= PICK;
    int fi = syn_family_index(V.focus);
    if (fi >= 0)
        f[fi] |= PICK;

    /* with what they descend from */
    for (int i = 0; i < ns; i++)
        for (int j = i; j >= 0 && (f[i] & PICK) && !(f[j] & SEEN); j = parent_index(j))
            f[j] |= SEEN;
    for (int i = 0; i < ns; i++) {
        int p = parent_index(i);
        if ((f[i] & SEEN) && p >= 0)
            vk[p]++;
    }
    /* an ancestor with one child shown folds into the edge */
    for (int i = 0; i < ns; i++)
        if ((f[i] & SEEN) && ((f[i] & PICK) || parent_index(i) < 0 || vk[i] != 1))
            f[i] |= KEEP;

    V.n = 0;
    for (int i = 0; i < ns; i++) {
        if (!(f[i] & KEEP))
            continue;
        if (V.n == V.cap) {
            V.cap = V.cap ? V.cap * 2 : 128;
            V.nodes = realloc(V.nodes, (size_t)V.cap * sizeof *V.nodes);
            if (!V.nodes)
                abort();
        }
        node_of[i] = V.n;
        Node *nd = &V.nodes[V.n++];
        memset(nd, 0, sizeof *nd);
        nd->id = syn_family_at(i)->id;
        nd->ep0 = ep0[i];
        nd->ep1 = ep1[i];
        int p = parent_index(i);
        while (p >= 0 && !(f[p] & KEEP)) {
            nd->skipped++;
            p = parent_index(p);
        }
        nd->up = p >= 0 ? node_of[p] : -1;
    }
    /* children, in the order they were founded */
    if (V.n > V.kid_cap) {
        V.kid_cap = V.n;
        V.kid = realloc(V.kid, (size_t)V.kid_cap * sizeof *V.kid);
        V.path = realloc(V.path, (size_t)V.kid_cap * sizeof *V.path);
        if (!V.kid || !V.path)
            abort();
    }
    for (int k = 0; k < V.n; k++)
        if (V.nodes[k].up >= 0)
            V.nodes[V.nodes[k].up].nkids++;
    int at = 0;
    for (int k = 0; k < V.n; k++) {
        V.nodes[k].kid0 = at;
        at += V.nodes[k].nkids;
        V.nodes[k].nkids = 0;
    }
    for (int k = 0; k < V.n; k++) {
        Node *up = V.nodes[k].up >= 0 ? &V.nodes[V.nodes[k].up] : NULL;
        if (up)
            V.kid[up->kid0 + up->nkids++] = k;
    }
    float y = 0;
    for (int k = 0; k < V.n; k++)
        if (V.nodes[k].up < 0) {
            place(k, 0, &y);
            y += ROW_GAP * 2;
        }
    V.x0 = V.y0 = 1e30f;
    V.x1 = V.y1 = -1e30f;
    for (int k = 0; k < V.n; k++) {
        V.x0 = fminf(V.x0, V.nodes[k].x - NODE_W / 2);
        V.x1 = fmaxf(V.x1, V.nodes[k].x + NODE_W / 2);
        V.y0 = fminf(V.y0, V.nodes[k].y - NODE_H / 2);
        V.y1 = fmaxf(V.y1, V.nodes[k].y + NODE_H / 2);
    }
    free(f);
    free(vk);
    free(node_of);
    free(ep0);
    free(ep1);
    free(order);
}

static int node_index(uint32_t id)
{
    for (int k = 0; k < V.n; k++)
        if (V.nodes[k].id == id)
            return k;
    return -1;
}

/* ---- export ---- */

static void hex(Rgb c, char out[8])
{
    snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b);
}

static void html(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '&')
            fputs("&amp;", f);
        else if (*s == '<')
            fputs("&lt;", f);
        else if (*s == '>')
            fputs("&gt;", f);
        else if (*s == '"')
            fputs("&quot;", f);
        else
            fputc(*s, f);
    }
}

/* s in single quotes for the shell */
static void shell_quote(const char *s, char *out, size_t n)
{
    size_t k = 0;
    if (k < n - 1)
        out[k++] = '\'';
    for (; *s && k + 5 < n; s++) {
        if (*s == '\'') {
            memcpy(out + k, "'\\''", 4);
            k += 4;
        } else {
            out[k++] = *s;
        }
    }
    if (k < n - 1)
        out[k++] = '\'';
    out[k] = '\0';
}

/* The body as a picture, as the screens draw it. */
static bool body_png(const GeneticCode *gc, const char *path)
{
    return gc && code_image(gc, 112, path);
}

/* "VELORAX-3", "OMICRON-II-14"; one not numbered by its founder's number
 * (Color Mod's two steps in one birth share one) */
static void file_stem(const Species *s, char *out, size_t n)
{
    syn_family_name(s, out, n);
    if (!s->number) {
        snprintf(out, n, "species-%d-%u", s->founder, s->id);
        return;
    }
    for (char *p = out; *p; p++)
        if (*p == ' ')
            *p = '-';
}

static void file_name(const Species *s, const char *what, char *out, size_t n)
{
    char stem[64];
    file_stem(s, stem, sizeof stem);
    snprintf(out, n, "%s.%s", stem, what);
}

static void export_tree(App *a, const History *hi)
{
    /* beside the world, else where the file dialogs start for codes (the
     * folder the game started in) */
    char base[1024], name[256] = "world";
    const char *slash = strrchr(a->world_path, '/');
    if (a->world_path[0] && slash) {
        snprintf(base, sizeof base, "%.*s", (int)(slash - a->world_path), a->world_path);
        snprintf(name, sizeof name, "%s", slash + 1);
        char *dot = strrchr(name, '.');
        if (dot && dot != name)
            *dot = '\0';
    } else {
        const char *home = home_dir();
        snprintf(base, sizeof base, "%s", home && *home ? home : ".");
    }
    char dir[1400];
    snprintf(dir, sizeof dir, "%s/%s-family-t%lld", base, name, (long long)hi->t_now);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        interface_note(a, true, "Could not make the folder for the family tree");
        return;
    }
    char path[1700];
    const Theme *th = skin_theme();
    char bg[8], panel[8], text[8], border[8], accent[8], dim[8], good[8];
    hex(th->bg, bg);
    hex(th->panel, panel);
    hex(th->text, text);
    hex(th->border, border);
    hex(th->accent, accent);
    hex(th->accent_dim, dim);
    SDL_Color gc_ = skin_tone_color(TONE_GOOD);
    hex((Rgb){gc_.r, gc_.g, gc_.b}, good);

    /* each species shown: its picture and its code */
    int pictures = 0;
    for (int k = 0; k < V.n; k++) {
        const Species *s = species(&V.nodes[k]);
        if (!s || !s->gc)
            continue;
        char fn[96];
        file_name(s, "png", fn, sizeof fn);
        snprintf(path, sizeof path, "%s/%s", dir, fn);
        pictures += body_png(s->gc, path);
        file_name(s, "bgg", fn, sizeof fn);
        snprintf(path, sizeof path, "%s/%s", dir, fn);
        char err[200];
        bgg_save(path, s->gc, err, (int)sizeof err);
    }

    /* the graph */
    snprintf(path, sizeof path, "%s/family.dot", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        interface_note(a, true, "Could not write the family tree");
        return;
    }
    char tn[32];
    fmt_num(tn, sizeof tn, (double)hi->t_now);
    fprintf(f, "digraph family {\n");
    fprintf(f,
            "  graph [rankdir=LR, bgcolor=\"%s\", fontcolor=\"%s\", fontname=\"sans\", fontsize=22, "
            "nodesep=0.3, ranksep=1.1, pad=0.4, labelloc=t, label=\"%s at t %s: %s  \xc2\xb7  %s\\n\\n\"];\n",
            bg, text, name, tn, k_show[V.show], th->name);
    fprintf(f, "  node [shape=plain, fontname=\"sans\", fontcolor=\"%s\", fontsize=11];\n", text);
    fprintf(f, "  edge [color=\"%s\", fontname=\"sans\", fontcolor=\"%s\", fontsize=10, arrowsize=0.6, "
               "penwidth=1.2];\n", border, text);
    for (int k = 0; k < V.n; k++) {
        const Node *nd = &V.nodes[k];
        const Species *s = species(nd);
        if (!s)
            continue;
        bool focus = s->id == V.focus;
        const char *frame = focus ? accent : nd->ep0 ? accent : V.path[k] ? dim : border;
        fprintf(f, "  n%u [label=<<TABLE BORDER=\"%d\" COLOR=\"%s\" BGCOLOR=\"%s\" CELLBORDER=\"0\" "
                   "CELLPADDING=\"3\" CELLSPACING=\"0\">", s->id, focus ? 3 : nd->ep0 ? 2 : 1, frame, panel);
        char fn[96];
        file_name(s, "png", fn, sizeof fn);
        if (s->gc && pictures)
            fprintf(f, "<TR><TD FIXEDSIZE=\"TRUE\" WIDTH=\"112\" HEIGHT=\"112\"><IMG SRC=\"%s\"/></TD></TR>", fn);
        char ep[32];
        epochs_text(nd, ep, sizeof ep);
        char nm[64];
        syn_family_label(s, nm, sizeof nm);
        fputs("<TR><TD><B>", f);
        html(f, nm);
        fprintf(f, "</B>%s%s%s</TD></TR>", new_family(s) ? "  new family" : "", ep[0] ? "  " : "", ep);
        if (s->number)
            fprintf(f, "<TR><TD><FONT POINT-SIZE=\"9\">#%d</FONT></TD></TR>", s->founder);
        char plan[160];
        syn_family_plan(s->gc, plan, sizeof plan);
        fputs("<TR><TD><FONT POINT-SIZE=\"9\">", f);
        html(f, plan);
        fputs("</FONT></TD></TR>", f);
        char st[96], v[32], when[32];
        fmt_num(v, sizeof v, s->alive > 0 ? s->alive : s->peak);
        tnum(when, sizeof when, s->alive > 0 ? s->born : s->last_seen);
        if (s->alive > 0)
            snprintf(st, sizeof st, "%s living; here since %s", v, when);
        else
            snprintf(st, sizeof st, "peaked at %s; gone at %s", v, when);
        fprintf(f, "<TR><TD><FONT POINT-SIZE=\"9\" COLOR=\"%s\">%s</FONT></TD></TR></TABLE>>];\n",
                s->alive > 0 ? good : border, st);
        if (nd->up >= 0) {
            char ch[200], lab[260];
            syn_family_change(s, ch, sizeof ch);
            if (nd->skipped)
                snprintf(lab, sizeof lab, "%s\\n(%d species between)", ch, nd->skipped);
            else
                snprintf(lab, sizeof lab, "%s", ch);
            fprintf(f, "  n%u -> n%u [label=\"", V.nodes[nd->up].id, s->id);
            for (const char *p = lab; *p; p++) {
                if (*p == '"')
                    fputc('\\', f);
                fputc(*p, f);
            }
            if (V.path[k])
                fprintf(f, "\", color=\"%s\", penwidth=2.5];\n", accent);
            else
                fprintf(f, "\"];\n");
        }
    }
    fprintf(f, "}\n");
    fclose(f);

    /* the best of each epoch, compiled */
    snprintf(path, sizeof path, "%s/best-of-each-epoch.txt", dir);
    f = fopen(path, "w");
    int bests = 0;
    if (f) {
        int span = V.epoch_span;
        fprintf(f, "The best of each epoch of %s: the species with the most living at once.\n", name);
        fprintf(f, "Epochs of %d time units, to t %s. Each one's code is in epoch-NN-NAME.bgg.\n\n", span, tn);
        int64_t k0 = span > 0 ? hi->t_start / span : 0, k1 = span > 0 ? hi->t_now / span : -1;
        for (int64_t kk = k0; kk <= k1; kk++) {
            Epoch ep;
            if (!syn_history_merge(kk * span, (kk + 1) * span, &ep) || !ep.best)
                continue;
            const Species *s = syn_family_get(ep.best);
            if (!s || !s->gc)
                continue;
            int num = (int)(kk - k0 + 1);
            char fn[128], plan[160], t0[32], t1[32], bt[32], bn[32], stem[64], nm[64];
            file_stem(s, stem, sizeof stem);
            syn_family_label(s, nm, sizeof nm);
            snprintf(fn, sizeof fn, "epoch-%02d-%s.bgg", num, stem);
            snprintf(path, sizeof path, "%s/%s", dir, fn);
            char err[200];
            bgg_save(path, s->gc, err, (int)sizeof err);
            syn_family_plan(s->gc, plan, sizeof plan);
            fmt_num(t0, sizeof t0, (double)ep.t0);
            fmt_num(t1, sizeof t1, (double)ep.t1);
            fmt_num(bt, sizeof bt, (double)ep.best_t);
            fmt_num(bn, sizeof bn, ep.best_n);
            fprintf(f, "epoch %2d  t %s to %s\n  %s (#%d): %s living at once (t %s); %s\n  %s\n\n", num, t0,
                    t1, nm, s->founder, bn, bt, plan, fn);
            bests++;
        }
        fclose(f);
    }

    /* Graphviz, if it is there */
    char q[1600], cmd[3400];
    shell_quote(dir, q, sizeof q);
    bool drawn = false;
    if (system("command -v dot >/dev/null 2>&1") == 0) {
        snprintf(cmd, sizeof cmd,
                 "cd %s && dot -Tsvg family.dot -o family.svg 2>/dev/null && dot -Tpng family.dot -o family.png "
                 "2>/dev/null",
                 q);
        drawn = system(cmd) == 0;
    }
    char msg[1600];
    snprintf(msg, sizeof msg, "Family tree (%d species, %d epochs' best) saved in %s%s", V.n, bests, dir,
             drawn ? ", drawn by Graphviz" : "; install Graphviz to draw family.dot");
    interface_note(a, false, "%s", msg);
    syn_history_user(&a->w, 0, 0, "You exported the family tree");
}

/* ---- the public side ---- */

static bool g_export;

float syn_family_controls(App *a, float x, float y, float h, char *info, size_t n)
{
    (void)a;
    const Fonts *fo = shell_fonts();
    for (int i = 0; i < SHOW_COUNT; i++) {
        UIRect r = {x, y, skin_text_width(fo->bold, k_show[i]) + 30, h};
        if (button(r, k_show[i], V.show == (Show)i) && V.show != (Show)i) {
            V.show = (Show)i;
            V.fit = FIT_READ;
        }
        x += r.w + 8;
    }
    x += 12;
    UIRect rf = {x, y, 70, h};
    if (button(rf, "FIT", false))
        V.fit = FIT_ALL;
    x += rf.w + 8;
    UIRect re = {x, y, 100, h};
    if (button(re, "EXPORT", false))
        g_export = true;
    if (over(re))
        uiin_tooltip("Save the tree as shown for Graphviz, with every species' picture and code, and the best of "
                   "each epoch compiled, in a folder beside the world");
    x += re.w + 8;
    int kept = syn_family_count(), fams = syn_family_named();
    snprintf(info, n, "%d of %d species, %d famil%s; drag to move, wheel to zoom, click one to follow it", V.n, kept,
             fams, fams == 1 ? "y" : "ies");
    return x;
}

void syn_family_focus(uint32_t id)
{
    V.focus = id;
    V.bring = id != 0;
}

/* where graph point (gx, gy) is on screen */
static float sx(UIRect c, float gx)
{
    return c.x + c.w / 2 + (gx - V.cam_x) * V.zoom;
}

static float sy(UIRect c, float gy)
{
    return c.y + c.h / 2 + (gy - V.cam_y) * V.zoom;
}

/* The whole tree; or, when that would be too small to read, as much as
 * can be read from its top left (or around the focus). */
static void fit(UIRect c, int how)
{
    if (V.n == 0)
        return;
    float bw = V.x1 - V.x0 + 80, bh = V.y1 - V.y0 + 80;
    V.zoom = fminf(1.0f, fminf(c.w / bw, c.h / bh));
    if (V.zoom < 0.04f)
        V.zoom = 0.04f;
    V.cam_x = (V.x0 + V.x1) / 2;
    V.cam_y = (V.y0 + V.y1) / 2;
    if (how != FIT_READ || V.zoom >= TEXT_ZOOM)
        return;
    V.zoom = 0.7f;
    if (bw * V.zoom > c.w)
        V.cam_x = V.x0 - 40 + c.w / 2 / V.zoom;
    if (bh * V.zoom > c.h)
        V.cam_y = V.y0 - 40 + c.h / 2 / V.zoom;
    int k = node_index(V.focus);
    if (k >= 0) {
        V.cam_x = V.nodes[k].x;
        V.cam_y = V.nodes[k].y;
    }
}

static void edge(UIRect c, const Node *from, const Node *to, bool on)
{
    float x0 = sx(c, from->x + NODE_W / 2), y0 = sy(c, from->y);
    float x3 = sx(c, to->x - NODE_W / 2), y3 = sy(c, to->y);
    float dx = (x3 - x0) * 0.5f;
    float px[21], py[21];
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20, u = 1 - t;
        px[i] = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x3 - dx) + t * t * t * x3;
        py[i] = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y3 + t * t * t * y3;
    }
    const Theme *th = skin_theme();
    Rgb rc = on ? th->accent : th->border;
    SDL_Color col = {rc.r, rc.g, rc.b, 255};
    float hw = (on ? 1.6f : 0.9f) * fmaxf(0.7f, fminf(1.4f, V.zoom));
    for (int i = 0; i < 20; i++)
        stroke(px[i], py[i], px[i + 1], py[i + 1], hw, col);
    /* the arrowhead */
    float ax = 7 * fmaxf(0.6f, fminf(1.2f, V.zoom));
    stroke(x3 - ax, y3 - ax * 0.6f, x3, y3, hw, col);
    stroke(x3 - ax, y3 + ax * 0.6f, x3, y3, hw, col);
}

static void edge_label(UIRect c, const Node *from, const Node *to)
{
    const Species *s = species(to);
    if (!s)
        return;
    const Fonts *fo = shell_fonts();
    char ch[200], fit_[200];
    syn_family_change(s, ch, sizeof ch);
    float x0 = sx(c, from->x + NODE_W / 2), x3 = sx(c, to->x - NODE_W / 2);
    float room = x3 - x0 - 16;
    if (room < 40)
        return;
    /* along the edge's last stretch, where it runs flat into the box: on
     * two lines if one is too short, broken after a comma or a space */
    char l1[200] = "", *l2 = ch;
    if (skin_text_width(fo->small, ch) > room) {
        int cut = -1;
        for (int i = 0; ch[i]; i++) {
            if (ch[i] != ' ')
                continue;
            snprintf(l1, sizeof l1, "%.*s", i - (i > 0 && ch[i - 1] == ','), ch);
            if (skin_text_width(fo->small, l1) > room)
                break;
            cut = i;
        }
        if (cut > 0) {
            snprintf(l1, sizeof l1, "%.*s", cut - (ch[cut - 1] == ','), ch);
            l2 = ch + cut + 1;
        } else {
            l1[0] = '\0';
        }
    }
    float lh = skin_text_height(fo->small), tx = x3 - 10;
    float ty = sy(c, to->y) - lh - 3;
    const char *lines[2] = {l2, l1};
    for (int i = 0; i < 2; i++, ty -= lh) {
        if (!lines[i][0])
            continue;
        const char *t = skin_fit(fo->small, lines[i], room, fit_, sizeof fit_);
        float w = skin_text_width(fo->small, t);
        skin_fill(ui_rect(tx - w - 4, ty, w + 8, lh), skin_theme()->bg, 0.75f);
        skin_text(fo->small, t, tx, ty, TONE_TEXT, ALIGN_RIGHT);
    }
    if (to->skipped) {
        char b[48];
        snprintf(b, sizeof b, "%d species between", to->skipped);
        skin_text(fo->small, b, tx, sy(c, to->y) + 3, TONE_MUTED, ALIGN_RIGHT);
    }
}

/* A species' menu: copy its code, keep it in the library. */
static void species_menu(App *a, const Species *s)
{
    char nm[64], plan[160], origin[200];
    syn_family_label(s, nm, sizeof nm);
    syn_family_plan(s->gc, plan, sizeof plan);
    snprintf(origin, sizeof origin, "the family tree: species %.40s, born at time %lld; %.100s", nm,
             (long long)s->born, plan);
    app_being_menu(a, s->gc, nm, origin, "Saved");
}

static void card(App *a, UIRect r, const History *hi)
{
    (void)hi;
    const Fonts *fo = shell_fonts();
    skin_panel(r, false);
    float x = r.x + 16, y = r.y + 14, w = r.w - 32;
    char buf[256], fit_[256];
    const Species *s = syn_family_get(V.focus);
    if (!s) {
        skin_text(fo->heading, "FAMILY TREE", x, y, TONE_TEXT, ALIGN_LEFT);
        y += skin_text_height(fo->heading) + 8;
        static const char *const help[] = {
            "Every box is a species: a body plan",
            "(arms, mirroring, the colours of its",
            "segments). Lengths and angles change",
            "within one; a birth that changes the",
            "plan founds a new species. Each edge",
            "says what changed.",
            "",
            "Species are grouped in families,",
            "each named (VELORAX, OMICRON ...)",
            "when one of them first has 5 living",
            "at once; a species is numbered when",
            "it does (VELORAX-3). One far enough",
            "from its family's first plan begins",
            "a new family. VELORAX sp.: one not",
            "numbered yet.",
            "",
            "Click a species to follow it here;",
            "double-click opens it in the",
            "laboratory, a right click its menu.",
            "Choose an organism in the world and",
            "press Family to find its species.",
        };
        for (size_t i = 0; i < sizeof help / sizeof help[0]; i++) {
            skin_text(fo->small, help[i], x, y, TONE_MUTED, ALIGN_LEFT);
            y += skin_text_height(fo->small) + 1;
        }
        return;
    }
    syn_family_label(s, buf, sizeof buf);
    if (buf[0] == '#')
        snprintf(buf, sizeof buf, "SPECIES #%d", s->founder);
    float hw = skin_text(fo->heading, skin_fit(fo->heading, buf, w - 70, fit_, sizeof fit_), x, y, TONE_TEXT,
                         ALIGN_LEFT);
    skin_text(fo->small_bold, s->alive > 0 ? "LIVING" : "GONE", x + hw + 10, y + 5,
              s->alive > 0 ? TONE_GOOD : TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->heading) + 6;
    float side = fminf(w, 210);
    UIRect th = {x + (w - side) / 2, y, side, side};
    body(th, s->gc, 255);
    if (over(th) && s->gc) {
        uiin_tooltip("Click for its genes; right click for its menu");
        if (uiin_clicked(th))
            lab_open(a, s->gc);
        else if (uiin_right_clicked(th))
            species_menu(a, s);
    }
    y += side + 10;

    /* the plan, its numbers */
    char plan[200];
    syn_family_plan(s->gc, plan, sizeof plan);
    char *semi = strchr(plan, ';');
    if (semi) {
        *semi = '\0';
        skin_text(fo->bold, plan, x, y, TONE_TEXT, ALIGN_LEFT);
        y += skin_text_height(fo->bold);
        skin_text(fo->small, skin_fit(fo->small, semi + 2, w, fit_, sizeof fit_), x, y, TONE_TEXT, ALIGN_LEFT);
    } else {
        skin_text(fo->small, skin_fit(fo->small, plan, w, fit_, sizeof fit_), x, y, TONE_TEXT, ALIGN_LEFT);
    }
    y += skin_text_height(fo->small) + 8;
    char v1[32], v2[32], v3[32];
    if (s->alive > 0) {
        fmt_num(v1, sizeof v1, s->alive);
        snprintf(buf, sizeof buf, "%s living now", v1);
        skin_text(fo->small_bold, buf, x, y, TONE_GOOD, ALIGN_LEFT);
    } else {
        tnum(v1, sizeof v1, s->last_seen);
        snprintf(buf, sizeof buf, "died out by %s", v1);
        skin_text(fo->small_bold, buf, x, y, TONE_MUTED, ALIGN_LEFT);
    }
    y += skin_text_height(fo->small_bold) + 1;
    fmt_num(v1, sizeof v1, s->peak);
    tnum(v2, sizeof v2, s->peak_t);
    fmt_num(v3, sizeof v3, (double)s->members);
    snprintf(buf, sizeof buf, "at its peak %s living (%s); %s born into it", v1, v2, v3);
    skin_text(fo->small, skin_fit(fo->small, buf, w, fit_, sizeof fit_), x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->small) + 1;
    tnum(v1, sizeof v1, s->born);
    const Species *root = syn_family_get(s->root);
    char rn[64];
    syn_family_label(root, rn, sizeof rn);
    if (s->parent)
        snprintf(buf, sizeof buf, "first seen %s, in #%d; %d step%s from %s", v1, s->founder, s->depth,
                 s->depth > 1 ? "s" : "", rn);
    else
        snprintf(buf, sizeof buf, "first seen %s, in #%d; a root of the tree", v1, s->founder);
    skin_text(fo->small, skin_fit(fo->small, buf, w, fit_, sizeof fit_), x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->small) + 1;
    family_line(s, buf, sizeof buf);
    skin_text(fo->small, skin_fit(fo->small, buf, w, fit_, sizeof fit_), x, y, TONE_TEXT, ALIGN_LEFT);
    y += skin_text_height(fo->small) + 1;
    int k = node_index(s->id);
    if (k >= 0 && V.nodes[k].ep0) {
        char ep[32];
        epochs_text(&V.nodes[k], ep, sizeof ep);
        snprintf(buf, sizeof buf, "THE BEST OF %s", ep);
        skin_text(fo->small_bold, buf, x, y, TONE_ACCENT, ALIGN_LEFT);
        y += skin_text_height(fo->small_bold) + 1;
    }
    y += 8;

    /* what to do with it */
    float bw = (w - 8) / 2;
    if (button(ui_rect(x, y, bw, 30), "COPY", false) && s->gc) {
        gc_assign(&a->clipboard, s->gc);
        interface_note(a, false, "Genetic code copied: paste it with Ctrl+V");
    }
    if (button(ui_rect(x + bw + 8, y, bw, 30), "PARENT", false) && s->parent)
        syn_family_focus(s->parent);
    y += 30 + 14;

    /* the way here, from its root */
    skin_text(fo->small_bold, "THE WAY HERE", x, y, TONE_MUTED, ALIGN_LEFT);
    y += skin_text_height(fo->small_bold) + 4;
    uint32_t chain[512];
    int nc = 0;
    for (const Species *p = s; p && nc < 512; p = syn_family_get(p->parent))
        chain[nc++] = p->id;
    float row = skin_text_height(fo->small) + 3;
    int room = (int)((r.y + r.h - 10 - y) / row);
    int first = nc - 1;
    if (nc > room && room > 1) {
        /* the root, then as many of the latest steps as fit */
        const Species *p = syn_family_get(chain[nc - 1]);
        char pn[64];
        syn_family_label(p, pn, sizeof pn);
        snprintf(buf, sizeof buf, "%s  the root", pn);
        skin_text(fo->small, buf, x, y, TONE_TEXT, ALIGN_LEFT);
        y += row;
        /* the root, this line and room - 2 steps shown */
        int skipped = nc - room + 1;
        snprintf(buf, sizeof buf, "... %d step%s ...", skipped, skipped > 1 ? "s" : "");
        skin_text(fo->small, buf, x, y, TONE_MUTED, ALIGN_LEFT);
        y += row;
        first = room - 3;
    }
    for (int i = first; i >= 0 && y + row <= r.y + r.h - 6; i--) {
        const Species *p = syn_family_get(chain[i]);
        if (!p)
            continue;
        char ch[180], pn[64];
        syn_family_change(p, ch, sizeof ch);
        syn_family_label(p, pn, sizeof pn);
        snprintf(buf, sizeof buf, "%s  %s", pn, p->parent ? ch : "the root");
        UIRect rr = {x - 4, y - 1, w + 8, row};
        bool h = over(rr);
        if (h)
            skin_row_hover(rr);
        if (h && uiin_clicked(rr))
            syn_family_focus(p->id);
        skin_text(fo->small, skin_fit(fo->small, buf, w, fit_, sizeof fit_), x, y,
                  p->id == s->id ? TONE_ACCENT : TONE_TEXT, ALIGN_LEFT);
        y += row;
    }
}

void syn_family_view(App *a, UIRect area, bool keys)
{
    const History *hi = syn_history();
    const Fonts *fo = shell_fonts();
    const Theme *th = skin_theme();
    float mxf, myf;
    uiin_mouse(&mxf, &myf);
    int mx = (int)mxf, my = (int)myf;
    static bool first = true;
    if (first) {
        /* screenshots: SYN3D_SHOT_FAMILY = living | best | notable;
         * SYN3D_SHOT_FOCUS follows the species with the most living */
        const char *e = getenv("SYN3D_SHOT_FAMILY");
        if (e)
            V.show = !strcmp(e, "living") ? SHOW_LIVING : !strcmp(e, "notable") ? SHOW_NOTABLE : SHOW_BEST;
        if (getenv("SYN3D_SHOT_FOCUS"))
            V.focus = syn_family_top(NULL);
        first = false;
    }

    UIRect c = area;
    UIRect cr = {0};
    if (area.w >= 760) {
        cr = ui_rect(area.x + area.w - CARD_W, area.y, CARD_W, area.h);
        c.w -= CARD_W + 14;
    }

    /* laid out again as the tree grows, or the choice changes */
    int span = syn_stats_epoch_span(hi);
    if (!V.laid || V.laid_version != syn_family_version() || V.laid_show != V.show || V.laid_focus != V.focus ||
        V.laid_span != span || V.laid_epochs != hi->nepochs) {
        layout(hi);
        V.laid = true;
        V.laid_version = syn_family_version();
        V.laid_show = V.show;
        V.laid_focus = V.focus;
        V.laid_span = span;
        V.laid_epochs = hi->nepochs;
    }
    if (V.fit) {
        fit(c, V.fit);
        V.fit = FIT_NONE;
    }
    int fk = node_index(V.focus);
    if (V.bring && fk >= 0) {
        V.cam_x = V.nodes[fk].x;
        V.cam_y = V.nodes[fk].y;
        if (V.zoom < 0.7f)
            V.zoom = 0.7f;
        V.bring = false;
    }
    for (int k = 0; k < V.n; k++)
        V.path[k] = false;
    for (int k = fk; k >= 0; k = V.nodes[k].up)
        V.path[k] = true;

    if (g_export) {
        g_export = false;
        export_tree(a, hi);
    }

    skin_fill(c, (Rgb){0, 0, 0}, th->light ? 0.06f : 0.35f);
    if (V.n == 0) {
        skin_text(fo->body, "No species to show yet: they are counted once a time unit.", c.x + 16, c.y + 16,
                  TONE_MUTED, ALIGN_LEFT);
        if (cr.w > 0)
            card(a, cr, hi);
        return;
    }

    /* moving and zooming */
    bool in = over(c);
    if (in) {
        float wh = uiin_wheel();
        if (wh != 0) {
            float gx = V.cam_x + ((float)mx - (c.x + c.w / 2)) / V.zoom;
            float gy = V.cam_y + ((float)my - (c.y + c.h / 2)) / V.zoom;
            V.zoom = fmaxf(0.04f, fminf(2.5f, V.zoom * powf(1.15f, wh)));
            V.cam_x = gx - ((float)mx - (c.x + c.w / 2)) / V.zoom;
            V.cam_y = gy - ((float)my - (c.y + c.h / 2)) / V.zoom;
        }
    }
    if (keys) {
        if (uiin_key(SDLK_HOME) || uiin_key(SDLK_f))
            fit(c, FIT_ALL);
        if (uiin_key(SDLK_PLUS) || uiin_key(SDLK_EQUALS) || uiin_key(SDLK_KP_PLUS))
            V.zoom = fminf(2.5f, V.zoom * 1.25f);
        if (uiin_key(SDLK_MINUS) || uiin_key(SDLK_KP_MINUS))
            V.zoom = fmaxf(0.04f, V.zoom / 1.25f);
        if (uiin_key(SDLK_BACKSPACE) && V.focus) {
            const Species *s = syn_family_get(V.focus);
            if (s && s->parent)
                syn_family_focus(s->parent);
        }
    }

    gl2d_clip_push(c.x, c.y, c.w, c.h);
    for (int k = 0; k < V.n; k++)
        if (V.nodes[k].up >= 0)
            edge(c, &V.nodes[V.nodes[k].up], &V.nodes[k], V.path[k]);
    float z = V.zoom;
    int hover = -1;
    for (int k = 0; k < V.n; k++) {
        const Node *nd = &V.nodes[k];
        const Species *s = species(nd);
        if (!s)
            continue;
        UIRect r = {sx(c, nd->x - NODE_W / 2), sy(c, nd->y - NODE_H / 2), NODE_W * z, NODE_H * z};
        if (r.x > c.x + c.w || r.y > c.y + c.h || r.x + r.w < c.x || r.y + r.h < c.y)
            continue;
        bool focus = s->id == V.focus;
        bool words = z >= TEXT_ZOOM;
        skin_fill(r, th->panel, 1);
        float tb = words ? TEXT_H : 0;
        float side = fminf(r.w - 12 * z, r.h - tb - 12 * z);
        UIRect b = {r.x + (r.w - side) / 2, r.y + 6 * z, side, side};
        body(b, s->gc, s->alive > 0 ? 255 : 150);
        if (nd->ep0 && z >= 0.3f) {
            char ep[32];
            epochs_text(nd, ep, sizeof ep);
            float ew = skin_text_width(fo->small_bold, ep) + 10;
            SDL_Color ac = skin_tone_color(TONE_ACCENT);
            skin_fill(ui_rect(r.x + 3, r.y + 3, ew, skin_text_height(fo->small_bold)), (Rgb){ac.r, ac.g, ac.b}, 1);
            skin_text(fo->small_bold, ep, r.x + 8, r.y + 3, TONE_ON_ACCENT, ALIGN_LEFT);
        }
        if (words) {
            char buf[96], v[32], fit_[96];
            float ty = b.y + b.h + 3;
            syn_family_label(s, buf, sizeof buf);
            skin_text(fo->bold, skin_fit(fo->bold, buf, r.w - 8, fit_, sizeof fit_), r.x + r.w / 2, ty,
                      focus ? TONE_ACCENT : TONE_TEXT, ALIGN_CENTER);
            ty += skin_text_height(fo->bold) - 2;
            if (s->alive > 0) {
                fmt_num(v, sizeof v, s->alive);
                snprintf(buf, sizeof buf, "%s living", v);
                skin_text(fo->small_bold, buf, r.x + r.w / 2, ty, TONE_GOOD, ALIGN_CENTER);
            } else {
                tnum(v, sizeof v, s->last_seen);
                snprintf(buf, sizeof buf, "gone by %s", v);
                skin_text(fo->small, buf, r.x + r.w / 2, ty, TONE_MUTED, ALIGN_CENTER);
            }
            ty += skin_text_height(fo->small) + 1;
            fmt_num(v, sizeof v, s->peak);
            snprintf(buf, sizeof buf, "peak %s", v);
            skin_text(fo->small, buf, r.x + r.w / 2, ty, TONE_MUTED, ALIGN_CENTER);
        } else if (z >= 0.3f) {
            char buf[64], fit_[64];
            syn_family_label(s, buf, sizeof buf);
            skin_text(fo->small, skin_fit(fo->small, buf, r.w - 4, fit_, sizeof fit_), r.x + r.w / 2,
                      r.y + r.h - skin_text_height(fo->small) - 2, focus ? TONE_ACCENT : TONE_TEXT, ALIGN_CENTER);
        }
        if (new_family(s) && z >= 0.3f) {
            /* where a family begins, at the box's other corner */
            const char *nf = "NEW FAMILY";
            float nw = skin_text_width(fo->small_bold, nf) + 10, nh = skin_text_height(fo->small_bold);
            /* under the epochs' mark if it has one */
            UIRect nb = {r.x + r.w - 3 - nw, r.y + 3 + (nd->ep0 ? nh + 2 : 0), nw, nh};
            skin_fill(nb, th->panel, 1);
            outline(nb, 1, th->accent, 1);
            skin_text(fo->small_bold, nf, nb.x + 5, nb.y, TONE_ACCENT, ALIGN_LEFT);
        }
        Rgb fc = focus || nd->ep0 ? th->accent : V.path[k] ? th->accent : th->border;
        outline(r, focus ? 3 : nd->ep0 || V.path[k] ? 2 : 1, fc, focus || nd->ep0 ? 1 : V.path[k] ? 0.6f : 0.8f);
        if (in && over(r)) {
            hover = k;
            skin_fill(r, th->accent, 0.10f);
        }
    }
    if (z >= TEXT_ZOOM)
        for (int k = 0; k < V.n; k++)
            if (V.nodes[k].up >= 0)
                edge_label(c, &V.nodes[V.nodes[k].up], &V.nodes[k]);
    gl2d_clip_pop();

    /* a click picks a species; the background drags */
    if (hover >= 0) {
        const Species *s = species(&V.nodes[hover]);
        char tip[400], plan[160], nm[64], fl[96];
        syn_family_plan(s->gc, plan, sizeof plan);
        syn_family_label(s, nm, sizeof nm);
        family_line(s, fl, sizeof fl);
        if (nm[0] == '#')
            snprintf(tip, sizeof tip, "Species %s, %s: %s. Click to follow it; double-click for its genes.", nm, fl,
                     plan);
        else
            snprintf(tip, sizeof tip, "%s (#%d), %s: %s. Click to follow it; double-click for its genes.", nm,
                     s->founder, fl, plan);
        uiin_tooltip(tip);
        if (uiin_pressed(c, 0)) {
            /* a second click on it soon after: the laboratory */
            if (s->id == V.click_id && SDL_GetTicks() - V.click_at < 450 && s->gc)
                lab_open(a, s->gc);
            V.click_id = s->id;
            V.click_at = SDL_GetTicks();
            V.focus = s->id;
        }
        if (uiin_right_clicked(c)) {
            V.focus = s->id;
            species_menu(a, s);
        }
    } else if (in && uiin_pressed(c, 0)) {
        V.drag = true;
        V.drag_mx = mx;
        V.drag_my = my;
        V.drag_cx = V.cam_x;
        V.drag_cy = V.cam_y;
    }
    if (V.drag) {
        if (uiin_mouse_down(0)) {
            V.cam_x = V.drag_cx - (float)(mx - V.drag_mx) / V.zoom;
            V.cam_y = V.drag_cy - (float)(my - V.drag_my) / V.zoom;
        } else {
            V.drag = false;
        }
    }

    if (cr.w > 0)
        card(a, cr, hi);
}

void syn_family_view_free(void)
{
    free(V.nodes);
    free(V.kid);
    free(V.path);
    V.nodes = NULL;
    V.kid = NULL;
    V.path = NULL;
    V.n = V.cap = V.kid_cap = 0;
    V.laid = false;
}
