/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exceptions raised in a window procedure called back from the kernel
 *
 * DispatchMessage calls the window procedure through a kernel callback
 * (KiUserCallbackDispatcher). An exception raised there is handled by code
 * that called DispatchMessage (.NET programs: Windows Forms' message loop).
 * On amd64 the unwinder must get through KiUserCallbackDispatcher: the
 * kernel entered it with a zero rbp, so frames using rbp as the frame
 * pointer could not be unwound, the unwinder faulted, and every fault
 * started a new exception dispatch until the stack was gone (Paint.NET's
 * setup). Unwinding out of the callback must also end the callback in the
 * kernel, so that the thread can go on dispatching messages.
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#define TEST_EXCEPTION 0xE0001234
#define WM_RAISE (WM_USER + 1)
#define WM_PING (WM_USER + 2)

static LONG Raised, Pinged;

static
LRESULT
CALLBACK
RaiseWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_RAISE:
            Raised++;
            RaiseException(TEST_EXCEPTION, 0, 0, NULL);
            return 0;
        case WM_PING:
            Pinged++;
            return 42;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Dispatches one posted WM_RAISE inside __try; returns how it ended */
static
int
DispatchRaise(HWND hwnd)
{
    volatile int Result = 0;
    MSG msg;

    PostMessageW(hwnd, WM_RAISE, 0, 0);
    if (!GetMessageW(&msg, hwnd, WM_RAISE, WM_RAISE))
        return -1;

    _SEH2_TRY
    {
        DispatchMessageW(&msg);
        Result = 1; /* the exception stopped at the callback boundary */
    }
    _SEH2_EXCEPT(_SEH2_GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = 2; /* handled here, after DispatchMessage */
    }
    _SEH2_END;
    return Result;
}

/* Sends WM_RAISE inside __try (same thread: no kernel callback on Windows) */
static
int
SendRaise(HWND hwnd)
{
    volatile int Result = 0;

    _SEH2_TRY
    {
        SendMessageW(hwnd, WM_RAISE, 0, 0);
        Result = 1;
    }
    _SEH2_EXCEPT(_SEH2_GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        Result = 2;
    }
    _SEH2_END;
    return Result;
}

START_TEST(WndProcException)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    int i, Result;
    MSG msg;

    wc.lpfnWndProc = RaiseWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WndProcExceptionTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!hwnd)
        return;

    /* Several times: a callback left behind in the kernel would show up */
    for (i = 0; i < 5; i++)
    {
        Result = DispatchRaise(hwnd);
        ok(Result == 1 || Result == 2, "round %d: result %d\n", i, Result);
        if (i == 0)
            trace("The exception %s\n", Result == 2 ? "reached the caller of DispatchMessage" : "stopped at the callback");

        /* The thread still works: a sent and a posted message */
        ok(SendMessageW(hwnd, WM_PING, 0, 0) == 42, "round %d: SendMessage failed\n", i);
        PostMessageW(hwnd, WM_PING, 0, 0);
        ok(GetMessageW(&msg, hwnd, WM_PING, WM_PING) > 0, "round %d: GetMessage failed\n", i);
        ok(DispatchMessageW(&msg) == 42, "round %d: DispatchMessage of WM_PING failed\n", i);
    }
    /* SendMessage to a window of the same thread */
    ok(SendRaise(hwnd) == 2, "the exception from SendMessage was not caught\n");
    ok(SendMessageW(hwnd, WM_PING, 0, 0) == 42, "SendMessage failed after the exception\n");

    ok(Raised == 6, "the window procedure raised %ld times\n", Raised);
    ok(Pinged == 11, "WM_PING arrived %ld times\n", Pinged);

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
