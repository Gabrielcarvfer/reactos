/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stress test of the file system and cache manager from many threads
 *
 * Each thread creates files with a pattern derived from their name (written
 * through WriteFile or a file mapping), extends/truncates them, reads them back
 * and verifies, keeps or deletes them. At the end every remaining file is
 * verified and deleted, and the free space must be exactly what it was at the
 * start. Found: a cache manager race (two threads pinning the same range,
 * cc/pin.c) that corrupted FAT on several CPUs. Runs ROSTEST_STRESS_SECONDS
 * seconds (default 20) with 8 threads, in %TEMP%.
 * Also repro/fatstress.c in the workspace (standalone, with options).
 */

#include "precomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static char Dir[MAX_PATH];
static volatile LONG Stop, Failures, Ops, Verified, TruncRefused;
static FILE *Out;
static CRITICAL_SECTION LogLock;

static void Log(const char *fmt, ...)
{
    va_list ap;
    EnterCriticalSection(&LogLock);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (Out) { va_start(ap, fmt); vfprintf(Out, fmt, ap); va_end(ap); fflush(Out); }
    fflush(stdout);
    LeaveCriticalSection(&LogLock);
}

static ULONG Rand(ULONG *s) { *s = *s * 1103515245 + 12345; return (*s >> 8) & 0xFFFFFF; }

/* Byte i of file "seed": a function of both */
static BYTE PatternByte(ULONG seed, ULONG i) { return (BYTE)((i * 2654435761u) >> 13 ^ seed ^ (i >> 9)); }

static void Fill(BYTE *b, ULONG seed, ULONG off, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; i++) b[i] = PatternByte(seed, off + i);
}

static int Check(const char *name, BYTE *b, ULONG seed, ULONG off, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; i++)
        if (b[i] != PatternByte(seed, off + i))
        {
            Log("FAIL %s: byte %lu is %02x, expected %02x\n", name, off + i, b[i], PatternByte(seed, off + i));
            return 0;
        }
    return 1;
}

static int WriteFileWhole(const char *name, ULONG seed, ULONG size, int mapped, BYTE *buf)
{
    HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w;
    ULONG off;
    if (h == INVALID_HANDLE_VALUE) { Log("FAIL create %s: %lu\n", name, GetLastError()); return 0; }
    if (mapped && size)
    {
        HANDLE m;
        BYTE *v;
        SetFilePointer(h, size, NULL, FILE_BEGIN); SetEndOfFile(h);
        m = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, size, NULL);
        v = m ? MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, size) : NULL;
        if (!v) { Log("FAIL map %s: %lu\n", name, GetLastError()); if (m) CloseHandle(m); CloseHandle(h); return 0; }
        Fill(v, seed, 0, size);
        if (seed & 1) FlushViewOfFile(v, 0);
        UnmapViewOfFile(v);
        CloseHandle(m);
    }
    else
    {
        for (off = 0; off < size; off += 65536)
        {
            ULONG n = min(65536, size - off);
            Fill(buf, seed, off, n);
            if (!WriteFile(h, buf, n, &w, NULL) || w != n) { Log("FAIL write %s at %lu: %lu\n", name, off, GetLastError()); CloseHandle(h); return 0; }
        }
    }
    CloseHandle(h);
    return 1;
}

static int VerifyFile(const char *name, ULONG seed, ULONG size, BYTE *buf)
{
    HANDLE h = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD r, hi;
    ULONG off;
    if (h == INVALID_HANDLE_VALUE) { Log("FAIL open %s: %lu\n", name, GetLastError()); return 0; }
    if (GetFileSize(h, &hi) != size) { Log("FAIL %s: size %lu, expected %lu\n", name, GetFileSize(h, &hi), size); CloseHandle(h); return 0; }
    for (off = 0; off < size; off += 65536)
    {
        ULONG n = min(65536, size - off);
        if (!ReadFile(h, buf, n, &r, NULL) || r != n) { Log("FAIL read %s at %lu: %lu\n", name, off, GetLastError()); CloseHandle(h); return 0; }
        if (!Check(name, buf, seed, off, n)) { CloseHandle(h); return 0; }
    }
    CloseHandle(h);
    InterlockedIncrement(&Verified);
    return 1;
}

#define KEEP 24
typedef struct { char name[MAX_PATH]; ULONG seed, size; int used; } FILEREC;
static FILEREC Kept[64][KEEP];

