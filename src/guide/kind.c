/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Part of the SYN edition of SYN-BIOGENESIS, a C port of Biogenesis
 * (https://biogenesis.sourceforge.net/).
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

#include "guide/kind.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "guide/lexicon.h"
#include "guide/palette.h"

/* ui/gfx.h's, without SDL's headers: where the assets are */
const char *gfx_asset(const char *rel);

static const struct {
    const char *name, *tag, *meaning;
} k_kinds[KIND_COUNT] = {
    {"Plant", "plant", "Lives on light and air, and little else."},
    {"Drifter", "drifter", "A plant that moves."},
    {"Fortress", "fortress", "A plant behind a quarter or more of shield."},
    {"Nettle", "nettle", "A plant with parts that kill or poison."},
    {"Blight", "blight", "A plant that infects what it touches."},
    {"Half-plant", "mixed", "Both leaves and eating parts: a plant that also eats."},
    {"Grazer", "grazer", "Eats plants (maroon)."},
    {"Hunter", "hunter", "Eats other eaters (red)."},
    {"Omnivore", "omnivore", "Eats almost anything (orange)."},
    {"Raider", "raider", "Bites eaters hard and everything else lightly (fire)."},
    {"Scavenger", "scavenger", "Eats corpses, the weak and viruses (pink)."},
    {"Parasite", "parasite", "Small bites, hard to infect (cream)."},
    {"Predator", "predator", "Bites anything blue doesn't shield (red)."},
    {"Virus", "virus", "Can't feed; spreads by making others give birth to it."},
    {"Killer", "killer", "Built to kill, with no leaves or mouth."},
    {"Husk", "husk", "No leaves, mouth or weapon: lives on what it was born with."},
};

const char *syn_kind_name(Kind k)
{
    return k >= 0 && k < KIND_COUNT ? k_kinds[k].name : "";
}

const char *syn_kind_meaning(Kind k)
{
    return k >= 0 && k < KIND_COUNT ? k_kinds[k].meaning : "";
}

/* ---- reading the code ---- */

typedef struct {
    double role[ROLE_COUNT]; /* length per arm, by job */
    double total;
    double by_slot[SYN_NCOL];
} Parts;

static void parts(const GeneticCode *gc, Parts *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < syn_genes(gc); i++) {
        int s = syn_gene_col(gc, i);
        double l = syn_gene_len(gc, i);
        p->role[syn_col_role(s)] += l;
        if (s >= 0 && s < SYN_NCOL)
            p->by_slot[s] += l;
        p->total += l;
    }
}


static Kind eater_kind(const Parts *p)
{
    (void)p;
    return KIND_PREDATOR;
}

static Kind classify(const Parts *p)
{
    double photo = p->role[ROLE_PHOTO], eat = p->role[ROLE_EAT];
    if (photo > 0 && eat > 0)
        return KIND_MIXED;
    if (eat > 0)
        return eater_kind(p);
    if (photo > 0) {
        if (p->role[ROLE_INFECT] > 0)
            return KIND_BLIGHT;
        if (p->role[ROLE_KILL] > 0)
            return KIND_NETTLE;
        if (p->role[ROLE_DEFEND] >= 0.25 * p->total)
            return KIND_FORTRESS;
        if (p->role[ROLE_MOVE] > 0)
            return KIND_DRIFTER;
        return KIND_PLANT;
    }
    if (p->role[ROLE_INFECT] > 0)
        return KIND_VIRUS;
    if (p->role[ROLE_KILL] > 0)
        return KIND_KILLER;
    return KIND_HUSK;
}

Kind syn_kind(const GeneticCode *gc, char *traits, size_t n)
{
    Parts p;
    parts(gc, &p);
    Kind k = classify(&p);
    if (!traits || !n)
        return k;
    /* the traits the kind doesn't already say, biggest first */
    struct {
        const char *word;
        double len;
        bool shown;
    } t[] = {
        {"armoured", p.role[ROLE_DEFEND], k != KIND_FORTRESS},
        {"mobile", p.role[ROLE_MOVE], k != KIND_DRIFTER},
        {"killing", p.role[ROLE_KILL], k != KIND_NETTLE && k != KIND_KILLER},
        {"infectious", p.role[ROLE_INFECT], k != KIND_BLIGHT && k != KIND_VIRUS},
        {"healing", p.role[ROLE_HEAL], true},
        {"fertile", p.role[ROLE_REPRO], true},
        {"sighted", p.role[ROLE_SENSE], true},
    };
    int nt = (int)(sizeof t / sizeof *t);
    traits[0] = '\0';
    size_t used = 0;
    for (int shown = 0; shown < 3; shown++) {
        int best = -1;
        for (int i = 0; i < nt; i++)
            if (t[i].shown && t[i].len > 0 && (best < 0 || t[i].len > t[best].len))
                best = i;
        if (best < 0)
            break;
        used += (size_t)snprintf(traits + used, used < n ? n - used : 0, "%s%s", shown ? ", " : "",
                                 t[best].word);
        t[best].shown = false;
        if (used >= n)
            break;
    }
    return k;
}

