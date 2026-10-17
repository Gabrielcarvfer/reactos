/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stress test of byte-range file locking (LockFileEx/UnlockFileEx)
 *
 * Many threads, each with its own handles to the same files, lock random
 * ranges and check every grant against a shadow of what is held: two
 * exclusive owners, or exclusive + shared, is a violation. Exclusive owners
 * write a pattern and check it on release, shared owners check the data did
 * not change. A private zone per thread checks the Windows rules (another
 * handle can't read/write an exclusive lock, nobody writes into a shared
 * one, ERROR_NOT_LOCKED, closing a handle drops its locks and completes its
 * lock waits). Found: an unsynchronized lock table corrupted by two CPUs
 * (Audacity's SQLite). Runs ROSTEST_STRESS_SECONDS seconds (default 20).
 * Also repro/lockstress.c in the workspace (standalone, with options).
 */

#include "precomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* (Vista functions, looked up: the 32-bit tests link against 2003 exports) */
static BOOL (WINAPI *pCancelIoEx)(HANDLE, LPOVERLAPPED);
#define CancelIoEx(h, ov) (pCancelIoEx ? pCancelIoEx(h, ov) : CancelIo(h))
#define GetTickCount64() ((ULONGLONG)GetTickCount())   /* (test durations: no wrap) */

#define MAX_THREADS 64
#define MAX_FILES   8
#define BLOCK       64
#define ZONE_BLOCKS 1024                     /* shared zone: 64 KB */
#define ZONE_SIZE   (ZONE_BLOCKS * BLOCK)
#define PRIV_BASE   0x100000ULL               /* private zones, one per thread */
#define PRIV_SIZE   0x1000ULL
#define MAXHELD     8
#define MAXLEN      (8 * BLOCK)

static WCHAR Dir[MAX_PATH] = L"C:\\lockstress";
static int Seconds = 60, NThreads, NFiles = 2;
static volatile LONG Stop;
static volatile LONG64 Ops, Violations, Semantic, Errors;
static LONG Shadow[MAX_FILES][ZONE_BLOCKS];
static CRITICAL_SECTION PrintLock;

typedef struct
{
    int f;
    DWORD start, len, seq;
    BOOL excl;
    BYTE data[MAXLEN];           /* what the range must hold on release */
} HELD;

typedef struct
{
    int Id;
    unsigned Seed;
    HANDLE File[MAX_FILES];      /* synchronous */
    HANDLE FileOv[MAX_FILES];    /* overlapped, for waiting locks */
    volatile ULONGLONG Beat;     /* GetTickCount64 of the last progress */
    const char *volatile Doing;
    HELD Held[MAXHELD];
    int NHeld;
} WORKER;

static WORKER Workers[MAX_THREADS];

static void report(volatile LONG64 *counter, const char *kind, WORKER *w, const char *fmt, ...)
{
    va_list va;
    InterlockedIncrement64(counter);
    EnterCriticalSection(&PrintLock);
    printf("%s [t%d] ", kind, w ? w->Id : -1);
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
    fflush(stdout);
    LeaveCriticalSection(&PrintLock);
}

static unsigned rnd(WORKER *w)
{
    w->Seed = w->Seed * 1103515245u + 12345u;
    return (w->Seed >> 8) & 0xffffff;
}

static HANDLE open_file(int f, BOOL overlapped)
{
    WCHAR path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\file%d.bin", Dir, f);
    return CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_ALWAYS, overlapped ? FILE_FLAG_OVERLAPPED : 0, NULL);
}

static void set_ov(OVERLAPPED *ov, ULONGLONG off)
{
    memset(ov, 0, sizeof(*ov));
    ov->Offset = (DWORD)off;
    ov->OffsetHigh = (DWORD)(off >> 32);
}

/* Synchronous handles only */
static BOOL lock(HANDLE h, ULONGLONG off, DWORD len, BOOL excl, DWORD *err)
{
    OVERLAPPED ov;
    BOOL res;
    set_ov(&ov, off);
    res = LockFileEx(h, (excl ? LOCKFILE_EXCLUSIVE_LOCK : 0) | LOCKFILE_FAIL_IMMEDIATELY, 0, len, 0, &ov);
    *err = res ? 0 : GetLastError();
    return res;
}

