/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that idle processors take ready threads from busy ones
 *
 * One busy thread per processor is started, two of them on CPU 1 and none
 * on CPU 0 (affinities), then they may all run anywhere. CPU 0 goes idle
 * and must take the extra thread from CPU 1's ready queue: the busy
 * threads must run on every processor. Found: the scheduler never did
 * that (KiIdleSchedule was a stub), so N busy threads on N processors could
 * get N-1 processors' worth of time, and parallel programs barely got
 * faster with more processors.
 */

#include "precomp.h"

#define MAX_BUSY 64

static volatile LONG Stop;
static DWORD (WINAPI *pGetCurrentProcessorNumber)(void);   /* (Vista) */
static struct
{
    volatile ULONGLONG Count;
    volatile ULONGLONG CpuMask;   /* processors it ran on */
    ULONGLONG Pad[6];   /* (one cache line each) */
} Work[MAX_BUSY];

/* Counts its loops: the work done tells how much processor time it got */
static DWORD WINAPI BusyThread(LPVOID Param)
{
    ULONG_PTR Index = (ULONG_PTR)Param;
    while (!Stop)
    {
        if ((++Work[Index].Count & 0xFFFF) == 0)
            Work[Index].CpuMask |= 1ULL << (pGetCurrentProcessorNumber() & 63);
    }
    return 0;
}

START_TEST(IdleCpuPull)
{
    SYSTEM_INFO Info;
    HANDLE Threads[MAX_BUSY];
    ULONGLONG Before[MAX_BUSY], Total = 0, One, Used = 0, AllMask;
    DWORD Start, Elapsed;
    int N, i;
    volatile ULONG x;
    DWORD_PTR AllCpus;

    pGetCurrentProcessorNumber = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessorNumber");
    if (!pGetCurrentProcessorNumber)
    {
        skip("No GetCurrentProcessorNumber\n");
        return;
    }
    GetSystemInfo(&Info);
    N = min(MAX_BUSY, (int)Info.dwNumberOfProcessors);
    if (N < 2)
    {
        skip("Needs 2 processors or more (has %d)\n", N);
        return;
    }

    /* This thread runs on CPU 0, thread 0 and thread 1 both on CPU 1, the
       others one per processor. Then all of them may run anywhere, but a
       thread only moves when its processor leaves its affinity: CPU 1 keeps
       two of them queued */
    AllCpus = Info.dwActiveProcessorMask;
    SetThreadAffinityMask(GetCurrentThread(), 1);
    for (i = 0; i < N; i++)
    {
        Threads[i] = CreateThread(NULL, 0, BusyThread, (LPVOID)(ULONG_PTR)i, CREATE_SUSPENDED, NULL);
        ok(Threads[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
        SetThreadAffinityMask(Threads[i], (DWORD_PTR)1 << (i ? i : 1));
        ResumeThread(Threads[i]);
    }
    for (x = 0; x < 1000000; x++);
    for (i = 0; i < N; i++)
        SetThreadAffinityMask(Threads[i], AllCpus);
    SetThreadAffinityMask(GetCurrentThread(), AllCpus);

    /* Now leave this processor: N busy threads, N processors */
    Sleep(500);
    for (i = 0; i < N; i++)
    {
        Before[i] = Work[i].Count;
        Work[i].CpuMask = 0;
    }
    Start = GetTickCount();
    Sleep(2000);
    for (i = 0; i < N; i++)
    {
        Total += Work[i].Count - Before[i];
        Used |= Work[i].CpuMask;
    }
    Elapsed = GetTickCount() - Start;
    InterlockedExchange(&Stop, 1);
    WaitForMultipleObjects(N, Threads, TRUE, INFINITE);
    for (i = 0; i < N; i++)
        CloseHandle(Threads[i]);

    /* How much work one thread alone does in the same time */
    InterlockedExchange(&Stop, 0);
    Work[0].Count = 0;
    Threads[0] = CreateThread(NULL, 0, BusyThread, (LPVOID)0, 0, NULL);
    Sleep(200);
    Before[0] = Work[0].Count;
    Start = GetTickCount();
    Sleep(1000);
    One = (Work[0].Count - Before[0]) * Elapsed / max(1, GetTickCount() - Start);
    InterlockedExchange(&Stop, 1);
    WaitForSingleObject(Threads[0], INFINITE);
    CloseHandle(Threads[0]);

    /* Without the pull, CPU 0 stays idle and CPU 1 runs two threads: the
       busy threads never run on CPU 0 */
    AllMask = (N >= 64) ? ~0ULL : ((1ULL << N) - 1);
    ok((Used & AllMask) == AllMask, "the busy threads ran only on processors 0x%I64x of 0x%I64x\n", Used, AllMask);

    /* (for information: the host of a virtual machine, or SMT, can make this
       lower than N) */
    if (One)
        trace("%d busy threads on %d CPUs: the work of %lu.%02lu threads alone\n", N, N,
              (ULONG)(Total / One), (ULONG)(Total * 100 / One % 100));
}
