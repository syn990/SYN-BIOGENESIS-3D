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

/* The lexicon engine, sized for a species book. */
#include "guide/lexicon.h"

#include <stdio.h>
#include <string.h>

#define ID_LEN 48
#define PHRASE_LEN 384
#define PHRASE_MAX 4096
#define BANK_MAX 512
#define LINE_MAX_ 2048
/* template -> clause -> phrase -> word; a bank that names itself stops here */
#define MAX_DEPTH 4
#define VARS_MAX 16

typedef struct {
    char id[ID_LEN];
    int first; /* in phrases */
    int count;
} Bank;

static char phrases[PHRASE_MAX][PHRASE_LEN];
static int nphrases;
static Bank banks[BANK_MAX];
static int nbanks;

static char var_key[VARS_MAX][ID_LEN];
static char var_val[VARS_MAX][96];
static int nvars;

void lexicon_init(void)
{
    nphrases = nbanks = 0;
}

int lexicon_banks(void)
{
    return nbanks;
}

void lexicon_set_var(const char *key, const char *value)
{
    if (!key || !key[0])
        return;
    for (int i = 0; i < nvars; i++)
        if (!strcmp(var_key[i], key)) {
            if (!value || !value[0]) {
                nvars--;
                if (i != nvars) {
                    memcpy(var_key[i], var_key[nvars], sizeof var_key[i]);
                    memcpy(var_val[i], var_val[nvars], sizeof var_val[i]);
                }
                return;
            }
            snprintf(var_val[i], sizeof var_val[i], "%s", value);
            return;
        }
    if (!value || !value[0] || nvars >= VARS_MAX)
        return;
    snprintf(var_key[nvars], sizeof var_key[0], "%s", key);
    snprintf(var_val[nvars], sizeof var_val[0], "%s", value);
    nvars++;
}

void lexicon_clear_vars(void)
{
    nvars = 0;
}

/* ---- lookup: every bank of one id is one pool ---- */

static int pool_count(const char *id)
{
    int total = 0;
    for (int i = 0; i < nbanks; i++)
        if (!strcmp(banks[i].id, id))
            total += banks[i].count;
    return total;
}

static const char *pool_at(const char *id, int n)
{
    for (int i = 0; i < nbanks; i++) {
        if (strcmp(banks[i].id, id))
            continue;
        if (n < banks[i].count)
            return phrases[banks[i].first + n];
        n -= banks[i].count;
    }
    return NULL;
}

/* the tagged bank if it has anything, else the untagged one */
static int resolve(const char *id, const char *tag, char *out, size_t n)
{
    if (tag && tag[0]) {
        char t[ID_LEN * 2];
        snprintf(t, sizeof t, "%s.%s", id, tag);
        int c = pool_count(t);
        if (c > 0) {
            snprintf(out, n, "%s", t);
            return c;
        }
    }
    snprintf(out, n, "%s", id);
    return pool_count(id);
}