static BOOL unlock(HANDLE h, ULONGLONG off, DWORD len, DWORD *err)
{
    OVERLAPPED ov;
    BOOL res;
    set_ov(&ov, off);
    res = UnlockFileEx(h, 0, len, 0, &ov);
    *err = res ? 0 : GetLastError();
    return res;
}

static BOOL rw(HANDLE h, ULONGLONG off, void *buf, DWORD len, BOOL write, DWORD *err)
{
    OVERLAPPED ov;
    DWORD done = 0;
    BOOL res;
    set_ov(&ov, off);
    res = write ? WriteFile(h, buf, len, &done, &ov) : ReadFile(h, buf, len, &done, &ov);
    *err = res ? 0 : GetLastError();
    return res && done == len;
}

/* Overlapped lock that waits at most ms; TRUE if granted on h */
static BOOL lock_wait(HANDLE h, ULONGLONG off, DWORD len, BOOL excl, DWORD ms, BOOL *timedout)
{
    OVERLAPPED ov;
    DWORD dummy;
    BOOL res;
    set_ov(&ov, off);
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    *timedout = FALSE;
    res = LockFileEx(h, excl ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, len, 0, &ov);
    if (!res && GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(ov.hEvent, ms) == WAIT_TIMEOUT)
        {
            *timedout = TRUE;
            CancelIoEx(h, &ov);
        }
        res = GetOverlappedResult(h, &ov, &dummy, TRUE);
    }
    CloseHandle(ov.hEvent);
    return res;
}

/* ---- shadow: what the kernel has granted, as seen by the test */

static BOOL shadow_take(WORKER *w, int f, DWORD start, DWORD len, BOOL excl)
{
    DWORD b, first = start / BLOCK, last = (start + len - 1) / BLOCK;
    for (b = first; b <= last; b++)
    {
        LONG *s = &Shadow[f][b], v;
        if (excl)
        {
            v = InterlockedCompareExchange(s, -(w->Id + 1), 0);
            if (v != 0)
            {
                report(&Violations, "VIOLATION", w,
                       "file%d block %lu: exclusive lock granted while %s%ld holds it",
                       f, b, v < 0 ? "exclusive owner t" : "shared count ", v < 0 ? -v - 1 : v);
                while (b-- > first)
                    InterlockedExchange(&Shadow[f][b], 0);
                return FALSE;
            }
        }
        else
        {
            for (;;)
            {
                v = *s;
                if (v < 0)
                {
                    report(&Violations, "VIOLATION", w,
                           "file%d block %lu: shared lock granted while t%ld holds it exclusively", f, b, -v - 1);
                    while (b-- > first)
                        InterlockedDecrement(&Shadow[f][b]);
                    return FALSE;
                }
                if (InterlockedCompareExchange(s, v + 1, v) == v)
                    break;
            }
        }
    }
    return TRUE;
}

static void shadow_drop(WORKER *w, int f, DWORD start, DWORD len, BOOL excl)
{
    DWORD b, first = start / BLOCK, last = (start + len - 1) / BLOCK;
    for (b = first; b <= last; b++)
    {
        if (excl)
            InterlockedCompareExchange(&Shadow[f][b], 0, -(w->Id + 1));
        else
            InterlockedDecrement(&Shadow[f][b]);
    }
}

static BOOL overlaps_held(WORKER *w, int f, DWORD start, DWORD len)
{
    int i;
    for (i = 0; i < w->NHeld; i++)
        if (w->Held[i].f == f && start < w->Held[i].start + w->Held[i].len && w->Held[i].start < start + len)
            return TRUE;
    return FALSE;
}

static void fill(BYTE *buf, DWORD len, int id, DWORD seq)
{
    DWORD i;
    for (i = 0; i < len; i++)
        buf[i] = (BYTE)(id * 31 + seq * 7 + i);
}

/* ---- operations on the shared zone */

