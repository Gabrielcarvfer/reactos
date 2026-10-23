/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Many threads changing keys of one hive at the same time
 * COPYRIGHT:   Copyright 2026 ReactOS contributors
 */

#include "precomp.h"

/*
 * Writers of different keys of one hive run at the same time in the kernel:
 * each one only locks its own key. They share the hive's cell allocator, its
 * dirty vector and its block list (which grows when the hive needs another
 * bin), while readers use cells without a hive lock. With 4 processors this
 * crashed (bugcheck 0x50 in HvpGetCellHeader, the old block list had been
 * freed) or lost data. Each thread works in its own key under one parent
 * key; the values are large so that the hive grows often.
 */

#define STRESS_THREADS 8
#define STRESS_ROUNDS 60
#define STRESS_KEYS 4

static const WCHAR ParentPath[] = L"Software\\ReactOS-RegStress";
static LONG Failures;

typedef struct _STRESS_THREAD
{
    ULONG Index;
} STRESS_THREAD;

static
VOID
Fill(PBYTE Buffer, DWORD Size, ULONG Seed)
{
    DWORD i;
    for (i = 0; i < Size; i++)
        Buffer[i] = (BYTE)(Seed * 31 + i * 7);
}

static
DWORD
WINAPI
StressThread(PVOID Parameter)
{
    STRESS_THREAD *Thread = Parameter;
    BYTE *Data, *ReadBack;
    WCHAR Name[64], Sub[64];
    HKEY Parent, Key, SubKey;
    ULONG Round, k;
    DWORD Size, Type, Count;
    LONG Error;

    Data = HeapAlloc(GetProcessHeap(), 0, 64 * 1024);
    ReadBack = HeapAlloc(GetProcessHeap(), 0, 64 * 1024);
    if (!Data || !ReadBack)
    {
        InterlockedIncrement(&Failures);
        return 1;
    }

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, ParentPath, 0, KEY_ALL_ACCESS, &Parent);
    if (Error != ERROR_SUCCESS)
    {
        InterlockedIncrement(&Failures);
        return 1;
    }

    StringCbPrintfW(Name, sizeof(Name), L"Thread%lu", Thread->Index);
    for (Round = 0; Round < STRESS_ROUNDS; Round++)
    {
        Error = RegCreateKeyExW(Parent, Name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
        if (Error != ERROR_SUCCESS)
        {
            InterlockedIncrement(&Failures);
            break;
        }

        for (k = 0; k < STRESS_KEYS; k++)
        {
            /* 1 KB to 40 KB: big values take cells of their own, and new bins */
            Size = 1024 + ((Round * STRESS_KEYS + k + Thread->Index) % 40) * 1024;
            Fill(Data, Size, Thread->Index * 1000 + Round * 10 + k);
            StringCbPrintfW(Sub, sizeof(Sub), L"Key%lu", k);
            Error = RegCreateKeyExW(Key, Sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &SubKey, NULL);
            if (Error != ERROR_SUCCESS)
            {
                InterlockedIncrement(&Failures);
                continue;
            }
            if (RegSetValueExW(SubKey, L"Data", 0, REG_BINARY, Data, Size) != ERROR_SUCCESS)
                InterlockedIncrement(&Failures);
            Count = 64 * 1024;
            if (RegQueryValueExW(SubKey, L"Data", NULL, &Type, ReadBack, &Count) != ERROR_SUCCESS ||
                Count != Size || memcmp(Data, ReadBack, Size) != 0)
            {
                InterlockedIncrement(&Failures);
            }
            RegCloseKey(SubKey);
        }

        /* Read the other threads' keys too (cells used without a hive lock) */
        for (k = 0; ; k++)
        {
            Size = ARRAYSIZE(Sub);
            if (RegEnumKeyExW(Parent, k, Sub, &Size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
        }

        for (k = 0; k < STRESS_KEYS; k++)
        {
            StringCbPrintfW(Sub, sizeof(Sub), L"Key%lu", k);
            RegDeleteKeyW(Key, Sub);
        }
        RegCloseKey(Key);
        if (RegDeleteKeyW(Parent, Name) != ERROR_SUCCESS)
            InterlockedIncrement(&Failures);
    }

    RegCloseKey(Parent);
    HeapFree(GetProcessHeap(), 0, Data);
    HeapFree(GetProcessHeap(), 0, ReadBack);
    return 0;
}

START_TEST(RegStress)
{
    STRESS_THREAD Threads[STRESS_THREADS];
    HANDLE Handles[STRESS_THREADS];
    HKEY Parent;
    ULONG i;
    LONG Error;
    DWORD Wait, Start;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, ParentPath, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Parent, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW failed: %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    RegCloseKey(Parent);

    Start = GetTickCount();
    for (i = 0; i < STRESS_THREADS; i++)
    {
        Threads[i].Index = i;
        Handles[i] = CreateThread(NULL, 0, StressThread, &Threads[i], 0, NULL);
        ok(Handles[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    Wait = WaitForMultipleObjects(STRESS_THREADS, Handles, TRUE, 10 * 60 * 1000);
    ok(Wait == WAIT_OBJECT_0, "Threads did not finish: %lu\n", Wait);
    for (i = 0; i < STRESS_THREADS; i++)
    {
        if (Handles[i]) CloseHandle(Handles[i]);
    }
    trace("%u threads x %u rounds took %lu ms\n", STRESS_THREADS, STRESS_ROUNDS, GetTickCount() - Start);

    ok(Failures == 0, "%ld registry operations failed or read back wrong data\n", Failures);
    Error = RegDeleteKeyW(HKEY_CURRENT_USER, ParentPath);
    ok(Error == ERROR_SUCCESS, "RegDeleteKeyW failed: %ld\n", Error);
}
