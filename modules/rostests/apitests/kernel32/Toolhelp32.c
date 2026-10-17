/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test the toolhelp snapshot structures and process names
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

#include <tlhelp32.h>
#include <stddef.h>

/*
 * The heap fields are ULONG_PTR in Windows' tlhelp32.h. ReactOS declared them as
 * DWORD, so on 64-bit the structures were 8 bytes shorter before szExeFile:
 * programs built with Windows' headers read process names without their first
 * 8 characters ("stackdump64.exe" became "p64.exe") and never found a process
 * by name.
 */
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(PROCESSENTRY32, szExeFile) == 44);
C_ASSERT(FIELD_OFFSET(PROCESSENTRY32W, szExeFile) == 44);
C_ASSERT(sizeof(PROCESSENTRY32) == 304);
C_ASSERT(sizeof(PROCESSENTRY32W) == 568);
C_ASSERT(sizeof(HEAPLIST32) == 32);
C_ASSERT(sizeof(HEAPENTRY32) == 56);
#else
C_ASSERT(FIELD_OFFSET(PROCESSENTRY32, szExeFile) == 36);
C_ASSERT(sizeof(PROCESSENTRY32) == 296);
C_ASSERT(sizeof(PROCESSENTRY32W) == 556);
C_ASSERT(sizeof(HEAPLIST32) == 16);
C_ASSERT(sizeof(HEAPENTRY32) == 36);
#endif

START_TEST(Toolhelp32)
{
    WCHAR Path[MAX_PATH], *Name;
    CHAR NameA[MAX_PATH];
    PROCESSENTRY32W EntryW;
    PROCESSENTRY32 EntryA;
    HANDLE Snapshot;
    BOOL FoundW = FALSE, FoundA = FALSE, Ok;
    DWORD Pid = GetCurrentProcessId();

    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    Name = wcsrchr(Path, L'\\') ? wcsrchr(Path, L'\\') + 1 : Path;
    WideCharToMultiByte(CP_ACP, 0, Name, -1, NameA, sizeof(NameA), NULL, NULL);

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    ok(Snapshot != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot failed: %lu\n", GetLastError());
    if (Snapshot == INVALID_HANDLE_VALUE)
        return;

    EntryW.dwSize = sizeof(EntryW);
    for (Ok = Process32FirstW(Snapshot, &EntryW); Ok; Ok = Process32NextW(Snapshot, &EntryW))
    {
        if (EntryW.th32ProcessID != Pid)
            continue;
        FoundW = TRUE;
        ok(!_wcsicmp(EntryW.szExeFile, Name), "Process32NextW name '%ls', expected '%ls'\n",
           EntryW.szExeFile, Name);
    }
    ok(FoundW, "Own process %lu not found (W)\n", Pid);

    EntryA.dwSize = sizeof(EntryA);
    for (Ok = Process32First(Snapshot, &EntryA); Ok; Ok = Process32Next(Snapshot, &EntryA))
    {
        if (EntryA.th32ProcessID != Pid)
            continue;
        FoundA = TRUE;
        ok(!_stricmp(EntryA.szExeFile, NameA), "Process32Next name '%s', expected '%s'\n",
           EntryA.szExeFile, NameA);
    }
    ok(FoundA, "Own process %lu not found (A)\n", Pid);

    CloseHandle(Snapshot);
}