static void op_lock(WORKER *w)
{
    int f = rnd(w) % NFiles;
    DWORD first = rnd(w) % ZONE_BLOCKS, n = 1 + rnd(w) % 8, start, len, err;
    BOOL excl = rnd(w) & 1, res, timedout = FALSE;
    HANDLE h;
    HELD *held;

    if (w->NHeld >= MAXHELD)
        return;
    if (first + n > ZONE_BLOCKS)
        n = ZONE_BLOCKS - first;
    start = first * BLOCK;
    len = n * BLOCK;
    if (overlaps_held(w, f, start, len))
        return;     /* (locks over one's own locks: see op_rules) */

    /* Wait only when holding nothing, so threads can't deadlock each other */
    if (!w->NHeld && (rnd(w) % 4) == 0)
    {
        w->Doing = "LockFileEx (waiting, overlapped)";
        h = w->FileOv[f];
        res = lock_wait(h, start, len, excl, 2000, &timedout);
        if (!res && !timedout && GetLastError() != ERROR_OPERATION_ABORTED)
            report(&Errors, "ERROR", w, "waiting LockFileEx(file%d %lu+%lu) failed: %lu", f, start, len, GetLastError());
    }
    else
    {
        w->Doing = "LockFileEx (immediate)";
        h = w->File[f];
        res = lock(h, start, len, excl, &err);
        if (!res && err != ERROR_LOCK_VIOLATION)
            report(&Errors, "ERROR", w, "LockFileEx(file%d %lu+%lu, %s) failed with %lu",
                   f, start, len, excl ? "excl" : "shared", err);
    }
    if (!res)
        return;

    if (!shadow_take(w, f, start, len, excl))
    {
        unlock(h, start, len, &err);
        return;
    }
    /* A waited lock belongs to the overlapped handle. Another handle (the main
     * one) must not get an exclusive lock over it, then it is simply released:
     * the shadow and data checks only use locks held on the main handle. */
    if (h != w->File[f])
    {
        if (excl && lock(w->File[f], start, len, TRUE, &err))
        {
            report(&Violations, "VIOLATION", w, "file%d %lu+%lu: a second handle got an exclusive lock over another handle's exclusive lock",
                   f, start, len);
            unlock(w->File[f], start, len, &err);
        }
        shadow_drop(w, f, start, len, excl);
        if (!unlock(h, start, len, &err))
            report(&Errors, "ERROR", w, "UnlockFileEx of a waited lock failed: %lu", err);
        return;
    }

    held = &w->Held[w->NHeld];
    held->f = f;
    held->start = start;
    held->len = len;
    held->excl = excl;
    held->seq = rnd(w);
    if (excl)
    {
        fill(held->data, len, w->Id, held->seq);
        w->Doing = "WriteFile (own exclusive lock)";
        if (!rw(w->File[f], start, held->data, len, TRUE, &err))
            report(&Errors, "ERROR", w, "write inside own exclusive lock file%d %lu+%lu failed: %lu", f, start, len, err);
    }
    else
    {
        w->Doing = "ReadFile (own shared lock)";
        if (!rw(w->File[f], start, held->data, len, FALSE, &err))
            report(&Errors, "ERROR", w, "read inside own shared lock file%d %lu+%lu failed: %lu", f, start, len, err);
    }
    w->NHeld++;
}

static void op_release(WORKER *w)
{
    HELD *held;
    BYTE now[MAXLEN];
    DWORD err;
    int i;

    if (!w->NHeld)
        return;
    i = rnd(w) % w->NHeld;
    held = &w->Held[i];

    w->Doing = "ReadFile (verify before release)";
    if (rw(w->File[held->f], held->start, now, held->len, FALSE, &err))
    {
        if (memcmp(now, held->data, held->len))
            report(&Violations, "VIOLATION", w, "file%d %lu+%lu: data changed under a%s lock",
                   held->f, held->start, held->len, held->excl ? "n exclusive" : " shared");
    }
    else
        report(&Errors, "ERROR", w, "read before release file%d %lu+%lu failed: %lu", held->f, held->start, held->len, err);

    shadow_drop(w, held->f, held->start, held->len, held->excl);   /* before unlocking: no false alarms */
    w->Doing = "UnlockFileEx";
    if (!unlock(w->File[held->f], held->start, held->len, &err))
        report(&Errors, "ERROR", w, "UnlockFileEx(file%d %lu+%lu) of a held lock failed: %lu",
               held->f, held->start, held->len, err);
    *held = w->Held[--w->NHeld];
}

/* ---- Windows rules, in this thread's private zone (nobody else touches it) */