static DWORD WINAPI Worker(LPVOID p)
{
    int me = (int)(ULONG_PTR)p;
    ULONG s = 0x9E3779B9u * (me + 1) ^ GetTickCount();
    BYTE *buf = VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    ULONG n = 0;

    while (!Stop)
    {
        FILEREC *f = &Kept[me][Rand(&s) % KEEP];
        ULONG op = Rand(&s) % 10;

        if (f->used && op < 3)
        {
            /* verify and delete */
            if (!VerifyFile(f->name, f->seed, f->size, buf)) InterlockedIncrement(&Failures);
            if (!DeleteFileA(f->name)) { Log("FAIL delete %s: %lu\n", f->name, GetLastError()); InterlockedIncrement(&Failures); }
            f->used = 0;
        }
        else if (f->used && op < 5)
        {
            /* grow it: append more pattern bytes */
            HANDLE h = CreateFileA(f->name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            ULONG add = Rand(&s) % (512 * 1024) + 1, off, w;
            if (h == INVALID_HANDLE_VALUE) { Log("FAIL reopen %s: %lu\n", f->name, GetLastError()); InterlockedIncrement(&Failures); f->used = 0; continue; }
            SetFilePointer(h, 0, NULL, FILE_END);
            for (off = 0; off < add; off += 65536)
            {
                ULONG c = min(65536, add - off);
                Fill(buf, f->seed, f->size + off, c);
                if (!WriteFile(h, buf, c, &w, NULL) || w != c) { Log("FAIL append %s: %lu\n", f->name, GetLastError()); InterlockedIncrement(&Failures); break; }
            }
            f->size += add;
            CloseHandle(h);
        }
        else if (f->used && op < 6)
        {
            /* truncate it */
            HANDLE h = CreateFileA(f->name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                ULONG newsize = f->size ? Rand(&s) % f->size : 0;
                SetFilePointer(h, newsize, NULL, FILE_BEGIN);
                if (SetEndOfFile(h))
                    f->size = newsize;
                else
                    InterlockedIncrement(&TruncRefused); /* (e.g. still mapped: allowed) */
                CloseHandle(h);
            }
        }
        else
        {
            /* (re)create */
            if (f->used) DeleteFileA(f->name);
            sprintf(f->name, "%s\\t%02d_%03lu.bin", Dir, me, n++ % 1000);
            f->seed = Rand(&s);
            f->size = (Rand(&s) % 8 == 0) ? Rand(&s) % (4 * 1024 * 1024) : Rand(&s) % 70000;
            f->used = WriteFileWhole(f->name, f->seed, f->size, (Rand(&s) % 4) == 0, buf);
            if (!f->used) InterlockedIncrement(&Failures);
            else if (!VerifyFile(f->name, f->seed, f->size, buf)) InterlockedIncrement(&Failures);
        }
        InterlockedIncrement(&Ops);
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    return 0;
}

static ULONGLONG LogSize0;

/* Bytes the log file takes on the disk (whole clusters) */
static ULONGLONG LogAllocation(void)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    DWORD spc, bps, fc, tc;
    ULONGLONG size, cluster;
    if (Out) fflush(Out);
    if (!GetFileAttributesExA("C:\\fatstress.txt", GetFileExInfoStandard, &a)) return 0;
    if (!GetDiskFreeSpaceA("C:\\", &spc, &bps, &fc, &tc)) return 0;
    cluster = (ULONGLONG)spc * bps;
    size = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    return (size + cluster - 1) / cluster * cluster;
}

static int run_fatstress(int seconds, int threads)
{
    int i, k;
    HANDLE th[64];
    ULARGE_INTEGER free0, free1, tot, avail;
    char root[4] = "C:\\";
    BYTE *buf = VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);

    if (threads > 64) threads = 64;
    root[0] = Dir[0];
    InitializeCriticalSection(&LogLock);
    /* (Measure before the directory exists: it takes a cluster) */
    LogSize0 = LogAllocation();
    GetDiskFreeSpaceExA(root, &avail, &tot, &free0);
    CreateDirectoryA(Dir, NULL);
    Log("fatstress: %d threads, %d s, dir %s, free %I64u KB\n", threads, seconds, Dir, free0.QuadPart >> 10);

    for (i = 0; i < threads; i++) th[i] = CreateThread(NULL, 0, Worker, (LPVOID)(ULONG_PTR)i, 0, NULL);
    Sleep(seconds * 1000);
    Stop = 1;
    WaitForMultipleObjects(threads, th, TRUE, INFINITE);

    /* Verify and delete what is left */
    for (i = 0; i < threads; i++)
        for (k = 0; k < KEEP; k++)
            if (Kept[i][k].used)
            {
                if (!VerifyFile(Kept[i][k].name, Kept[i][k].seed, Kept[i][k].size, buf)) Failures++;
                if (!DeleteFileA(Kept[i][k].name)) { Log("FAIL final delete %s\n", Kept[i][k].name); Failures++; }
            }
    RemoveDirectoryA(Dir);
    Sleep(2000);
    GetDiskFreeSpaceExA(root, &avail, &tot, &free1);
    /* The log file may have grown into another cluster meanwhile */
    free1.QuadPart += LogAllocation() - LogSize0;
    Log("fatstress: %ld operations, %ld verified files, free space %I64u KB -> %I64u KB\n",
        Ops, Verified, free0.QuadPart >> 10, free1.QuadPart >> 10);
    if (free1.QuadPart != free0.QuadPart)
    {
        Log("FAIL free space changed by %I64d KB\n", ((LONGLONG)free1.QuadPart - (LONGLONG)free0.QuadPart) / 1024);
        Failures++;
    }
    Log("fatstress: %ld failures (%ld truncations refused)\n", Failures, TruncRefused);
    return Failures;
}

START_TEST(FatStress)
{
    char temp[MAX_PATH], env[16];
    int seconds;

    seconds = GetEnvironmentVariableA("ROSTEST_STRESS_SECONDS", env, sizeof(env)) ? atoi(env) : 20;
    if (seconds <= 0)
        seconds = 20;
    GetTempPathA(MAX_PATH, temp);
    StringCchPrintfA(Dir, MAX_PATH, "%sfatstress_apitest", temp);

    run_fatstress(seconds, 8);
    ok(Failures == 0, "%ld failures (wrong data, failed calls, or the free space changed)\n", Failures);
    ok(Verified > 0, "no file was verified\n");
}
