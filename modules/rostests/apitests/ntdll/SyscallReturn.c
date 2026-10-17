/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that code keeps a usable stack after returning from system calls
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * On AMD CPUs SYSRET sets the SS selector but not its cached descriptor. If the
 * 64-bit kernel returned with a NULL SS (an interrupt in kernel mode loads one,
 * and a context switch carries it over), 32-bit (WOW64) code got an unusable
 * stack segment: its next push or ret raised a stack fault (#SS), and ReactOS
 * bugchecked. Many threads making system calls while others keep the CPU busy
 * (so interrupts and context switches land between syscall entry and return)
 * must all finish without a single fault. On a 64-bit system this test is
 * meaningful in its 32-bit build.
 */

#define SYSCALL_THREADS 4
#define BUSY_THREADS 2

static volatile LONG Stop;
static LONG Loops[SYSCALL_THREADS];
static LONG Faults[SYSCALL_THREADS];

static
DWORD
WINAPI
SyscallThread(
    _In_ PVOID Parameter)
{
    ULONG Index = (ULONG)(ULONG_PTR)Parameter;
    LARGE_INTEGER Zero = { { 0 } };

    while (!Stop)
    {
        _SEH2_TRY
        {
            /* Every return from these is followed by a ret on the user stack */
            NtYieldExecution();
            NtDelayExecution(FALSE, &Zero);
            Loops[Index]++;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Faults[Index]++;
        }
        _SEH2_END;
    }
    return 0;
}

static
DWORD
WINAPI
BusyThread(
    _In_ PVOID Parameter)
{
    volatile ULONG Value = (ULONG)(ULONG_PTR)Parameter;

    while (!Stop)
        Value = Value * 1664525 + 1013904223;
    return 0;
}

START_TEST(SyscallReturn)
{
    HANDLE Threads[SYSCALL_THREADS + BUSY_THREADS];
    ULONG i, Count = 0;

    for (i = 0; i < SYSCALL_THREADS; i++)
    {
        Threads[Count] = CreateThread(NULL, 0, SyscallThread, (PVOID)(ULONG_PTR)i, 0, NULL);
        ok(Threads[Count] != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (Threads[Count])
            Count++;
    }
    for (i = 0; i < BUSY_THREADS; i++)
    {
        Threads[Count] = CreateThread(NULL, 0, BusyThread, (PVOID)(ULONG_PTR)i, 0, NULL);
        ok(Threads[Count] != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (Threads[Count])
            Count++;
    }

    Sleep(3000);
    InterlockedExchange(&Stop, 1);
    ok(WaitForMultipleObjects(Count, Threads, TRUE, 10000) == WAIT_OBJECT_0,
       "The threads did not stop\n");

    for (i = 0; i < SYSCALL_THREADS; i++)
    {
        ok(Loops[i] > 0, "Thread %lu made no system calls\n", i);
        ok(Faults[i] == 0, "Thread %lu got %ld exceptions after returning from system calls\n",
           i, Faults[i]);
    }
    for (i = 0; i < Count; i++)
        CloseHandle(Threads[i]);
}