static void op_rules(WORKER *w)
{
    int f = rnd(w) % NFiles;
    ULONGLONG base = PRIV_BASE + (ULONGLONG)w->Id * PRIV_SIZE;
    HANDLE other, temp, waiter;
    BYTE buf[64];
    DWORD err;
    BOOL timedout;

    w->Doing = "rules: open";
    other = open_file(f, FALSE);
    if (other == INVALID_HANDLE_VALUE)
    {
        report(&Errors, "ERROR", w, "second handle: CreateFile failed %lu", GetLastError());
        return;
    }

    /* 1. Exclusive lock on handle A: handle B (same process) may not read/write it */
    w->Doing = "rules: exclusive";
    if (lock(w->File[f], base, 64, TRUE, &err))
    {
        if (rw(other, base, buf, 64, FALSE, &err) || err != ERROR_LOCK_VIOLATION)
            report(&Semantic, "SEMANTIC", w, "read by another handle inside an exclusive lock: %s %lu (want ERROR_LOCK_VIOLATION)",
                   err ? "error" : "succeeded", err);
        memset(buf, 0xAA, sizeof(buf));
        if (rw(other, base, buf, 64, TRUE, &err) || err != ERROR_LOCK_VIOLATION)
            report(&Semantic, "SEMANTIC", w, "write by another handle inside an exclusive lock: %s %lu (want ERROR_LOCK_VIOLATION)",
                   err ? "error" : "succeeded", err);
        if (lock(other, base, 64, FALSE, &err))
        {
            report(&Violations, "VIOLATION", w, "another handle got a shared lock inside an exclusive lock");
            unlock(other, base, 64, &err);
        }
        if (lock(w->File[f], base, 64, TRUE, &err))
        {
            report(&Semantic, "SEMANTIC", w, "a second exclusive lock on the same range and handle was granted");
            unlock(w->File[f], base, 64, &err);
        }
        if (unlock(w->File[f], base, 32, &err) || err != ERROR_NOT_LOCKED)
            report(&Errors, "ERROR", w, "UnlockFileEx with the wrong length: %lu (want ERROR_NOT_LOCKED)", err);
        if (unlock(other, base, 64, &err) || err != ERROR_NOT_LOCKED)
            report(&Errors, "ERROR", w, "UnlockFileEx from another handle: %lu (want ERROR_NOT_LOCKED)", err);
        if (!unlock(w->File[f], base, 64, &err))
            report(&Errors, "ERROR", w, "UnlockFileEx of own lock failed: %lu", err);
        if (unlock(w->File[f], base, 64, &err) || err != ERROR_NOT_LOCKED)
            report(&Errors, "ERROR", w, "second UnlockFileEx of the same lock: %lu (want ERROR_NOT_LOCKED)", err);
    }
    else
        report(&Errors, "ERROR", w, "exclusive lock on a private range failed: %lu", err);

    /* 2. Shared lock: other handles may read but not write */
    w->Doing = "rules: shared";
    if (lock(w->File[f], base + 128, 64, FALSE, &err))
    {
        memset(buf, 0x55, sizeof(buf));
        if (rw(other, base + 128, buf, 64, TRUE, &err) || err != ERROR_LOCK_VIOLATION)
            report(&Semantic, "SEMANTIC", w, "write by another handle into a shared lock: %s %lu (want ERROR_LOCK_VIOLATION)",
                   err ? "error" : "succeeded", err);
        if (!rw(other, base + 128, buf, 64, FALSE, &err))
            report(&Errors, "ERROR", w, "read by another handle inside a shared lock failed: %lu", err);
        if (lock(other, base + 128, 64, TRUE, &err))
        {
            report(&Violations, "VIOLATION", w, "another handle got an exclusive lock inside a shared lock");
            unlock(other, base + 128, 64, &err);
        }
        unlock(w->File[f], base + 128, 64, &err);
    }

    /* 3. Closing a handle drops its locks: the waiter behind them gets the lock */
    w->Doing = "rules: close wakes waiter";
    temp = open_file(f, FALSE);
    waiter = open_file(f, TRUE);
    if (temp != INVALID_HANDLE_VALUE && waiter != INVALID_HANDLE_VALUE && lock(temp, base + 256, 64, TRUE, &err))
    {
        OVERLAPPED ov;
        DWORD dummy;
        set_ov(&ov, base + 256);
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!LockFileEx(waiter, LOCKFILE_EXCLUSIVE_LOCK, 0, 64, 0, &ov) && GetLastError() == ERROR_IO_PENDING)
        {
            CloseHandle(temp);
            temp = INVALID_HANDLE_VALUE;
            if (WaitForSingleObject(ov.hEvent, 5000) == WAIT_TIMEOUT)
            {
                report(&Errors, "ERROR", w, "a lock wait was not granted after the blocking handle was closed");
                CancelIoEx(waiter, &ov);
            }
            if (GetOverlappedResult(waiter, &ov, &dummy, TRUE))
                unlock(waiter, base + 256, 64, &err);
        }
        else
            report(&Errors, "ERROR", w, "a lock wait behind a held lock did not pend: %lu", GetLastError());
        CloseHandle(ov.hEvent);
    }
    if (temp != INVALID_HANDLE_VALUE) CloseHandle(temp);
    if (waiter != INVALID_HANDLE_VALUE) CloseHandle(waiter);

    /* 4. A pending lock wait whose own handle is closed must complete, not hang */
    w->Doing = "rules: close completes own wait";
    temp = open_file(f, FALSE);
    if (temp != INVALID_HANDLE_VALUE && lock(temp, base + 512, 64, TRUE, &err))
    {
        HANDLE w2 = open_file(f, TRUE);
        OVERLAPPED ov;
        set_ov(&ov, base + 512);
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (w2 != INVALID_HANDLE_VALUE &&
            !LockFileEx(w2, LOCKFILE_EXCLUSIVE_LOCK, 0, 64, 0, &ov) && GetLastError() == ERROR_IO_PENDING)
        {
            CloseHandle(w2);
            w2 = INVALID_HANDLE_VALUE;
            if (WaitForSingleObject(ov.hEvent, 5000) == WAIT_TIMEOUT)
                report(&Semantic, "SEMANTIC", w, "closing a handle left its own pending lock wait hanging");
        }
        if (w2 != INVALID_HANDLE_VALUE) CloseHandle(w2);
        CloseHandle(ov.hEvent);
        unlock(temp, base + 512, 64, &err);
    }
    if (temp != INVALID_HANDLE_VALUE) CloseHandle(temp);

    /* 5. Ranges near 2^63, a waited lock that times out, zero length */
    w->Doing = "rules: edges";
    {
        ULONGLONG top = 0x7FFFFFFFFFFFF000ULL - (ULONGLONG)w->Id * 256;
        if (lock(other, top, 128, TRUE, &err))
        {
            if (lock(w->File[f], top + 64, 128, TRUE, &err))
            {
                report(&Violations, "VIOLATION", w, "overlapping exclusive locks near 2^63 both granted");
                unlock(w->File[f], top + 64, 128, &err);
            }
            if (lock_wait(w->FileOv[f], top + 64, 128, TRUE, 300, &timedout))
            {
                report(&Violations, "VIOLATION", w, "a waited lock was granted over another handle's exclusive lock (near 2^63)");
                unlock(w->FileOv[f], top + 64, 128, &err);
            }
            else if (!timedout)
                report(&Errors, "ERROR", w, "waited lock behind a held lock failed instead of waiting: %lu", GetLastError());
            unlock(other, top, 128, &err);
        }
        else
            report(&Errors, "ERROR", w, "lock near 2^63 failed: %lu", err);
        if (lock(other, base + 900, 0, TRUE, &err) && !unlock(other, base + 900, 0, &err))
            report(&Errors, "ERROR", w, "unlock of a zero-length lock failed: %lu", err);
    }
    CloseHandle(other);
}