/* The scramble; the salt keeps "{word} and {word}" from repeating */
static unsigned lex_hash(unsigned seed, unsigned salt)
{
    unsigned x = seed ^ (salt * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

int lexicon_count(const char *id, const char *tag)
{
    char chosen[ID_LEN * 2];
    return id ? resolve(id, tag, chosen, sizeof chosen) : 0;
}

/* ---- expansion ---- */

static void append(char *out, size_t n, size_t *used, const char *s)
{
    while (s && *s && *used + 1 < n)
        out[(*used)++] = *s++;
    out[*used] = '\0';
}

static void expand(char *out, size_t n, size_t *used, const char *src, const char *tag, unsigned seed,
                   int depth, unsigned *occurrence)
{
    for (const char *p = src; p && *p && *used + 1 < n;) {
        if (*p != '{') {
            out[(*used)++] = *p++;
            out[*used] = '\0';
            continue;
        }
        const char *close = strchr(p, '}');
        if (!close) {
            append(out, n, used, p);
            return;
        }
        size_t len = (size_t)(close - p - 1);
        if (len == 0 || len >= ID_LEN) {
            p = close + 1;
            continue;
        }
        char ref[ID_LEN];
        memcpy(ref, p + 1, len);
        ref[len] = '\0';
        p = close + 1;
        /* a bound variable outranks a bank of its name */
        int v;
        for (v = 0; v < nvars && strcmp(var_key[v], ref); v++)
            ;
        if (v < nvars) {
            append(out, n, used, var_val[v]);
            continue;
        }
        if (depth >= MAX_DEPTH)
            continue;
        char chosen[ID_LEN * 2];
        int c = resolve(ref, tag, chosen, sizeof chosen);
        if (c <= 0)
            continue;
        unsigned salt = (*occurrence)++;
        const char *ph = pool_at(chosen, (int)(lex_hash(seed, salt + 1u) % (unsigned)c));
        expand(out, n, used, ph, tag, lex_hash(seed, salt + 977u), depth + 1, occurrence);
    }
}

void lexicon_compose(char *out, size_t n, const char *id, const char *tag, unsigned seed)
{
    if (!out || !n)
        return;
    out[0] = '\0';
    if (!id)
        return;
    char chosen[ID_LEN * 2];
    int c = resolve(id, tag, chosen, sizeof chosen);
    if (c <= 0)
        return;
    size_t used = 0;
    unsigned occurrence = 0;
    expand(out, n, &used, pool_at(chosen, (int)(lex_hash(seed, 0u) % (unsigned)c)), tag, seed, 0,
           &occurrence);
}

/* ---- loading ---- */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
        e--;
    *e = '\0';
    return s;
}

int lexicon_load(const char *path)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) {
        fprintf(stderr, "lexicon: %s: can't open it\n", path ? path : "(no file)");
        return 1;
    }
    char buf[LINE_MAX_];
    int line = 0, problems = 0;
    Bank *b = NULL;
    while (fgets(buf, sizeof buf, f)) {
        line++;
        char *s = trim(buf);
        if (!s[0] || s[0] == '#')
            continue;
        if (!strcmp(s, "[bank]")) {
            if (nbanks >= BANK_MAX) {
                fprintf(stderr, "lexicon: %s:%d: more than %d banks\n", path, line, BANK_MAX);
                problems++;
                b = NULL;
                continue;
            }
            b = &banks[nbanks++];
            b->id[0] = '\0';
            b->first = nphrases;
            b->count = 0;
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || !b) {
            fprintf(stderr, "lexicon: %s:%d: %s\n", path, line, !eq ? "no '='" : "a field before any [bank]");
            problems++;
            continue;
        }
        *eq = '\0';
        char *key = trim(s), *value = trim(eq + 1);
        if (!strcmp(key, "id")) {
            snprintf(b->id, sizeof b->id, "%s", value);
        } else if (!strcmp(key, "words")) {
            if (!b->id[0] || b->first + b->count != nphrases) {
                fprintf(stderr, "lexicon: %s:%d: words %s\n", path, line,
                        !b->id[0] ? "before the bank's id" : "for a bank already closed");
                problems++;
                continue;
            }
            for (char *c = value; c && *c;) {
                char *sep = strchr(c, '|');
                if (sep)
                    *sep = '\0';
                char *ph = trim(c);
                if (*ph) {
                    if (nphrases >= PHRASE_MAX) {
                        fprintf(stderr, "lexicon: %s:%d: more than %d phrases\n", path, line, PHRASE_MAX);
                        problems++;
                        break;
                    }
                    if (strlen(ph) >= PHRASE_LEN) {
                        fprintf(stderr, "lexicon: %s:%d: a phrase longer than %d\n", path, line, PHRASE_LEN - 1);
                        problems++;
                    }
                    snprintf(phrases[nphrases], PHRASE_LEN, "%s", strcmp(ph, "~") ? ph : "");
                    nphrases++;
                    b->count++;
                }
                c = sep ? sep + 1 : NULL;
            }
        } else {
            fprintf(stderr, "lexicon: %s:%d: what is '%s'?\n", path, line, key);
            problems++;
        }
    }
    fclose(f);
    return problems;
}
