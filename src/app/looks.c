/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Grown from SYN-BIOGENESIS's src/syn/looks.c, the SYN edition of its C
 * port of Biogenesis 0.9 (https://biogenesis.sourceforge.net/).
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

#include "app/looks.h"

#include <string.h>
#include <strings.h>

#include "sim/genetic_code.h"
#include "ui/shell.h"

static const char *const k_names[LOOK_COUNT] = {"Classic", "Biological", "Galaxy", "Digital (SYN-OS)"};
static const char *const k_keys[LOOK_COUNT] = {"classic", "biological", "galaxy", "digital"};

/* SYN-BIOGENESIS's palettes, for 0.9's colours in BioColor's order: green,
 * red, cyan, blue, white, gray, yellow, purple (Color Mod's, made for each
 * look as SYN-BIOGENESIS makes its Color Mod colours), brown */
static const unsigned char k_pal[LOOK_COUNT][COL_COUNT][3] = {
    [LOOK_BIOLOGICAL] = {{88, 200, 92},
                         {214, 64, 72},
                         {90, 210, 220},
                         {86, 112, 230},
                         {232, 228, 240},
                         {140, 146, 160},
                         {236, 196, 70},
                         {194, 62, 128},
                         {128, 92, 58}},
    [LOOK_GALAXY] = {{110, 255, 170},
                     {255, 90, 70},
                     {120, 220, 255},
                     {90, 120, 255},
                     {255, 250, 240},
                     {170, 140, 220},
                     {255, 210, 110},
                     {234, 108, 171},
                     {140, 90, 70}},
    /* SYN-OS: its reds, gold and grey, plus a green, cyan and blue for the
     * colours SYN-OS has no use for */
    [LOOK_DIGITAL] = {{72, 255, 96},
                      {255, 32, 32},
                      {32, 232, 255},
                      {74, 124, 255},
                      {240, 240, 240},
                      {170, 170, 170},
                      {224, 160, 32},
                      {222, 0, 111},
                      {138, 20, 20}},
};

static Look g_look = LOOK_CLASSIC;

Look look_current(void)
{
    return g_look;
}

void look_set(Look l)
{
    if (l < 0 || l >= LOOK_COUNT)
        return;
    g_look = l;
    shell_setting_set("look", k_keys[l]);
}

void look_load(void)
{
    const char *k = shell_setting("look");
    for (int l = 0; k && l < LOOK_COUNT; l++)
        if (!strcasecmp(k, k_keys[l]))
            g_look = (Look)l;
}

const char *look_name(Look l)
{
    return l >= 0 && l < LOOK_COUNT ? k_names[l] : "";
}

void look_rgb(Look l, int c, float out[3])
{
    if (c < 0 || c >= COL_COUNT)
        c = COL_WHITE;
    unsigned char r, g, b;
    if (l == LOOK_CLASSIC || l < 0 || l >= LOOK_COUNT) {
        bio_color_rgb(c, &r, &g, &b);
    } else {
        r = k_pal[l][c][0];
        g = k_pal[l][c][1];
        b = k_pal[l][c][2];
    }
    out[0] = r / 255.0f;
    out[1] = g / 255.0f;
    out[2] = b / 255.0f;
}

static void rgb3(float out[3], float r, float g, float b)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
}

void look_scene(Look l, const Theme *t, LookScene *s)
{
    memset(s, 0, sizeof *s);
    s->bloom = 1;
    rgb3(s->water, t->bg.r / 255.0f, t->bg.g / 255.0f, t->bg.b / 255.0f);
    rgb3(s->deep, s->water[0] * 0.6f, s->water[1] * 0.6f, s->water[2] * 0.6f);
    rgb3(s->line, t->border.r / 255.0f, t->border.g / 255.0f, t->border.b / 255.0f);
    rgb3(s->edge_rgb, t->accent.r / 255.0f, t->accent.g / 255.0f, t->accent.b / 255.0f);
    s->grid = 0.30f;
    s->edge = 0.65f;
    s->tissue = 0.45f;
    s->sss = 0.25f;
    switch (l) {
    case LOOK_CLASSIC:
        /* 0.9's: lines in its colours, still */
        s->lines = true;
        break;
    case LOOK_BIOLOGICAL:
        /* green-blue water lit from above, the box a faint tank: its floor
         * sediment, the surface rippling, light falling in shafts, motes
         * drifting down, and the bodies living tissue */
        rgb3(s->water, 0.050f, 0.170f, 0.190f);
        rgb3(s->deep, 0.006f, 0.028f, 0.040f);
        rgb3(s->line, 0.30f, 0.62f, 0.60f);
        rgb3(s->edge_rgb, 0.45f, 0.80f, 0.78f);
        s->grid = 0;
        s->edge = 0.12f;
        s->bloom = 0.9f;
        s->specks = true;
        s->tissue = 1;
        s->sss = 0.8f;
        s->gradient = s->floor = s->surface = true;
        s->caustics = 1;
        s->shafts = 1;
        rgb3(s->sediment[0], 0.22f, 0.21f, 0.16f);
        rgb3(s->sediment[1], 0.42f, 0.38f, 0.28f);
        break;
    case LOOK_GALAXY:
        /* the deep dark, the bodies glowing in it, stars far off */
        rgb3(s->water, 0.006f, 0.008f, 0.024f);
        rgb3(s->deep, 0.004f, 0.005f, 0.016f);
        rgb3(s->line, 0.45f, 0.40f, 0.75f);
        rgb3(s->edge_rgb, 0.55f, 0.50f, 0.90f);
        s->grid = 0;
        s->edge = 0.18f;
        s->glow = 0.16f;
        s->bloom = 1.6f;
        s->stars = true;
        s->tissue = 0.7f;
        s->sss = 0.45f;
        break;
    case LOOK_DIGITAL:
        /* SYN-OS: black, red grids on every wall */
        rgb3(s->water, 0.012f, 0.004f, 0.004f);
        rgb3(s->deep, 0.008f, 0.002f, 0.002f);
        rgb3(s->line, 0.70f, 0.08f, 0.08f);
        rgb3(s->edge_rgb, 1.0f, 0.15f, 0.15f);
        s->grid = 0.32f;
        s->edge = 0.95f;
        s->glow = 0.12f;
        s->bloom = 1.3f;
        s->walls = true;
        s->tissue = 0.35f;
        s->sss = 0.2f;
        break;
    default:
        break;
    }
}
