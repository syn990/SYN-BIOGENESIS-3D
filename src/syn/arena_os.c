/* SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
 * Copyright (C) 2026 syn990
 *
 * Ported from SYN-BIOGENESIS's src/syn/arena_os.c, the SYN edition of its C port
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

#include "syn/arena_os.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

bool arena_os_start(ArenaProc *p, bool nice, char *err, size_t n)
{
    const char *args[] = {"--arena-worker", "--nice"};
    return arena_os_spawn(p, args, nice ? 2 : 1, err, n);
}

bool arena_os_spawn(ArenaProc *p, const char *const *args, int nargs, char *err, size_t n)
{
    /* a worker gone is seen when its pipe closes, not by a signal */
    signal(SIGPIPE, SIG_IGN);
    int job[2], msg[2];
    if (pipe(job) != 0) {
        snprintf(err, n, "no pipe: %s", strerror(errno));
        return false;
    }
    if (pipe(msg) != 0) {
        snprintf(err, n, "no pipe: %s", strerror(errno));
        close(job[0]);
        close(job[1]);
        return false;
    }
    int fds[4] = {job[0], job[1], msg[0], msg[1]};
    for (int i = 0; i < 4; i++)
        fcntl(fds[i], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, job[0], 0);
    posix_spawn_file_actions_adddup2(&fa, msg[1], 1);
    char *argv[34] = {"syn-biogenesis-3d"};
    for (int i = 0; i < nargs && i < 32; i++)
        argv[i + 1] = (char *)args[i];
    argv[(nargs < 32 ? nargs : 32) + 1] = NULL;
    pid_t pid;
    int rc = posix_spawn(&pid, "/proc/self/exe", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(job[0]);
    close(msg[1]);
    if (rc != 0) {
        snprintf(err, n, "the worker didn't start: %s", strerror(rc));
        close(job[1]);
        close(msg[0]);
        return false;
    }
    fcntl(msg[0], F_SETFL, O_NONBLOCK);
    p->id = pid;
    p->to = job[1];
    p->from = msg[0];
    return true;
}

long arena_os_read(ArenaProc *p, void *buf, size_t n)
{
    for (;;) {
        ssize_t m = read(p->from, buf, n);
        if (m > 0)
            return (long)m;
        if (m < 0 && errno == EINTR)
            continue;
        if (m < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        return -1;
    }
}

void arena_os_end(ArenaProc *p)
{
    kill((pid_t)p->id, SIGKILL);
    waitpid((pid_t)p->id, NULL, 0);
    close(p->to);
    close(p->from);
}

void arena_os_wait(const int *from, int n, int ms)
{
    struct pollfd pf[64];
    int np = 0;
    for (int i = 0; i < n && np < 64; i++)
        pf[np++] = (struct pollfd){.fd = from[i], .events = POLLIN};
    poll(pf, (nfds_t)np, ms);
}

void arena_os_child_begin(void)
{
    setpriority(PRIO_PROCESS, 0, 10);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
}

int arena_os_worker_begin(bool nice)
{
    if (nice)
        setpriority(PRIO_PROCESS, 0, 10);
    /* gone with the program that started it */
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1)
        return -1;
    int fd = dup(1);
    if (fd < 0)
        return -1;
    dup2(2, 1);
    return fd;
}

int arena_os_cores(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : n > 1024 ? 1024 : (int)n;
}

double arena_os_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

bool arena_os_replace(const char *from, const char *to)
{
    return rename(from, to) == 0;
}

