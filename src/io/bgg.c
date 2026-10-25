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

#include "io/bgg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/params.h"

#define BGG_MAX_GENES 100

/* The value of attribute name inside the tag starting at tag (up to its
 * '>'), copied into out. False if the tag has no such attribute. */
static bool tag_attr(const char *tag, const char *name, char *out, size_t outlen)
{
    const char *end = strchr(tag, '>');
    size_t nl = strlen(name);
    for (const char *p = tag; p && (!end || p < end); p++) {
        p = strstr(p, name);
        if (!p || (end && p >= end))
            return false;
        if ((p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\n' && p[-1] != '\r') || p[nl] != '=')
            continue;
        char q = p[nl + 1];
        if (q != '"' && q != '\'')
            continue;
        const char *v = p + nl + 2;
        const char *ve = strchr(v, q);
        if (!ve)
            return false;
        size_t n = (size_t)(ve - v);
        if (n >= outlen)
            n = outlen - 1;
        memcpy(out, v, n);
        out[n] = 0;
        return true;
    }
    return false;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 4096 + 1 > cap) {
            cap = cap ? cap * 2 : 8192;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        size_t got = fread(buf + len, 1, 4096, f);
        len += got;
        if (got < 4096)
            break;
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

/* Color Mod 2.01's gene colours, each as the colour here that does its
 * job (SYN-BIOGENESIS's field guide says what each does) */
static const struct {
    const char *name;
    uint8_t color;
} k_colour_mod[] = {
    /* leaves of every kind */
    {"forest", COL_GREEN}, {"spring", COL_GREEN}, {"summer", COL_GREEN}, {"lime", COL_GREEN},
    {"leaf", COL_GREEN}, {"c4", COL_GREEN}, {"jade", COL_GREEN}, {"grass", COL_GREEN}, {"bark", COL_GREEN},
    /* eaters: fire, herbivores, decomposers, parasites */
    {"fire", COL_RED}, {"orange", COL_RED}, {"maroon", COL_RED}, {"pink", COL_RED}, {"cream", COL_RED},
    /* killers */
    {"spike", COL_GRAY}, {"lilac", COL_GRAY}, {"violet", COL_GRAY}, {"olive", COL_GRAY}, {"fallow", COL_GRAY},
    /* shields, pushers, immune systems, healers, long life, mimics */
    {"sky", COL_BLUE}, {"ochre", COL_BLUE}, {"mint", COL_BLUE}, {"lavender", COL_BLUE}, {"magenta", COL_BLUE},
    {"rose", COL_BLUE}, {"indigo", COL_BLUE}, {"gold", COL_BLUE}, {"dark", COL_BLUE},
    /* viruses and transformers */
    {"plague", COL_WHITE}, {"coral", COL_WHITE},
    /* movers and the eyes that steer them */
    {"teal", COL_CYAN}, {"eye", COL_CYAN},
    /* more children */
    {"silver", COL_YELLOW}, {"spore", COL_YELLOW}, {"auburn", COL_YELLOW}, {"blond", COL_YELLOW},
    {"flower", COL_YELLOW},
    /* its other methane eater */
    {"darkgray", COL_PURPLE},
};

static void add_note(char *out, size_t n, const char *fmt, const char *a)
{
    size_t k = strlen(out);
    if (k < n)
        snprintf(out + k, n - k, fmt, a);
}

/* text: NUL-terminated; path names it in the messages */
static GeneticCode *parse(const char *text, const char *path, BggInfo *info, char *err, int errlen)
{
    BggInfo own;
    if (!info)
        info = &own;
    memset(info, 0, sizeof *info);
    /* Color Mod's colours met, for the note */
    char seen[12][16];
    uint8_t seen_as[12];
    int nseen = 0;
    GeneticCode *gc = NULL;
    Gene genes[BGG_MAX_GENES];
    int n = 0;
    char val[64];
    const char *root = strstr(text, "<genetic_code ");
    if (!root) {
        snprintf(err, (size_t)errlen, "%s: no <genetic_code> element", path);
        goto out;
    }
    int symmetry = tag_attr(root, "symmetry", val, sizeof val) ? atoi(val) : 0;
    if (symmetry < 1 || symmetry > 8) {
        snprintf(err, (size_t)errlen, "%s: symmetry must be 1 to 8", path);
        goto out;
    }
    int mirror = tag_attr(root, "mirror", val, sizeof val) && strcmp(val, "yes") == 0;
    bool disperse = tag_attr(root, "disperse", val, sizeof val) && strcmp(val, "yes") == 0;
    bool colour_mod = tag_attr(root, "mutationrate", val, sizeof val);
    double drive[DRIVE_COUNT] = {0};
    bool has_drives = false, has_own = false;
    for (int d = 0; d < DRIVE_COUNT; d++) {
        if (!tag_attr(root, gc_drive_name(d), val, sizeof val))
            continue;
        has_drives = has_own = true;
        drive[d] = strtod(val, NULL);
        if (!(fabs(drive[d]) <= 1)) {
            snprintf(err, (size_t)errlen, "%s: the drive %s must be -1 to 1", path,
                     gc_drive_name(d));
            goto out;
        }
    }
    for (const char *g = strstr(root, "<gene "); g; g = strstr(g + 1, "<gene ")) {
        if (n == BGG_MAX_GENES) {
            snprintf(err, (size_t)errlen, "%s: more than %d genes", path, BGG_MAX_GENES);
            goto out;
        }
        Gene *ge = &genes[n];
        memset(ge, 0, sizeof *ge);
        if (!tag_attr(g, "length", val, sizeof val)) {
            snprintf(err, (size_t)errlen, "%s: gene %d has no length", path, n + 1);
            goto out;
        }
        ge->length = strtod(val, NULL);
        if (!tag_attr(g, "theta", val, sizeof val)) {
            snprintf(err, (size_t)errlen, "%s: gene %d has no theta", path, n + 1);
            goto out;
        }
        ge->theta = strtod(val, NULL);
        ge->pitch = tag_attr(g, "pitch", val, sizeof val) ? strtod(val, NULL) : 0;
        has_own = has_own || ge->pitch != 0;
        /* Color Mod's branches: a gene drawn from an earlier segment, not
         * the one before; here every arm is one chain */
        if (tag_attr(g, "branch", val, sizeof val) && atoi(val) >= 0)
            info->straightened++;
        /* 0.9's random genes are 2 to 18 long; the laboratory allows more */
        if (!(ge->length > 0 && ge->length <= 1000) || !isfinite(ge->theta) ||
            !(fabs(ge->pitch) <= 1.5707963267948966)) {
            snprintf(err, (size_t)errlen,
                     "%s: gene %d needs a length above 0 and at most 1000, a finite theta "
                     "and a pitch within +-pi/2",
                     path, n + 1);
            goto out;
        }
        val[0] = 0;
        if (!tag_attr(g, "color", val, sizeof val)) {
            snprintf(err, (size_t)errlen, "%s: gene %d has no colour", path, n + 1);
            goto out;
        }
        int c = bio_color_from_name(val);
        if (c < 0 || c == COL_BROWN) {
            /* another Biogenesis's colour: the one here that does its job,
             * or a leaf if none is known */
            c = COL_GREEN;
            for (size_t k = 0; k < sizeof k_colour_mod / sizeof k_colour_mod[0]; k++)
                if (!strcmp(val, k_colour_mod[k].name))
                    c = k_colour_mod[k].color;
            info->converted++;
            colour_mod = true;
            bool known = false;
            for (int k = 0; k < nseen && !known; k++)
                known = !strcmp(seen[k], val);
            if (!known && nseen < 12) {
                snprintf(seen[nseen], sizeof seen[0], "%.15s", val);
                seen_as[nseen++] = (uint8_t)c;
            }
        }
        ge->color = (uint8_t)c;
        n++;
    }
    if (n == 0) {
        snprintf(err, (size_t)errlen, "%s: no genes", path);
        goto out;
    }
    gc = gc_new_from_genes(genes, n, symmetry, mirror, disperse);
    memcpy(gc->drive, drive, sizeof drive);
    if (!has_drives || !gc_steers(gc)) {
        gc_instincts(gc); /* a 0.9 or Color Mod being */
        info->drives = true;
    }
    /* its height: read, or kept to where its light drive's light was (an
     * earlier version of this program's), or from its body */
    if (tag_attr(root, "height", val, sizeof val)) {
        gc->height = bio_between(strtod(val, NULL), 0, 1);
        has_own = true;
    } else if (has_drives) {
        gc->height = gc_height_of_light_drive(drive[DRIVE_LIGHT], P.world_depth, P.light_depth);
        info->height = true;
    } else {
        gc->height = gc_body_height(gc);
        info->height = true;
    }
    if (tag_attr(root, "plan", val, sizeof val)) {
        for (int k = 0; k < PLAN_COUNT; k++)
            if (!strcmp(val, gc_plan_name(k)))
                gc->plan = (uint8_t)k;
        has_own = true;
    }
    if (tag_attr(root, "cone", val, sizeof val))
        gc->cone = bio_between(strtod(val, NULL), -1.5707963267948966, 1.5707963267948966);
    snprintf(info->from, sizeof info->from, "%s",
             colour_mod ? "Color Mod 2.01" : has_own ? "SYN-BIOGENESIS-3D" : "Biogenesis 0.9");
    if (info->converted) {
        add_note(info->note, sizeof info->note, "%s: colours given this program's", info->from);
        for (int k = 0; k < nseen; k++) {
            char one[64];
            snprintf(one, sizeof one, "%s%.15s as %s", k ? ", " : " (", seen[k], bio_color_name(seen_as[k]));
            add_note(info->note, sizeof info->note, "%s", one);
        }
        add_note(info->note, sizeof info->note, "%s", ")");
    }
    if (info->straightened) {
        char b2[64];
        snprintf(b2, sizeof b2, "%d branch%s straightened", info->straightened, info->straightened == 1 ? "" : "es");
        add_note(info->note, sizeof info->note, info->note[0] ? "; %s" : "%s", b2);
    }
    if (info->height || info->drives)
        add_note(info->note, sizeof info->note, info->note[0] ? "; %s" : "%s",
                 info->height && info->drives ? "its height and drives worked out from its body"
                 : info->height            ? "its height worked out"
                                           : "its drives worked out from its body");
out:
    return gc;
}

GeneticCode *bgg_load_info(const char *path, BggInfo *info, char *err, int errlen)
{
    char *text = read_file(path);
    if (!text) {
        snprintf(err, (size_t)errlen, "%s: cannot read", path);
        return NULL;
    }
    GeneticCode *gc = parse(text, path, info, err, errlen);
    free(text);
    return gc;
}

GeneticCode *bgg_parse_info(const void *bytes, size_t n, BggInfo *info, char *err, int errlen)
{
    char *text = malloc(n + 1);
    if (!text) {
        snprintf(err, (size_t)errlen, "out of memory");
        return NULL;
    }
    memcpy(text, bytes, n);
    text[n] = 0;
    GeneticCode *gc = parse(text, "the code", info, err, errlen);
    free(text);
    return gc;
}

GeneticCode *bgg_load(const char *path, char *err, int errlen)
{
    return bgg_load_info(path, NULL, err, errlen);
}

GeneticCode *bgg_parse(const void *bytes, size_t n, char *err, int errlen)
{
    return bgg_parse_info(bytes, n, NULL, err, errlen);
}

void bgg_write(FILE *f, const GeneticCode *gc)
{
    bool bent = !gc_is_flat(gc), steers = gc_steers(gc);
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE genetic_code [\n"
          "<!ELEMENT genetic_code (gene+)>\n"
          "<!ATTLIST genetic_code symmetry (1|2|3|4|5|6|7|8) #REQUIRED>\n"
          "<!ATTLIST genetic_code mirror (yes|no) #REQUIRED>\n"
          "<!ATTLIST genetic_code disperse (yes|no) #REQUIRED>\n",
          f);
    if (steers)
        for (int d = 0; d < DRIVE_COUNT; d++)
            fprintf(f, "<!ATTLIST genetic_code %s CDATA #IMPLIED>\n", gc_drive_name(d));
    fputs("<!ATTLIST genetic_code height CDATA #IMPLIED>\n", f);
    if (gc->plan != PLAN_DISK)
        fputs("<!ATTLIST genetic_code plan (disk|cone|orb) #IMPLIED>\n"
              "<!ATTLIST genetic_code cone CDATA #IMPLIED>\n",
              f);
    fputs("<!ELEMENT gene EMPTY>\n"
          "<!ATTLIST gene length CDATA #REQUIRED>\n"
          "<!ATTLIST gene theta CDATA #REQUIRED>\n",
          f);
    if (bent)
        fputs("<!ATTLIST gene pitch CDATA #IMPLIED>\n", f);
    fputs("<!ATTLIST gene color (red|green|blue|cyan|yellow|gray|white|purple) #REQUIRED>\n"
          "]>\n",
          f);
    fprintf(f, "<genetic_code symmetry=\"%d\" mirror=\"%s\" disperse=\"%s\"", gc->symmetry,
            gc->mirror ? "yes" : "no", gc->disperse_children ? "yes" : "no");
    if (steers)
        for (int d = 0; d < DRIVE_COUNT; d++)
            fprintf(f, " %s=\"%.17g\"", gc_drive_name(d), gc->drive[d]);
    fprintf(f, " height=\"%.17g\"", gc->height);
    if (gc->plan != PLAN_DISK)
        fprintf(f, " plan=\"%s\" cone=\"%.17g\"", gc_plan_name(gc->plan), gc->cone);
    fputs(">\n", f);
    for (int i = 0; i < gc->ngenes; i++) {
        const Gene *g = &gc->genes[i];
        fprintf(f, "\t<gene length=\"%.17g\" theta=\"%.17g\"", g->length, g->theta);
        if (g->pitch != 0)
            fprintf(f, " pitch=\"%.17g\"", g->pitch);
        fprintf(f, " color=\"%s\" />\n", bio_color_name(g->color));
    }
    fputs("</genetic_code>\n", f);
}

bool bgg_save(const char *path, const GeneticCode *gc, char *err, int errlen)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
        return false;
    }
    bgg_write(f, gc);
    if (fclose(f) != 0) {
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
        return false;
    }
    return true;
}

uint8_t *bgg_bytes(const GeneticCode *gc, size_t *n)
{
    char *p = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&p, &len);
    if (!f)
        return NULL;
    bgg_write(f, gc);
    if (fclose(f) != 0) {
        free(p);
        return NULL;
    }
    *n = len;
    return (uint8_t *)p;
}
