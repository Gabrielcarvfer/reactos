/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The FAT on the disk must match the free space the driver reports,
 *              after heavy parallel allocation churn and volume flushes
 *
 * Threads create, extend, truncate and delete files in many directories while
 * another thread keeps flushing the volume (FlushFileBuffers on \\.\C:). Then
 * the volume is flushed once more, the first FAT is read from the disk and its
 * free entries are counted: the count must be the free cluster count that
 * GetDiskFreeSpace reports (the driver's in-memory count), both while the
 * files still exist and after they are all deleted.
 *
 * Found: FastFAT wrote a FAT page through its list of dirty FAT sectors, and
 * the ReactOS cache manager can write a page while the driver is still
 * changing it. A FAT change made during such a write was dropped from the
 * list and never reached the disk; when Mm later reread that page, the
 * driver's per-window free cluster counters no longer matched the FAT
 * ("ClustersFree == FreeClusterBitMapClear" assertions during big installs),
 * and the clusters could be handed out twice.
 *
 * Runs ROSTEST_STRESS_SECONDS seconds (default 20). Skipped when the system
 * volume is not FAT.
 */

#include "precomp.h"
#include <winioctl.h>
#include <stdio.h>
#include "fatdisk.h"

#define THREADS 8
#define DIRS 16
#define KEEP 32

static char VolumeRoot[4] = "C:\\";
static char Base[MAX_PATH];
static volatile LONG Stop, Failures, Ops, Flushes;

typedef struct
{
    char name[MAX_PATH];
    int used;
} FREC;

static FREC Files[THREADS][KEEP];

static ULONG Rand(ULONG *s) { *s = *s * 1103515245 + 12345; return (*s >> 8) & 0xFFFFFF; }

/* Write-through files make the driver write their FAT entries at once, while
 * other threads change the FAT next to them */
static DWORD WriteThrough(ULONG *s) { return (Rand(s) % 3 == 0) ? FILE_FLAG_WRITE_THROUGH : 0; }

static BOOL Append(HANDLE h, BYTE *buf, ULONG bytes)
{
    DWORD w;
    while (bytes)
    {
        ULONG n = min(bytes, 65536);
        if (!WriteFile(h, buf, n, &w, NULL) || w != n)
            return FALSE;
        bytes -= n;
    }
    return TRUE;
}

static DWORD WINAPI Churn(LPVOID p)
{
    int me = (int)(ULONG_PTR)p;
    ULONG s = 0x9E3779B9u * (me + 1) ^ GetTickCount();
    BYTE *buf = VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    ULONG n = 0;

    if (!buf)
        return 0;
    memset(buf, 0x5A ^ me, 65536);

    while (!Stop)
    {
        FREC *f = &Files[me][Rand(&s) % KEEP];
        ULONG op = Rand(&s) % 10;
        HANDLE h;

        if (f->used && op < 3)
        {
            if (!DeleteFileA(f->name))
            {
                trace("delete %s: %lu\n", f->name, GetLastError());
                InterlockedIncrement(&Failures);
            }
            f->used = 0;
        }
        else if (f->used && op < 6)
        {
            /* Extend (often by a lot, to move the allocation around) */
            h = CreateFileA(f->name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, WriteThrough(&s), NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                SetFilePointer(h, 0, NULL, FILE_END);
                Append(h, buf, (Rand(&s) % 8 == 0) ? Rand(&s) % (4 * 1024 * 1024) : Rand(&s) % 100000 + 1);
                if (Rand(&s) % 8 == 0)
                    FlushFileBuffers(h);
                CloseHandle(h);
            }
        }
        else if (f->used && op < 8)
        {
            /* Truncate */
            h = CreateFileA(f->name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD size = GetFileSize(h, NULL);
                SetFilePointer(h, size ? Rand(&s) % size : 0, NULL, FILE_BEGIN);
                SetEndOfFile(h);
                CloseHandle(h);
            }
        }
        else
        {
            /* (Re)create, in one of the directories */
            if (f->used)
                DeleteFileA(f->name);
            StringCchPrintfA(f->name, MAX_PATH, "%s\\d%02lu\\t%d_%lu.bin",
                             Base, Rand(&s) % DIRS, me, n++ % 1000);
            h = CreateFileA(f->name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, WriteThrough(&s), NULL);
            f->used = (h != INVALID_HANDLE_VALUE);
            if (f->used)
            {
                /* Sometimes allocate up front, like installers do */
                if (Rand(&s) % 4 == 0)
                {
                    SetFilePointer(h, Rand(&s) % (2 * 1024 * 1024), NULL, FILE_BEGIN);
                    SetEndOfFile(h);
                    SetFilePointer(h, 0, NULL, FILE_BEGIN);
                }
                Append(h, buf, Rand(&s) % 200000);
                CloseHandle(h);
            }
            else
            {
                trace("create %s: %lu\n", f->name, GetLastError());
                InterlockedIncrement(&Failures);
            }
        }
        InterlockedIncrement(&Ops);
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    return 0;
}

static DWORD WINAPI Flusher(LPVOID p)
{
    HANDLE Volume = (HANDLE)p;
    while (!Stop)
    {
        FlushFileBuffers(Volume);
        InterlockedIncrement(&Flushes);
        Sleep(Flushes & 3);
    }
    return 0;
}

/* The FAT on the disk must agree with the driver's free cluster count.
 * Other programs may change the volume meanwhile, and the lazy writer may
 * still be writing the FAT: retry for a while. */
static void CheckFat(HANDLE Volume, const FATGEOMETRY *g, const char *when)
{
    DWORD spc, bps, free1, free2, total;
    LONG ondisk = -1;
    int attempt;

    for (attempt = 0; attempt < 20; attempt++)
    {
        if (attempt)
            Sleep(1000);
        FlushFileBuffers(Volume);
        if (!GetDiskFreeSpaceA(VolumeRoot, &spc, &bps, &free1, &total))
            break;
        ondisk = CountFreeOnDisk(Volume, g);
        if (!GetDiskFreeSpaceA(VolumeRoot, &spc, &bps, &free2, &total))
            break;
        if (free1 == free2 && ondisk == (LONG)free1)
        {
            ok(TRUE, "\n");
            trace("%s: %lu free clusters, the FAT agrees (attempt %d)\n", when, free1, attempt + 1);
            return;
        }
        trace("%s: attempt %d: driver says %lu/%lu free clusters, the FAT on the disk %ld\n",
              when, attempt + 1, free1, free2, ondisk);
    }
    ok(FALSE, "%s: the FAT on the disk has %ld free clusters, the driver says %lu (%ld clusters lost or leaked)\n",
       when, ondisk, free1, ondisk - (LONG)free1);
}

START_TEST(FatFreeClusters)
{
    char temp[MAX_PATH], env[16];
    HANDLE Volume, th[THREADS + 1];
    FATGEOMETRY g;
    int seconds, i, k;

    seconds = GetEnvironmentVariableA("ROSTEST_STRESS_SECONDS", env, sizeof(env)) ? atoi(env) : 20;
    if (seconds <= 0)
        seconds = 20;

    GetTempPathA(MAX_PATH, temp);
    VolumeRoot[0] = temp[0];
    Volume = OpenFatVolume(VolumeRoot, &g);
    if (Volume == INVALID_HANDLE_VALUE)
        return;

    CheckFat(Volume, &g, "before");

    StringCchPrintfA(Base, MAX_PATH, "%sfatfree_apitest", temp);
    CreateDirectoryA(Base, NULL);
    for (i = 0; i < DIRS; i++)
    {
        char d[MAX_PATH];
        StringCchPrintfA(d, MAX_PATH, "%s\\d%02d", Base, i);
        CreateDirectoryA(d, NULL);
    }

    for (i = 0; i < THREADS; i++)
        th[i] = CreateThread(NULL, 0, Churn, (LPVOID)(ULONG_PTR)i, 0, NULL);
    th[THREADS] = CreateThread(NULL, 0, Flusher, Volume, 0, NULL);
    Sleep(seconds * 1000);
    Stop = 1;
    WaitForMultipleObjects(THREADS + 1, th, TRUE, INFINITE);
    for (i = 0; i <= THREADS; i++)
        CloseHandle(th[i]);
    trace("%ld operations, %ld volume flushes\n", Ops, Flushes);

    CheckFat(Volume, &g, "with the files");

    for (i = 0; i < THREADS; i++)
        for (k = 0; k < KEEP; k++)
            if (Files[i][k].used && !DeleteFileA(Files[i][k].name))
            {
                trace("final delete %s: %lu\n", Files[i][k].name, GetLastError());
                Failures++;
            }
    for (i = 0; i < DIRS; i++)
    {
        char d[MAX_PATH];
        StringCchPrintfA(d, MAX_PATH, "%s\\d%02d", Base, i);
        RemoveDirectoryA(d);
    }
    RemoveDirectoryA(Base);

    CheckFat(Volume, &g, "after deleting them");

    ok(Failures == 0, "%ld file operations failed\n", Failures);
    ok(Ops > 0, "no operations\n");
    CloseHandle(Volume);
}