static DWORD WINAPI worker(void *arg)
{
    WORKER *w = arg;
    int f;
    for (f = 0; f < NFiles; f++)
    {
        w->File[f] = open_file(f, FALSE);
        w->FileOv[f] = open_file(f, TRUE);
        if (w->File[f] == INVALID_HANDLE_VALUE || w->FileOv[f] == INVALID_HANDLE_VALUE)
        {
            report(&Errors, "ERROR", w, "CreateFile file%d failed: %lu", f, GetLastError());
            return 1;
        }
    }
    while (!Stop)
    {
        unsigned r = rnd(w) % 100;
        w->Beat = GetTickCount64();
        if (r < 50) op_lock(w);
        else if (r < 97) op_release(w);
        else op_rules(w);
        w->Doing = "idle";
        InterlockedIncrement64(&Ops);
    }
    w->Beat = GetTickCount64();
    while (w->NHeld)
        op_release(w);
    for (f = 0; f < NFiles; f++)
    {
        CloseHandle(w->File[f]);
        CloseHandle(w->FileOv[f]);
    }
    return 0;
}

static int run_lockstress(void)
{
    HANDLE threads[MAX_THREADS];
    SYSTEM_INFO si;
    ULONGLONG start, end, lastprint;
    int i, f;

    GetSystemInfo(&si);
    NThreads = 2 * si.dwNumberOfProcessors;
    if (NThreads < 2) NThreads = 2;
    if (NThreads > MAX_THREADS) NThreads = MAX_THREADS;
    if (NFiles < 1) NFiles = 1;
    if (NFiles > MAX_FILES) NFiles = MAX_FILES;
    InitializeCriticalSection(&PrintLock);
    CreateDirectoryW(Dir, NULL);
    for (f = 0; f < NFiles; f++)
    {
        HANDLE h = open_file(f, FALSE);
        LARGE_INTEGER size;
        if (h == INVALID_HANDLE_VALUE)
        {
            printf("cannot create file%d in %ls: %lu\n", f, Dir, GetLastError());
            return 99;
        }
        size.QuadPart = PRIV_BASE + MAX_THREADS * PRIV_SIZE;
        SetFilePointerEx(h, size, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        CloseHandle(h);
    }
    printf("lockstress: %d threads, %d files, %d s, %lu CPUs, dir %ls\n",
           NThreads, NFiles, Seconds, si.dwNumberOfProcessors, Dir);
    fflush(stdout);

    for (i = 0; i < NThreads; i++)
    {
        Workers[i].Id = i;
        Workers[i].Seed = GetTickCount() * 2654435761u + i * 40503u;
        Workers[i].Beat = GetTickCount64();
        Workers[i].Doing = "start";
        threads[i] = CreateThread(NULL, 0, worker, &Workers[i], 0, NULL);
    }

    /* Watchdog + progress */
    start = lastprint = GetTickCount64();
    end = start + (ULONGLONG)Seconds * 1000;
    while (GetTickCount64() < end)
    {
        ULONGLONG now;
        Sleep(1000);
        now = GetTickCount64();
        for (i = 0; i < NThreads; i++)
            if (now - Workers[i].Beat > 20000)
            {
                report(&Errors, "STUCK", &Workers[i], "no progress for %I64u s in %s",
                       (now - Workers[i].Beat) / 1000, Workers[i].Doing);
                Workers[i].Beat = now;
            }
        if (now - lastprint >= 10000)
        {
            printf("  %I64u s: %I64d ops, %I64d violations, %I64d semantic, %I64d errors\n",
                   (now - start) / 1000, Ops, Violations, Semantic, Errors);
            fflush(stdout);
            lastprint = now;
        }
    }
    Stop = 1;
    if (WaitForMultipleObjects(NThreads, threads, TRUE, 60000) == WAIT_TIMEOUT)
        report(&Errors, "STUCK", NULL, "threads did not finish within 60 s of the end");

    /* Nothing may be left locked: a fresh handle locks each whole zone */
    for (f = 0; f < NFiles; f++)
    {
        HANDLE h = open_file(f, FALSE);
        DWORD err;
        if (!lock(h, 0, ZONE_SIZE, TRUE, &err))
            report(&Errors, "ERROR", NULL, "file%d: a lock was left behind after every thread released (%lu)", f, err);
        else
            unlock(h, 0, ZONE_SIZE, &err);
        CloseHandle(h);
    }

    printf("lockstress: %I64d ops, %I64d violations, %I64d semantic, %I64d errors\n", Ops, Violations, Semantic, Errors);
    return (int)(Violations + Errors);
}

START_TEST(LockStress)
{
    WCHAR temp[MAX_PATH], path[MAX_PATH], env[16];
    int f;

    pCancelIoEx = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelIoEx");
    Seconds = GetEnvironmentVariableW(L"ROSTEST_STRESS_SECONDS", env, 16) ? _wtoi(env) : 20;
    if (Seconds <= 0)
        Seconds = 20;
    GetTempPathW(MAX_PATH, temp);
    StringCchPrintfW(Dir, MAX_PATH, L"%lslockstress_apitest", temp);

    run_lockstress();
    ok(Violations == 0, "%I64d lock violations (locks granted that clash, or data changed under a lock)\n", Violations);
    ok(Errors == 0, "%I64d errors (failed calls, stuck threads, locks left behind)\n", Errors);
    ok(Semantic == 0, "%I64d differences from the Windows locking rules\n", Semantic);

    for (f = 0; f < NFiles; f++)
    {
        StringCchPrintfW(path, MAX_PATH, L"%ls\\file%d.bin", Dir, f);
        DeleteFileW(path);
    }
    RemoveDirectoryW(Dir);
}
