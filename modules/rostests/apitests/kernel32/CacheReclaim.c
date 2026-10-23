/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Memory held by the file cache is given back to programs quickly
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Reads a file larger than what will be left free, closes it (its pages
 * stay cached), then commits and touches more memory than is available:
 * the cached pages must be reclaimed without stalling the program. ReactOS
 * only started to free them when less than 1 MB was left, and each page
 * then took milliseconds.
 */

#include "precomp.h"

#define FILE_MB 384
#define CHUNK (1024 * 1024)

static double
NowMs(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

START_TEST(CacheReclaim)
{
    WCHAR Path[MAX_PATH], Dir[MAX_PATH];
    MEMORYSTATUSEX Status = { sizeof(Status) };
    HANDLE hFile;
    PUCHAR Buffer, Mem;
    DWORD Done, i;
    SIZE_T Size, Offset;
    double t0, Elapsed;

    GlobalMemoryStatusEx(&Status);
    if (Status.ullTotalPhys < 1024ULL * 1024 * 1024 || Status.ullTotalPhys > 8ULL * 1024 * 1024 * 1024)
    {
        skip("Needs 1 to 8 GB of RAM (has %I64u MB)\n", Status.ullTotalPhys >> 20);
        return;
    }

    Buffer = HeapAlloc(GetProcessHeap(), 0, CHUNK);
    ok(Buffer != NULL, "HeapAlloc failed\n");
    if (!Buffer) return;
    memset(Buffer, 0x5a, CHUNK);

    GetTempPathW(MAX_PATH, Dir);
    GetTempFileNameW(Dir, L"crc", 0, Path);

    /* Write the file, then read it back through the cache */
    hFile = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE) goto Cleanup;
    for (i = 0; i < FILE_MB; i++)
    {
        if (!WriteFile(hFile, Buffer, CHUNK, &Done, NULL) || Done != CHUNK)
        {
            skip("Could not write the file (%lu)\n", GetLastError());
            CloseHandle(hFile);
            goto Cleanup;
        }
    }
    /* Clean pages, as those of the programs and files a program read */
    FlushFileBuffers(hFile);
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    for (i = 0; i < FILE_MB; i++)
        ReadFile(hFile, Buffer, CHUNK, &Done, NULL);
    CloseHandle(hFile);
    Sleep(3000);

    /* Take what is available and 192 MB more: that part must come from the cache */
    GlobalMemoryStatusEx(&Status);
    Size = (SIZE_T)Status.ullAvailPhys + 192 * CHUNK;
    trace("Available %I64u MB, taking %Iu MB\n", Status.ullAvailPhys >> 20, Size >> 20);
    Mem = VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Mem != NULL, "VirtualAlloc of %Iu MB failed: %lu\n", Size >> 20, GetLastError());
    if (Mem)
    {
        t0 = NowMs();
        for (Offset = 0; Offset < Size; Offset += 4096)
            Mem[Offset] = 1;
        Elapsed = NowMs() - t0;
        trace("Touched in %.0f ms\n", Elapsed);
        ok(Elapsed < 20000, "Touching %Iu MB took %.0f ms\n", Size >> 20, Elapsed);
        VirtualFree(Mem, 0, MEM_RELEASE);
    }

Cleanup:
    DeleteFileW(Path);
    HeapFree(GetProcessHeap(), 0, Buffer);
}
