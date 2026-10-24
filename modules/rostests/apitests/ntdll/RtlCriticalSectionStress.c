/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Critical sections under contention on several processors:
 *              exclusivity, recursion, TryEnter, no lost wake-ups, and a loose
 *              check that contention does not make them crawl (lock convoys)
 * COPYRIGHT:   Copyright 2026 ReactOS contributors
 */

#include "precomp.h"

#define MAX_THREADS 16

static RTL_CRITICAL_SECTION Cs;
static volatile LONG Stop, Inside, Overlaps, Errors;
static volatile ULONG Counter;
static ULONG PerThread[MAX_THREADS], TryHits[MAX_THREADS];

static void Work(ULONG n)
{
    volatile ULONG x = 0;
    while (n--) x++;
}

static DWORD WINAPI ExclusiveThread(PVOID Param)
{
    ULONG Index = (ULONG)(ULONG_PTR)Param, n = 0;
    HANDLE Me = UlongToHandle(GetCurrentThreadId());
    BOOLEAN Got;

    while (!Stop)
    {
        if ((n & 15) == 15)
        {
            Got = (BOOLEAN)RtlTryEnterCriticalSection(&Cs);
            if (Got) TryHits[Index]++;
        }
        else
        {
            RtlEnterCriticalSection(&Cs);
            Got = TRUE;
        }

        if (Got)
        {
            /* Recursion */
            RtlEnterCriticalSection(&Cs);
            if (Cs.RecursionCount != 2 || Cs.OwningThread != Me)
                InterlockedIncrement(&Errors);
            if (!RtlTryEnterCriticalSection(&Cs))
                InterlockedIncrement(&Errors);
            if (Cs.RecursionCount != 3)
                InterlockedIncrement(&Errors);
            RtlLeaveCriticalSection(&Cs);
            RtlLeaveCriticalSection(&Cs);

            if (InterlockedIncrement(&Inside) != 1)
                InterlockedIncrement(&Overlaps);
            Counter = Counter + 1;
            Work(20);
            InterlockedDecrement(&Inside);

            if (Cs.RecursionCount != 1 || Cs.OwningThread != Me)
                InterlockedIncrement(&Errors);
            RtlLeaveCriticalSection(&Cs);
            PerThread[Index]++;
        }
        Work(50);
        n++;
    }
    return 0;
}

static ULONG GetThreadCount(void)
{
    SYSTEM_INFO si;
    ULONG Count;

    GetSystemInfo(&si);
    Count = 2 * si.dwNumberOfProcessors;
    if (Count < 4) Count = 4;
    if (Count > MAX_THREADS) Count = MAX_THREADS;
    return Count;
}

