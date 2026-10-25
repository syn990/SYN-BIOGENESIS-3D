/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/arena_os.h, the SYN edition of its C port
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

/* What the arena (syn/arena.h) asks of the system, on Linux and on
 * Windows: its workers, this program started again; how many cores; a
 * clock; a file replaced by another at once.
 *
 * A worker's standard input is the pipe its jobs are written to, its
 * standard output the pipe its messages are read from, both in binary; it
 * shares the program's standard error, and is gone with the program, even
 * one killed. */
#ifndef SYN_ARENA_OS_H
#define SYN_ARENA_OS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    intptr_t id; /* the process: its pid, or on Windows its handle */
    int to;      /* the job pipe, written here */
    int from;    /* the message pipe, read here without waiting */
} ArenaProc;

/* syn-biogenesis-3d --arena-worker [--nice] */
bool arena_os_start(ArenaProc *p, bool nice, char *err, size_t n);
/* syn-biogenesis-3d ARGS: this program again with arguments of its own, on
 * the same pipes. */
bool arena_os_spawn(ArenaProc *p, const char *const *args, int nargs, char *err, size_t n);
/* In a program started by arena_os_spawn, before anything: its priority
 * lowered, made to end with the program that started it, and what it
 * prints sent a line at a time. */
void arena_os_child_begin(void);
/* What the worker has said so far, up to n bytes: how many; 0 when nothing
 * is waiting; -1 when it has gone (and all it said has been read). */
long arena_os_read(ArenaProc *p, void *buf, size_t n);
/* Ended at once, and its pipes closed. */
void arena_os_end(ArenaProc *p);
/* Until any of these pipes has something to read, or has closed, or ms
 * milliseconds have passed. */
void arena_os_wait(const int *from, int n, int ms);

/* In the worker, before anything: its priority lowered if nice, made to end
 * with the program that started it, its pipes made binary, and standard
 * output pointed at standard error so that nothing printed gets among the
 * messages. Returns where the messages go, or -1 if the worker should end. */
int arena_os_worker_begin(bool nice);

int arena_os_cores(void);
/* seconds, from no particular moment */
double arena_os_now(void);
/* from replaces to, at once (Windows's rename will not replace a file) */
bool arena_os_replace(const char *from, const char *to);

#endif
