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

#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <wchar.h>

/* The workers are in it, and it closes with the program however that ends,
 * taking them with it. */
static HANDLE g_job;

static void job_make(void)
{
    if (g_job)
        return;
    g_job = CreateJobObjectW(NULL, NULL);
    if (!g_job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    memset(&li, 0, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof li)) {
        CloseHandle(g_job);
        g_job = NULL;
    }
}

static void fail(char *err, size_t n, const char *what)
{
    snprintf(err, n, "%s (Windows error %lu)", what, (unsigned long)GetLastError());
}

/* an argument for the command line as CommandLineToArgvW reads it back:
 * quoted if it has to be, backslashes before a quote doubled */
static bool quote_arg(wchar_t *cmd, size_t *k, size_t cap, const char *arg)
{
    static wchar_t w[8192];
    if (!MultiByteToWideChar(CP_UTF8, 0, arg, -1, w, 8192))
        return false;
    bool plain = w[0] && !wcspbrk(w, L" \t\"");
    if (*k + 2 * wcslen(w) + 4 >= cap)
        return false;
    cmd[(*k)++] = L' ';
    if (plain) {
        for (const wchar_t *c = w; *c; c++)
            cmd[(*k)++] = *c;
        return true;
    }
    cmd[(*k)++] = L'"';
    for (const wchar_t *c = w;; c++) {
        size_t bs = 0;
        while (*c == L'\\') {
            bs++;
            c++;
        }
        if (!*c) {
            for (size_t i = 0; i < 2 * bs; i++)
                cmd[(*k)++] = L'\\';
            break;
        }
        for (size_t i = 0; i < (*c == L'"' ? 2 * bs + 1 : bs); i++)
            cmd[(*k)++] = L'\\';
        cmd[(*k)++] = *c;
    }
    cmd[(*k)++] = L'"';
    return true;
}

bool arena_os_start(ArenaProc *p, bool nice, char *err, size_t n)
{
    const char *args[] = {"--arena-worker", "--nice"};
    return arena_os_spawn(p, args, nice ? 2 : 1, err, n);
}

bool arena_os_spawn(ArenaProc *p, const char *const *args, int nargs, char *err, size_t n)
{
    static wchar_t exe[32768], cmd[65536];
    DWORD len = GetModuleFileNameW(NULL, exe, 32768);
    if (!len || len >= 32768) {
        fail(err, n, "this program's path is unknown");
        return false;
    }
    /* "exe" ARGS */
    size_t k = 0;
    cmd[k++] = L'"';
    for (DWORD i = 0; i < len; i++)
        cmd[k++] = exe[i];
    cmd[k++] = L'"';
    for (int i = 0; i < nargs; i++)
        if (!quote_arg(cmd, &k, sizeof cmd / sizeof *cmd, args[i])) {
            snprintf(err, n, "an argument too long: %s", args[i]);
            return false;
        }
    cmd[k] = 0;

    /* the worker's ends inherited, these not */
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE job_r, job_w, msg_r, msg_w;
    if (!CreatePipe(&job_r, &job_w, &sa, 1 << 16)) {
        fail(err, n, "no pipe");
        return false;
    }
    if (!CreatePipe(&msg_r, &msg_w, &sa, 1 << 20)) {
        fail(err, n, "no pipe");
        CloseHandle(job_r);
        CloseHandle(job_w);
        return false;
    }
    SetHandleInformation(job_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(msg_r, HANDLE_FLAG_INHERIT, 0);
    /* standard error shared, if there is one (a window program started
     * from Explorer has none) */
    HANDLE errh = NULL;
    intptr_t e = _get_osfhandle(2);
    if (e != -1 && e != -2)
        DuplicateHandle(GetCurrentProcess(), (HANDLE)e, GetCurrentProcess(), &errh, 0, TRUE,
                        DUPLICATE_SAME_ACCESS);

    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = job_r;
    si.hStdOutput = msg_w;
    si.hStdError = errh;
    PROCESS_INFORMATION pi;
    job_make();
    /* suspended until it is in the job, so it never runs outside it */
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi);
    if (!ok)
        fail(err, n, "the worker didn't start");
    CloseHandle(job_r);
    CloseHandle(msg_w);
    if (errh)
        CloseHandle(errh);
    if (!ok) {
        CloseHandle(job_w);
        CloseHandle(msg_r);
        return false;
    }
    if (g_job)
        AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p->id = (intptr_t)pi.hProcess;
    p->to = _open_osfhandle((intptr_t)job_w, _O_WRONLY | _O_BINARY);
    p->from = _open_osfhandle((intptr_t)msg_r, _O_RDONLY | _O_BINARY);
    if (p->to < 0 || p->from < 0) {
        snprintf(err, n, "the worker's pipes can't be used");
        if (p->to < 0)
            CloseHandle(job_w);
        if (p->from < 0)
            CloseHandle(msg_r);
        p->to = p->to < 0 ? -1 : p->to;
        p->from = p->from < 0 ? -1 : p->from;
        arena_os_end(p);
        return false;
    }
    return true;
}

long arena_os_read(ArenaProc *p, void *buf, size_t n)
{
    HANDLE h = (HANDLE)_get_osfhandle(p->from);
    DWORD avail = 0, got = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
        return -1;
    if (!avail)
        return 0;
    if (!ReadFile(h, buf, avail < n ? avail : (DWORD)n, &got, NULL))
        return -1;
    return (long)got;
}

void arena_os_end(ArenaProc *p)
{
    HANDLE h = (HANDLE)p->id;
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    if (p->to >= 0)
        _close(p->to);
    if (p->from >= 0)
        _close(p->from);
}

void arena_os_wait(const int *from, int n, int ms)
{
    double end = arena_os_now() + ms / 1000.0;
    for (;;) {
        for (int i = 0; i < n; i++) {
            DWORD avail = 0;
            if (!PeekNamedPipe((HANDLE)_get_osfhandle(from[i]), NULL, 0, NULL, &avail, NULL) || avail)
                return;
        }
        if (arena_os_now() >= end)
            return;
        Sleep(5);
    }
}

void arena_os_child_begin(void)
{
    /* it ends with the program through the job it was put in */
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    /* Windows has no line buffering (_IOLBF buffers fully): unbuffered,
     * each printf one write */
    setvbuf(stdout, NULL, _IONBF, 0);
}

int arena_os_worker_begin(bool nice)
{
    if (nice)
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    _setmode(0, _O_BINARY);
    int fd = _dup(1);
    if (fd < 0)
        return -1;
    _setmode(fd, _O_BINARY);
    if (_dup2(2, 1) != 0) {
        int nul = _open("NUL", _O_WRONLY);
        if (nul >= 0) {
            _dup2(nul, 1);
            _close(nul);
        }
    }
    return fd;
}

int arena_os_cores(void)
{
    return (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}

double arena_os_now(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart)
        QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

bool arena_os_replace(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
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
    setvbuf(stdout, NULL, _IOLBF, 0);
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

#endif