void syn_kind_label(const GeneticCode *gc, char *out, size_t n)
{
    char traits[96];
    Kind k = syn_kind(gc, traits, sizeof traits);
    snprintf(out, n, "%s%s%s", syn_kind_name(k), traits[0] ? " \xc2\xb7 " : "", traits);
}

/* ---- the description ---- */

static bool book_tried;

static void book(void)
{
    if (book_tried)
        return;
    book_tried = true;
    lexicon_init();
    const char *path = gfx_asset("lexicon/species.txt");
    if (path)
        lexicon_load(path);
}

/* FNV-1a over what the code is */
static unsigned code_seed(const GeneticCode *gc)
{
    uint32_t h = 2166136261u;
#define MIX(v)                                                                                         \
    do {                                                                                               \
        uint32_t x_ = (uint32_t)(v);                                                                   \
        for (int b_ = 0; b_ < 4; b_++, x_ >>= 8) {                                                     \
            h ^= x_ & 0xffu;                                                                           \
            h *= 16777619u;                                                                            \
        }                                                                                              \
    } while (0)
    MIX(gc->symmetry);
    MIX(gc->mirror);
    for (int i = 0; i < syn_genes(gc); i++) {
        MIX(syn_gene_col(gc, i));
        MIX((int)(syn_gene_len(gc, i) * 10 + 0.5));
    }
#undef MIX
    return h;
}

static const char *colour_word(int slot)
{
    const char *n = syn_col_name(slot);
    return !strcmp(n, "c4plant") ? "C4" : n;
}

void syn_kind_describe(const GeneticCode *gc, char *out, size_t n)
{
    Parts p;
    parts(gc, &p);
    Kind k = classify(&p);
    double body = p.total * gc->symmetry;
    const char *size = body < 60 ? "tiny" : body < 150 ? "small" : body < 350 ? "mid-sized" : body < 800 ? "large" : "huge";
    static const char *const arms[] = {"one-armed",  "two-armed",   "three-armed", "four-armed",
                                       "five-armed", "six-armed",   "seven-armed", "eight-armed"};
    const char *arm = gc->symmetry >= 1 && gc->symmetry <= 8 ? arms[gc->symmetry - 1] : "many-armed";
    /* its main colours: the longest, and the next if it is a good part */
    int a = -1, b = -1;
    for (int s = 0; s < SYN_NCOL; s++) {
        if (p.by_slot[s] <= 0)
            continue;
        if (a < 0 || p.by_slot[s] > p.by_slot[a]) {
            b = a;
            a = s;
        } else if (b < 0 || p.by_slot[s] > p.by_slot[b]) {
            b = s;
        }
    }
    char main[96] = "", genes[32], Size[16];
    if (a >= 0 && (b < 0 || p.by_slot[b] < 0.2 * p.total))
        snprintf(main, sizeof main, "%s%s", p.by_slot[a] >= 0.95 * p.total ? "all " : "mostly ", colour_word(a));
    else if (a >= 0)
        snprintf(main, sizeof main, "%s and %s", colour_word(a), colour_word(b));
    int ng = syn_genes(gc);
    snprintf(genes, sizeof genes, ng == 1 ? "one gene" : "%d genes", ng);
    snprintf(Size, sizeof Size, "%s", size);
    Size[0] = (char)toupper((unsigned char)Size[0]);

    book();
    lexicon_clear_vars();
    lexicon_set_var("size", size);
    lexicon_set_var("Size", Size);
    lexicon_set_var("arms", arm);
    lexicon_set_var("mirror", gc->mirror ? "mirrored, " : "");
    lexicon_set_var("genes", genes);
    lexicon_set_var("main", main);
    char noun[32];
    snprintf(noun, sizeof noun, "%s", syn_kind_name(k));
    noun[0] = (char)tolower((unsigned char)noun[0]);
    lexicon_set_var("kind", noun);
    lexicon_compose(out, n, "species", k_kinds[k].tag, code_seed(gc));
    lexicon_clear_vars();
    /* an empty closing leaves a space */
    for (size_t e = strlen(out); e > 0 && out[e - 1] == ' '; e--)
        out[e - 1] = '\0';
    if (!out[0])
        snprintf(out, n, "%s, %s%s %s, %s.", Size, gc->mirror ? "mirrored, " : "", arm, noun, main);
}
