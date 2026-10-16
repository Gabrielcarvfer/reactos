/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that QueryPerformanceCounter is monotonic across CPUs
 *
 * Chromium's base::TimeTicks::Now() uses QueryPerformanceCounter and CHECKs
 * that time never goes backwards (VS Code's renderer crashed in
 * MemoryCache::PruneStrongReferences, "negative frame latency"). One thread
 * per CPU reads the counter and compares it with the latest value any thread
 * published.
 */

#include "precomp.h"

#define RUN_MS 300

static volatile LONG64 Latest;
static volatile LONG Backwards;
static volatile LONG64 WorstTicks;
static LARGE_INTEGER Frequency;

static
DWORD
WINAPI
Worker(LPVOID Parameter)
{
    LARGE_INTEGER Now, End;
    LONG64 Seen, Old;

    if (Parameter)
        SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)Parameter);

    QueryPerformanceCounter(&End);
    End.QuadPart += Frequency.QuadPart * RUN_MS / 1000;
    do
    {
        /* Atomic 64-bit read, also on x86 */
        Seen = InterlockedCompareExchange64(&Latest, 0, 0);
        QueryPerformanceCounter(&Now);
        if (Now.QuadPart < Seen)
        {
            InterlockedIncrement(&Backwards);
            if (Seen - Now.QuadPart > WorstTicks)
                WorstTicks = Seen - Now.QuadPart;
        }
        else
        {
            /* Publish it if it is the largest */
            while ((Old = InterlockedCompareExchange64(&Latest, 0, 0)) < Now.QuadPart &&
                   InterlockedCompareExchange64(&Latest, Now.QuadPart, Old) != Old)
                ;
        }
    } while (Now.QuadPart < End.QuadPart);

    return 0;
}

START_TEST(QueryPerformanceCounter)
{
    DWORD_PTR ProcessMask, SystemMask;
    HANDLE Threads[MAXIMUM_WAIT_OBJECTS];
    DWORD Count = 0, i, Result;
    LARGE_INTEGER Start, Stop;
    DWORD TickStart, TickStop;
    LONG64 QpcMs;

    ok(QueryPerformanceFrequency(&Frequency), "QueryPerformanceFrequency failed with %lu\n", GetLastError());
    if (Frequency.QuadPart <= 0)
    {
        skip("No performance counter\n");
        return;
    }

    ok(GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask),
       "GetProcessAffinityMask failed with %lu\n", GetLastError());

    /* One thread per CPU */
    for (i = 0; i < sizeof(DWORD_PTR) * 8 && Count < _countof(Threads); i++)
    {
        if (!(ProcessMask & ((DWORD_PTR)1 << i)))
            continue;
        Threads[Count] = CreateThread(NULL, 0, Worker, (LPVOID)((DWORD_PTR)1 << i), 0, NULL);
        ok(Threads[Count] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Threads[Count])
            Count++;
    }
    if (Count)
    {
        Result = WaitForMultipleObjects(Count, Threads, TRUE, 10000);
        ok(Result < WAIT_OBJECT_0 + Count, "Waiting for the threads returned %lu\n", Result);
        for (i = 0; i < Count; i++)
            CloseHandle(Threads[i]);
    }
    ok(Backwards == 0, "QPC went backwards %ld times across %lu CPU threads, worst %I64d ticks (frequency %I64d)\n",
       Backwards, Count, WorstTicks, Frequency.QuadPart);

    /* One thread that may move between CPUs */
    Backwards = 0;
    WorstTicks = 0;
    Latest = 0;
    Threads[0] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Threads[0])
    {
        Result = WaitForSingleObject(Threads[0], 10000);
        ok(Result == WAIT_OBJECT_0, "Waiting for the thread returned %lu\n", Result);
        CloseHandle(Threads[0]);
    }
    ok(Backwards == 0, "QPC went backwards %ld times in one floating thread, worst %I64d ticks\n",
       Backwards, WorstTicks);

    /* The counter's rate agrees with the tick count */
    QueryPerformanceCounter(&Start);
    TickStart = GetTickCount();
    Sleep(1000);
    QueryPerformanceCounter(&Stop);
    TickStop = GetTickCount();
    QpcMs = (Stop.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart;
    ok(QpcMs >= (LONG64)(TickStop - TickStart) - 100 && QpcMs <= (LONG64)(TickStop - TickStart) + 100,
       "1 s is %I64d ms by QPC and %lu ms by the tick count\n", QpcMs, TickStop - TickStart);
}
