/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for NtGetNextThread and NtGetNextProcess
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * CCleaner walks its threads with NtGetNextThread; the stub answered with
 * an error that it retried forever.
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PFN_NEXT_THREAD)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
typedef NTSTATUS (NTAPI *PFN_NEXT_PROCESS)(HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);

static DWORD WINAPI SleepThread(LPVOID Parameter)
{
    WaitForSingleObject((HANDLE)Parameter, INFINITE);
    return 0;
}

START_TEST(NtGetNextThread)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_NEXT_THREAD pNtGetNextThread = (PFN_NEXT_THREAD)GetProcAddress(Ntdll, "NtGetNextThread");
    PFN_NEXT_PROCESS pNtGetNextProcess = (PFN_NEXT_PROCESS)GetProcAddress(Ntdll, "NtGetNextProcess");
    HANDLE Event, Threads[2], Current = NULL, Next;
    DWORD Ids[2], Count = 0, Found = 0, MyPid = GetCurrentProcessId();
    BOOLEAN FoundSelf = FALSE, FoundMain = FALSE;
    NTSTATUS Status;
    ULONG i;

    if (!pNtGetNextThread || !pNtGetNextProcess)
    {
        skip("NtGetNextThread or NtGetNextProcess is missing (before Vista)\n");
        return;
    }

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < 2; i++)
        Threads[i] = CreateThread(NULL, 0, SleepThread, Event, 0, &Ids[i]);

    /* Walk this process' threads */
    while (Count < 1000)
    {
        Status = pNtGetNextThread(GetCurrentProcess(), Current, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &Next);
        if (Current)
            CloseHandle(Current);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        ok(Status == STATUS_SUCCESS, "NtGetNextThread returned 0x%lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;
        Count++;
        if (GetThreadId(Next) == Ids[0] || GetThreadId(Next) == Ids[1])
            Found++;
        if (GetThreadId(Next) == GetCurrentThreadId())
            FoundMain = TRUE;
        Current = Next;
    }
    ok(Status == STATUS_NO_MORE_ENTRIES, "the walk ended with 0x%lx\n", Status);
    ok(Count >= 3 && Count < 1000, "%lu threads\n", Count);
    ok(Found == 2, "found %lu of the 2 new threads\n", Found);
    ok(FoundMain, "the current thread was not found\n");

    Status = pNtGetNextThread(GetCurrentProcess(), NULL, THREAD_QUERY_LIMITED_INFORMATION, 0, 1, &Next);
    ok(Status == STATUS_INVALID_PARAMETER, "flags 1: 0x%lx\n", Status);

    SetEvent(Event);
    WaitForMultipleObjects(2, Threads, TRUE, 10000);
    CloseHandle(Threads[0]);
    CloseHandle(Threads[1]);
    CloseHandle(Event);

    /* Walk the processes */
    Current = NULL;
    Count = 0;
    while (Count < 10000)
    {
        Status = pNtGetNextProcess(Current, PROCESS_QUERY_LIMITED_INFORMATION, 0, 0, &Next);
        if (Current)
            CloseHandle(Current);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        ok(Status == STATUS_SUCCESS, "NtGetNextProcess returned 0x%lx\n", Status);
        if (!NT_SUCCESS(Status))
            break;
        Count++;
        if (GetProcessId(Next) == MyPid)
            FoundSelf = TRUE;
        Current = Next;
    }
    ok(Status == STATUS_NO_MORE_ENTRIES, "the walk ended with 0x%lx\n", Status);
    ok(Count >= 2, "%lu processes\n", Count);
    ok(FoundSelf, "this process was not found\n");
}
