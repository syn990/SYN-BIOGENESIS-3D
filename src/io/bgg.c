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

GeneticCode *bgg_load(const char *path, char *err, int errlen)
{
    char *text = read_file(path);
    if (!text) {
        snprintf(err, (size_t)errlen, "%s: cannot read", path);
        return NULL;
    }
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
        int c = tag_attr(g, "color", val, sizeof val) ? bio_color_from_name(val) : -1;
        if (c < 0 || c == COL_BROWN) {
            snprintf(err, (size_t)errlen, "%s: gene %d has colour \"%s\", not one of 0.9's", path,
                     n + 1, val);
            goto out;
        }
        ge->color = (uint8_t)c;
        n++;
    }
    if (n == 0) {
        snprintf(err, (size_t)errlen, "%s: no genes", path);
        goto out;
    }
    gc = gc_new_from_genes(genes, n, symmetry, mirror, disperse);
out:
    free(text);
    return gc;
}

bool bgg_save(const char *path, const GeneticCode *gc, char *err, int errlen)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
        return false;
    }
    bool bent = !gc_is_flat(gc);
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE genetic_code [\n"
          "<!ELEMENT genetic_code (gene+)>\n"
          "<!ATTLIST genetic_code symmetry (1|2|3|4|5|6|7|8) #REQUIRED>\n"
          "<!ATTLIST genetic_code mirror (yes|no) #REQUIRED>\n"
          "<!ATTLIST genetic_code disperse (yes|no) #REQUIRED>\n"
          "<!ELEMENT gene EMPTY>\n"
          "<!ATTLIST gene length CDATA #REQUIRED>\n"
          "<!ATTLIST gene theta CDATA #REQUIRED>\n",
          f);
    if (bent)
        fputs("<!ATTLIST gene pitch CDATA #IMPLIED>\n", f);
    fputs("<!ATTLIST gene color (red|green|blue|cyan|yellow|gray|white) #REQUIRED>\n"
          "]>\n",
          f);
    fprintf(f, "<genetic_code symmetry=\"%d\" mirror=\"%s\" disperse=\"%s\">\n", gc->symmetry,
            gc->mirror ? "yes" : "no", gc->disperse_children ? "yes" : "no");
    for (int i = 0; i < gc->ngenes; i++) {
        const Gene *g = &gc->genes[i];
        fprintf(f, "\t<gene length=\"%.17g\" theta=\"%.17g\"", g->length, g->theta);
        if (g->pitch != 0)
            fprintf(f, " pitch=\"%.17g\"", g->pitch);
        fprintf(f, " color=\"%s\" />\n", bio_color_name(g->color));
    }
    fputs("</genetic_code>\n", f);
    if (fclose(f) != 0) {
        snprintf(err, (size_t)errlen, "%s: cannot write", path);
        return false;
    }
    return true;
}