/* Returns the critical section entries per millisecond */
static double RunExclusive(ULONG Threads, ULONG Milliseconds)
{
    HANDLE Handles[MAX_THREADS];
    ULONG i, Total = 0, Start, Elapsed;

    Stop = 0;
    Counter = 0;
    ZeroMemory(PerThread, sizeof(PerThread));
    ZeroMemory(TryHits, sizeof(TryHits));

    Start = GetTickCount();
    for (i = 0; i < Threads; i++)
    {
        Handles[i] = CreateThread(NULL, 0, ExclusiveThread, (PVOID)(ULONG_PTR)i, 0, NULL);
        ok(Handles[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    Sleep(Milliseconds);
    Stop = 1;
    ok_long(WaitForMultipleObjects(Threads, Handles, TRUE, 30000), WAIT_OBJECT_0);
    Elapsed = GetTickCount() - Start;

    for (i = 0; i < Threads; i++)
    {
        Total += PerThread[i];
        CloseHandle(Handles[i]);
    }

    ok(Total == Counter, "%lu threads: %lu entries but counter %lu\n", Threads, Total, Counter);
    if (Threads > 1)
    {
        for (i = 0; i < Threads; i++)
            ok(PerThread[i] != 0, "Thread %lu of %lu never got the critical section\n", i, Threads);
    }
    return (double)Total / (Elapsed ? Elapsed : 1);
}

/* Many threads blocked in Enter must all get it when it is released
 * (no lost wake-ups) */
static volatile LONG BlockedIn;

static DWORD WINAPI BlockedThread(PVOID Param)
{
    InterlockedIncrement(&BlockedIn);
    RtlEnterCriticalSection(&Cs);
    if (InterlockedIncrement(&Inside) != 1)
        InterlockedIncrement(&Overlaps);
    Work(1000);
    InterlockedDecrement(&Inside);
    RtlLeaveCriticalSection(&Cs);
    return 0;
}

static void TestWakeups(ULONG Threads)
{
    HANDLE Handles[MAX_THREADS];
    ULONG Round, i, Wait;
    BOOL Failed = FALSE;

    for (Round = 0; Round < 100 && !Failed; Round++)
    {
        RtlEnterCriticalSection(&Cs);
        BlockedIn = 0;
        for (i = 0; i < Threads; i++)
            Handles[i] = CreateThread(NULL, 0, BlockedThread, NULL, 0, NULL);

        /* Let some of them block, the others race with the release */
        for (i = 0; i < 100 && BlockedIn < (LONG)Threads; i++)
            Sleep(1);
        if (Round & 1)
            Sleep(5);

        RtlLeaveCriticalSection(&Cs);

        Wait = WaitForMultipleObjects(Threads, Handles, TRUE, 20000);
        ok(Wait == WAIT_OBJECT_0, "Round %lu: waiters were not woken (%lu)\n", Round, Wait);
        if (Wait != WAIT_OBJECT_0)
            Failed = TRUE;
        for (i = 0; i < Threads; i++)
            CloseHandle(Handles[i]);
    }
}

static DWORD WINAPI TryEnterThread(PVOID Param)
{
    return RtlTryEnterCriticalSection(&Cs) ? 1 : 0;
}

static void TestTryEnterOwned(void)
{
    HANDLE Thread;
    DWORD Result = 2;

    RtlEnterCriticalSection(&Cs);
    Thread = CreateThread(NULL, 0, TryEnterThread, NULL, 0, NULL);
    ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
    GetExitCodeThread(Thread, &Result);
    ok_long(Result, 0);
    CloseHandle(Thread);
    RtlLeaveCriticalSection(&Cs);

    /* Free again: now it works */
    Thread = CreateThread(NULL, 0, TryEnterThread, NULL, 0, NULL);
    ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
    GetExitCodeThread(Thread, &Result);
    ok_long(Result, 1);
    CloseHandle(Thread);
    /* (the thread exited owning it) */
    Cs.OwningThread = NULL;
    Cs.RecursionCount = 0;
    Cs.LockCount = -1;
}

START_TEST(RtlCriticalSectionStress)
{
    ULONG Threads = GetThreadCount();
    double Single, Contended;

    RtlInitializeCriticalSection(&Cs);

    /* Correctness with heavy contention */
    RunExclusive(Threads, 1500);
    ok(Overlaps == 0, "%ld overlaps\n", Overlaps);
    ok(Errors == 0, "%ld recursion errors\n", Errors);
    ok(Cs.RecursionCount == 0, "RecursionCount %ld\n", Cs.RecursionCount);
    ok(Cs.OwningThread == NULL, "OwningThread %p\n", Cs.OwningThread);
    ok(Cs.LockCount == -1, "LockCount %ld\n", Cs.LockCount);

    TestWakeups(Threads);
    ok(Overlaps == 0, "%ld overlaps\n", Overlaps);
    ok(Cs.LockCount == -1, "LockCount %ld\n", Cs.LockCount);

    TestTryEnterOwned();

    /* Throughput: a contended critical section may be slower than an
     * uncontended one, but not by orders of magnitude (one context switch
     * per entry, a lock convoy, made it about 1000 times slower) */
    Single = RunExclusive(1, 500);
    Contended = RunExclusive(4, 1000);
    trace("Critical section: %.0f entries/ms with 1 thread, %.0f with 4 threads (%lu threads in the stress)\n",
          Single, Contended, Threads);
    ok(Contended * 30 >= Single, "Contended: %.0f entries/ms, uncontended %.0f\n", Contended, Single);
    ok(Overlaps == 0, "%ld overlaps\n", Overlaps);

    RtlDeleteCriticalSection(&Cs);
}
