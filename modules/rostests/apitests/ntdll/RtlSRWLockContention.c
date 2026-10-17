/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test SRW locks and condition variables under contention
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * Writers and readers hammer one SRW lock from several threads: the state it
 * protects must stay consistent (no reader sees a writer inside, never two
 * writers), nothing may crash or hang. Then two threads play ping-pong with a
 * condition variable on that lock: no wakeup may be lost.
 *
 * The old SRW code queued wait blocks on the waiters' stacks and crashed in
 * RtlReleaseSRWLockExclusive on multiprocessor machines (a shared wait block
 * with a NULL wake chain). The old condition variables released sleepers with
 * a zero-timeout keyed event release, so a sleeper that was queued but not
 * asleep yet missed its wakeup and slept forever. Flutter programs (RustDesk)
 * and Mesa's llvmpipe hit both.
 */

typedef VOID (NTAPI *PSRW)(PRTL_SRWLOCK);
typedef BOOLEAN (NTAPI *PTRYSRW)(PRTL_SRWLOCK);
typedef VOID (NTAPI *PCV)(PRTL_CONDITION_VARIABLE);
typedef NTSTATUS (NTAPI *PSLEEPCV)(PRTL_CONDITION_VARIABLE, PRTL_SRWLOCK, PLARGE_INTEGER, ULONG);

static PSRW pAcquireExclusive, pReleaseExclusive, pAcquireShared, pReleaseShared;
static PTRYSRW pTryExclusive, pTryShared;
static PCV pWakeAll;
static PSLEEPCV pSleep;

static RTL_SRWLOCK Lock;
static RTL_CONDITION_VARIABLE Condition;
static volatile LONG Writers, Readers, Errors, Stop, Ticket;
static LONG Writes;

#define PING_PONG_ROUNDS 5000

static DWORD WINAPI WriterThread(PVOID Parameter)
{
    ULONG n = 0;
    BOOLEAN Got;

    while (!Stop)
    {
        if ((n++ & 7) == 0)
            Got = pTryExclusive(&Lock);
        else
        {
            pAcquireExclusive(&Lock);
            Got = TRUE;
        }
        if (!Got)
            continue;
        if (InterlockedIncrement(&Writers) != 1 || Readers)
            InterlockedIncrement(&Errors);
        Writes++;
        if ((n & 15) == 0)
            SwitchToThread();
        InterlockedDecrement(&Writers);
        pReleaseExclusive(&Lock);
    }
    return 0;
}

static DWORD WINAPI ReaderThread(PVOID Parameter)
{
    ULONG n = 0;
    BOOLEAN Got;

    while (!Stop)
    {
        if ((n++ & 7) == 0)
            Got = pTryShared(&Lock);
        else
        {
            pAcquireShared(&Lock);
            Got = TRUE;
        }
        if (!Got)
            continue;
        InterlockedIncrement(&Readers);
        if (Writers)
            InterlockedIncrement(&Errors);
        if ((n & 31) == 0)
            Sleep(0);
        InterlockedDecrement(&Readers);
        pReleaseShared(&Lock);
    }
    return 0;
}

static DWORD WINAPI PingPongThread(PVOID Parameter)
{
    LONG Me = (LONG)(LONG_PTR)Parameter, i;

    for (i = 0; i < PING_PONG_ROUNDS; i++)
    {
        pAcquireExclusive(&Lock);
        while ((Ticket & 1) != Me)
            pSleep(&Condition, &Lock, NULL, 0);
        Ticket++;
        pWakeAll(&Condition);
        pReleaseExclusive(&Lock);
    }
    return 0;
}

START_TEST(RtlSRWLockContention)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    HANDLE Threads[12];
    ULONG i;
    DWORD Wait;

    pAcquireExclusive = (PSRW)GetProcAddress(hNtdll, "RtlAcquireSRWLockExclusive");
    pReleaseExclusive = (PSRW)GetProcAddress(hNtdll, "RtlReleaseSRWLockExclusive");
    pAcquireShared = (PSRW)GetProcAddress(hNtdll, "RtlAcquireSRWLockShared");
    pReleaseShared = (PSRW)GetProcAddress(hNtdll, "RtlReleaseSRWLockShared");
    pTryExclusive = (PTRYSRW)GetProcAddress(hNtdll, "RtlTryAcquireSRWLockExclusive");
    pTryShared = (PTRYSRW)GetProcAddress(hNtdll, "RtlTryAcquireSRWLockShared");
    pWakeAll = (PCV)GetProcAddress(hNtdll, "RtlWakeAllConditionVariable");
    pSleep = (PSLEEPCV)GetProcAddress(hNtdll, "RtlSleepConditionVariableSRW");
    if (!pAcquireExclusive || !pReleaseExclusive || !pAcquireShared || !pReleaseShared ||
        !pTryExclusive || !pTryShared || !pWakeAll || !pSleep)
    {
        skip("SRW lock or condition variable functions are not available\n");
        return;
    }

    /* Readers and writers, for a few seconds */
    for (i = 0; i < 4; i++)
        Threads[i] = CreateThread(NULL, 0, WriterThread, NULL, 0, NULL);
    for (i = 4; i < 12; i++)
        Threads[i] = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);
    for (i = 0; i < 12; i++)
        ok(Threads[i] != NULL, "CreateThread %lu failed: %lu\n", i, GetLastError());
    Sleep(3000);
    Stop = 1;
    Wait = WaitForMultipleObjects(12, Threads, TRUE, 30000);
    ok_long(Wait, WAIT_OBJECT_0);
    ok_long(Errors, 0);
    ok(Writes > 0, "No writer ever got the lock\n");
    for (i = 0; i < 12; i++)
        CloseHandle(Threads[i]);
    if (Wait != WAIT_OBJECT_0)
    {
        skip("The lock hung, skipping the condition variable test\n");
        return;
    }

    /* Condition variable ping-pong */
    Threads[0] = CreateThread(NULL, 0, PingPongThread, (PVOID)0, 0, NULL);
    Threads[1] = CreateThread(NULL, 0, PingPongThread, (PVOID)1, 0, NULL);
    Wait = WaitForMultipleObjects(2, Threads, TRUE, 60000);
    ok_long(Wait, WAIT_OBJECT_0);
    ok_long(Ticket, 2 * PING_PONG_ROUNDS);
    CloseHandle(Threads[0]);
    CloseHandle(Threads[1]);
}
