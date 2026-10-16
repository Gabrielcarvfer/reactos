/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for UnregisterWaitEx with a completion event
 *
 * UnregisterWaitEx(Wait, Event) must signal Event once the wait is
 * unregistered, also when no callback is running. Chromium's
 * ObjectWatcher::StopWatching waits on it: Spotify and VS Code froze.
 */

#include "precomp.h"

static volatile LONG Calls;

static
VOID
CALLBACK
WaitCallback(PVOID Parameter, BOOLEAN TimedOut)
{
    InterlockedIncrement(&Calls);
}

START_TEST(UnregisterWaitEx)
{
    HANDLE Object, Done, Wait;
    DWORD Result;
    BOOL Ret;

    Object = CreateEventW(NULL, FALSE, FALSE, NULL);
    Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Object != NULL && Done != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (!Object || !Done)
        return;

    /* Never fired, unregistered with an event */
    Ret = RegisterWaitForSingleObject(&Wait, Object, WaitCallback, NULL, INFINITE, WT_EXECUTEINWAITTHREAD);
    ok(Ret, "RegisterWaitForSingleObject failed with %lu\n", GetLastError());
    if (Ret)
    {
        Ret = UnregisterWaitEx(Wait, Done);
        ok(Ret || GetLastError() == ERROR_IO_PENDING, "UnregisterWaitEx(event) failed with %lu\n", GetLastError());
        Result = WaitForSingleObject(Done, 3000);
        ok(Result == WAIT_OBJECT_0, "Completion event not signaled (no callback ran), wait result %lu\n", Result);
    }
    ok(Calls == 0, "Callback ran %ld times\n", Calls);

    /* Fired once (WT_EXECUTEONLYONCE), then unregistered with an event */
    ResetEvent(Done);
    Ret = RegisterWaitForSingleObject(&Wait, Object, WaitCallback, NULL, INFINITE,
                                      WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE);
    ok(Ret, "RegisterWaitForSingleObject(once) failed with %lu\n", GetLastError());
    if (Ret)
    {
        SetEvent(Object);
        for (Result = 0; Result < 50 && Calls == 0; Result++)
            Sleep(20);
        ok(Calls == 1, "Callback ran %ld times, expected 1\n", Calls);
        Ret = UnregisterWaitEx(Wait, Done);
        ok(Ret || GetLastError() == ERROR_IO_PENDING,
           "UnregisterWaitEx(event) after the callback failed with %lu\n", GetLastError());
        Result = WaitForSingleObject(Done, 3000);
        ok(Result == WAIT_OBJECT_0, "Completion event not signaled (after the callback), wait result %lu\n", Result);
    }

    /* The blocking form */
    Ret = RegisterWaitForSingleObject(&Wait, Object, WaitCallback, NULL, INFINITE, WT_EXECUTEINWAITTHREAD);
    ok(Ret, "RegisterWaitForSingleObject failed with %lu\n", GetLastError());
    if (Ret)
    {
        ok(UnregisterWaitEx(Wait, INVALID_HANDLE_VALUE),
           "UnregisterWaitEx(INVALID_HANDLE_VALUE) failed with %lu\n", GetLastError());
    }

    CloseHandle(Done);
    CloseHandle(Object);
}
