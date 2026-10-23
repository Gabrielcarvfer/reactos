/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stress test of thread exit on all processors
 *
 * Threads on every processor keep creating short-lived threads that use the
 * FPU/SSE registers and exit. Found: on SMP the thread reaper could free an
 * exiting thread's kernel stack while another processor was still switching
 * away from that thread, saving its extended state (XSAVE area) on that very
 * stack: a page fault at raised IRQL ("KeGetCurrentIrql() <= APC_LEVEL" in
 * MiLockWorkingSetShared, from KiSwapContextResume), seen at shutdown while
 * csrss terminated processes. Checks that all threads ran and computed the
 * right value. Runs ROSTEST_STRESS_SECONDS seconds (default 10).
 */

#include "precomp.h"

#define MAX_CREATORS 16

static volatile LONG Stop;
static volatile LONG Created, Wrong, Failed;

static DWORD WINAPI ShortThread(LPVOID Param)
{
    volatile double x = (double)(ULONG_PTR)Param;
    double y = 0.0;
    int i;

    /* Use the FPU/SSE registers, so the thread has extended state to save */
    for (i = 0; i < 100; i++)
        y += x * 0.5 + i;
    return (y == (double)(ULONG_PTR)Param * 50.0 + 4950.0) ? 1 : 0;
}

static DWORD WINAPI CreatorThread(LPVOID Param)
{
    HANDLE Threads[8];
    DWORD Code;
    ULONG_PTR n = 0;
    int i, Count;

    while (!Stop)
    {
        for (Count = 0; Count < ARRAYSIZE(Threads); Count++)
        {
            Threads[Count] = CreateThread(NULL, 0, ShortThread, (LPVOID)(++n & 0xFFFF), 0, NULL);
            if (!Threads[Count])
            {
                InterlockedIncrement(&Failed);
                break;
            }
            InterlockedIncrement(&Created);
        }
        if (Count)
            WaitForMultipleObjects(Count, Threads, TRUE, INFINITE);
        for (i = 0; i < Count; i++)
        {
            if (!GetExitCodeThread(Threads[i], &Code) || Code != 1)
                InterlockedIncrement(&Wrong);
            CloseHandle(Threads[i]);
        }
    }
    return 0;
}

START_TEST(ThreadExitStress)
{
    SYSTEM_INFO Info;
    HANDLE Creators[MAX_CREATORS];
    WCHAR Env[16];
    int Seconds, NCreators, i;

    GetSystemInfo(&Info);
    Seconds = GetEnvironmentVariableW(L"ROSTEST_STRESS_SECONDS", Env, 16) ? _wtoi(Env) : 10;
    if (Seconds <= 0)
        Seconds = 10;
    NCreators = min(MAX_CREATORS, (int)Info.dwNumberOfProcessors * 2);

    for (i = 0; i < NCreators; i++)
    {
        Creators[i] = CreateThread(NULL, 0, CreatorThread, NULL, 0, NULL);
        ok(Creators[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    Sleep(Seconds * 1000);
    InterlockedExchange(&Stop, 1);
    ok(WaitForMultipleObjects(NCreators, Creators, TRUE, 60000) == WAIT_OBJECT_0, "creators did not stop\n");
    for (i = 0; i < NCreators; i++)
        CloseHandle(Creators[i]);

    trace("%lu CPUs: %ld threads created and exited in %d s\n", Info.dwNumberOfProcessors, Created, Seconds);
    ok(Failed == 0, "CreateThread failed %ld times\n", Failed);
    ok(Wrong == 0, "%ld threads returned a wrong result\n", Wrong);
    ok(Created > NCreators, "only %ld threads created\n", Created);
}
