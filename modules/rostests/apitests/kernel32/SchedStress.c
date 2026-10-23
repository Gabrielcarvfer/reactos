/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stress test of the scheduler: priority and affinity changes
 *              on threads that keep waking each other across processors
 *
 * Pairs of threads ping-pong two events, so they keep moving between
 * waiting, deferred ready, ready, standby and running, on all processors.
 * Other threads change the workers' priorities, boosts and affinities and
 * the process priority class all the time, and CPU-bound threads make
 * preemptions happen. Found: a priority change on a thread that sits on a
 * processor's deferred ready list hit "Deferred state not yet supported"
 * (ke/thrdschd.c) on SMP. Checks: no wakeup is lost (no wait times out),
 * priorities read back what was set last.
 * Runs ROSTEST_STRESS_SECONDS seconds (default 10).
 */

#include "precomp.h"

#define MAX_PAIRS 32
#define MAX_BUSY  8

typedef struct
{
    HANDLE Event[2];
    HANDLE Thread[2];
    volatile LONG Trips;
    volatile LONG Timeouts;
} PAIR;

static PAIR Pairs[MAX_PAIRS];
static HANDLE BusyThreads[MAX_BUSY];
static int NPairs, NBusy;
static DWORD NumCpus;
static DWORD_PTR AllCpus;
static volatile LONG Stop;
static volatile LONG Changes;

static const int Priorities[] =
{
    THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
    THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST,
    THREAD_PRIORITY_TIME_CRITICAL
};

static const DWORD Classes[] =
{
    IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
    ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS
};

static ULONG Random(ULONG *Seed)
{
    *Seed = *Seed * 1103515245 + 12345;
    return (*Seed >> 16) & 0x7FFF;
}

/* One side of a pair: wake the other one, wait to be woken */
static DWORD WINAPI PairThread(LPVOID Param)
{
    PAIR *Pair = &Pairs[(ULONG_PTR)Param >> 1];
    int Side = (ULONG_PTR)Param & 1;

    if (Side == 0)
        SetEvent(Pair->Event[1]);
    while (!Stop)
    {
        if (WaitForSingleObject(Pair->Event[Side], 20000) != WAIT_OBJECT_0)
        {
            if (!Stop)
                InterlockedIncrement(&Pair->Timeouts);
            break;
        }
        if (Side == 0)
            InterlockedIncrement(&Pair->Trips);
        SetEvent(Pair->Event[!Side]);
    }
    /* Let the other side see Stop */
    SetEvent(Pair->Event[!Side]);
    return 0;
}

/* CPU-bound threads, so that readied threads preempt running and standby ones */
static DWORD WINAPI BusyThread(LPVOID Param)
{
    volatile ULONG x = 0;
    ULONG Seed = (ULONG)(ULONG_PTR)Param;

    while (!Stop)
    {
        for (x = 0; x < 20000; x++);
        if ((Random(&Seed) & 15) == 0)
            Sleep(0);
    }
    return 0;
}

/* Change the workers' priorities, boost and affinities */
static DWORD WINAPI ChangerThread(LPVOID Param)
{
    ULONG Seed = (ULONG)(ULONG_PTR)Param * 7919 + GetTickCount();
    HANDLE Thread;
    ULONG r;

    while (!Stop)
    {
        r = Random(&Seed);
        Thread = Pairs[r % NPairs].Thread[(r >> 5) & 1];
        switch ((r >> 6) % 8)
        {
            case 0: case 1: case 2: case 3: case 4:
                SetThreadPriority(Thread, Priorities[Random(&Seed) % ARRAYSIZE(Priorities)]);
                break;
            case 5:
                SetThreadPriorityBoost(Thread, Random(&Seed) & 1);
                break;
            case 6:
                if (NumCpus > 1)
                {
                    DWORD_PTR Mask = (DWORD_PTR)Random(&Seed) * 0x10001 & AllCpus;
                    SetThreadAffinityMask(Thread, Mask ? Mask : AllCpus);
                }
                break;
            default:
                SetThreadAffinityMask(Thread, AllCpus);
                break;
        }
        InterlockedIncrement(&Changes);

        /* Leave time to the workers: at a higher priority than theirs,
           never waiting, this thread would starve those that can only
           run on its processor (Windows does the same) */
        if ((r & 31) == 0)
            Sleep(1);
    }
    return 0;
}

/* Change the priority class: changes every thread's priority at once */
static DWORD WINAPI ClassThread(LPVOID Param)
{
    ULONG Seed = GetTickCount();

    while (!Stop)
    {
        SetPriorityClass(GetCurrentProcess(), Classes[Random(&Seed) % ARRAYSIZE(Classes)]);
        InterlockedIncrement(&Changes);
        Sleep(Random(&Seed) & 3);
    }
    return 0;
}

START_TEST(SchedStress)
{
    SYSTEM_INFO Info;
    HANDLE Changers[4], Class;
    WCHAR Env[16];
    LONG Before[MAX_PAIRS];
    int Seconds, i, s, NChangers;
    LONG Timeouts = 0, Stuck = 0, Trips = 0, Wrong = 0;
    DWORD Start;

    GetSystemInfo(&Info);
    NumCpus = Info.dwNumberOfProcessors;
    AllCpus = Info.dwActiveProcessorMask;
    Seconds = GetEnvironmentVariableW(L"ROSTEST_STRESS_SECONDS", Env, 16) ? _wtoi(Env) : 10;
    if (Seconds <= 0)
        Seconds = 10;

    NPairs = min(MAX_PAIRS, max(4, (int)NumCpus * 2));
    NBusy = min(MAX_BUSY, (int)NumCpus);
    NChangers = min(4, max(2, (int)NumCpus / 2));
    trace("%lu CPUs, %d pairs, %d busy threads, %d changers, %d s\n",
          NumCpus, NPairs, NBusy, NChangers, Seconds);

    for (i = 0; i < NPairs; i++)
    {
        Pairs[i].Event[0] = CreateEventW(NULL, FALSE, FALSE, NULL);
        Pairs[i].Event[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(Pairs[i].Event[0] && Pairs[i].Event[1], "CreateEvent failed: %lu\n", GetLastError());
        for (s = 0; s < 2; s++)
        {
            Pairs[i].Thread[s] = CreateThread(NULL, 0, PairThread, (LPVOID)(ULONG_PTR)(i * 2 + s), 0, NULL);
            ok(Pairs[i].Thread[s] != NULL, "CreateThread failed: %lu\n", GetLastError());
        }
    }
    for (i = 0; i < NBusy; i++)
    {
        BusyThreads[i] = CreateThread(NULL, 0, BusyThread, (LPVOID)(ULONG_PTR)(i + 1), 0, NULL);
        SetThreadPriority(BusyThreads[i], THREAD_PRIORITY_IDLE);
    }
    for (i = 0; i < NChangers; i++)
    {
        Changers[i] = CreateThread(NULL, 0, ChangerThread, (LPVOID)(ULONG_PTR)(i + 1), 0, NULL);
        SetThreadPriority(Changers[i], THREAD_PRIORITY_HIGHEST);
    }
    Class = CreateThread(NULL, 0, ClassThread, NULL, 0, NULL);
    SetThreadPriority(Class, THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* Every 5 seconds: count the pairs that made no progress */
    for (i = 0; i < NPairs; i++)
        Before[i] = Pairs[i].Trips;
    Start = GetTickCount();
    for (s = 0; s < Seconds; s++)
    {
        /* (the busy threads have the lowest priority, so no pair starves) */
        Sleep(1000);
        if ((s % 5) != 4)
            continue;
        for (i = 0; i < NPairs; i++)
        {
            if (Pairs[i].Trips == Before[i])
                Stuck++;
            Before[i] = Pairs[i].Trips;
        }
    }

    InterlockedExchange(&Stop, 1);
    WaitForMultipleObjects(NChangers, Changers, TRUE, 30000);
    WaitForSingleObject(Class, 30000);
    for (i = 0; i < NBusy; i++)
    {
        ok(WaitForSingleObject(BusyThreads[i], 30000) == WAIT_OBJECT_0, "busy thread %d did not stop\n", i);
        CloseHandle(BusyThreads[i]);
    }
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    /* The workers are still alive (they stop through the events): their
       priorities must read back what is set now */
    for (i = 0; i < NPairs; i++)
    {
        for (s = 0; s < 2; s++)
        {
            int Prio = Priorities[(i + s) % ARRAYSIZE(Priorities)];
            if (!SetThreadPriority(Pairs[i].Thread[s], Prio) ||
                GetThreadPriority(Pairs[i].Thread[s]) != Prio)
            {
                Wrong++;
            }
        }
    }

    for (i = 0; i < NPairs; i++)
    {
        SetEvent(Pairs[i].Event[0]);
        SetEvent(Pairs[i].Event[1]);
        for (s = 0; s < 2; s++)
        {
            ok(WaitForSingleObject(Pairs[i].Thread[s], 30000) == WAIT_OBJECT_0, "pair %d thread %d did not stop\n", i, s);
            CloseHandle(Pairs[i].Thread[s]);
        }
        CloseHandle(Pairs[i].Event[0]);
        CloseHandle(Pairs[i].Event[1]);
        Timeouts += Pairs[i].Timeouts;
        Trips += Pairs[i].Trips;
    }
    for (i = 0; i < NChangers; i++)
        CloseHandle(Changers[i]);
    CloseHandle(Class);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    trace("%ld round trips, %ld changes in %lu ms\n", Trips, Changes, GetTickCount() - Start);
    ok(Timeouts == 0, "%ld waits timed out (lost wakeups)\n", Timeouts);
    /* (not a failure: a pair at the lowest priority can wait for the others
       until the balance set manager boosts it) */
    if (Stuck)
        trace("%ld times a pair made no progress for 5 s\n", Stuck);
    ok(Wrong == 0, "%ld threads with a wrong priority\n", Wrong);
    ok(Trips > NPairs, "only %ld round trips\n", Trips);
}
