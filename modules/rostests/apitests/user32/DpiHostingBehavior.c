/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for the DPI hosting behavior functions and GetPhysicalCursorPos
 *
 * Windows Forms (.NET 9) calls GetThreadDpiHostingBehavior and
 * SetThreadDpiHostingBehavior around window creation (Paint.NET 5 setup).
 */

#include "precomp.h"

typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_GET_THREAD)(VOID);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_SET_THREAD)(DPI_HOSTING_BEHAVIOR);
typedef DPI_HOSTING_BEHAVIOR (WINAPI *PFN_GET_WINDOW)(HWND);
typedef BOOL (WINAPI *PFN_GET_PHYSICAL_CURSOR_POS)(LPPOINT);

static PFN_GET_THREAD pGetThreadDpiHostingBehavior;
static PFN_SET_THREAD pSetThreadDpiHostingBehavior;

static DWORD WINAPI OtherThread(LPVOID Parameter)
{
    /* Each thread has its own value */
    *(DPI_HOSTING_BEHAVIOR *)Parameter = pGetThreadDpiHostingBehavior();
    return 0;
}

START_TEST(DpiHostingBehavior)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    PFN_GET_WINDOW pGetWindowDpiHostingBehavior;
    PFN_GET_PHYSICAL_CURSOR_POS pGetPhysicalCursorPos;
    DPI_HOSTING_BEHAVIOR Value, Other = DPI_HOSTING_BEHAVIOR_INVALID;
    POINT Physical, Logical;
    HANDLE Thread;
    HWND hwnd;

    pGetThreadDpiHostingBehavior = (PFN_GET_THREAD)GetProcAddress(User32, "GetThreadDpiHostingBehavior");
    pSetThreadDpiHostingBehavior = (PFN_SET_THREAD)GetProcAddress(User32, "SetThreadDpiHostingBehavior");
    pGetWindowDpiHostingBehavior = (PFN_GET_WINDOW)GetProcAddress(User32, "GetWindowDpiHostingBehavior");
    pGetPhysicalCursorPos = (PFN_GET_PHYSICAL_CURSOR_POS)GetProcAddress(User32, "GetPhysicalCursorPos");

    if (pGetPhysicalCursorPos)
    {
        ok(pGetPhysicalCursorPos(&Physical), "GetPhysicalCursorPos failed\n");
        GetCursorPos(&Logical);
        ok(Physical.x == Logical.x && Physical.y == Logical.y, "(%ld,%ld) vs (%ld,%ld)\n",
           Physical.x, Physical.y, Logical.x, Logical.y);
    }
    else
    {
        skip("GetPhysicalCursorPos is missing\n");
    }

    if (!pGetThreadDpiHostingBehavior || !pSetThreadDpiHostingBehavior || !pGetWindowDpiHostingBehavior)
    {
        skip("The DPI hosting behavior functions are missing (before Windows 10 1803)\n");
        return;
    }

    ok(pGetThreadDpiHostingBehavior() == DPI_HOSTING_BEHAVIOR_DEFAULT, "the default is %d\n", pGetThreadDpiHostingBehavior());

    Value = pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED);
    ok(Value == DPI_HOSTING_BEHAVIOR_DEFAULT, "Set returned %d\n", Value);
    ok(pGetThreadDpiHostingBehavior() == DPI_HOSTING_BEHAVIOR_MIXED, "Get returned %d\n", pGetThreadDpiHostingBehavior());

    Thread = CreateThread(NULL, 0, OtherThread, &Other, 0, NULL);
    ok(Thread != NULL, "CreateThread failed\n");
    if (Thread)
    {
        WaitForSingleObject(Thread, 10000);
        CloseHandle(Thread);
        ok(Other == DPI_HOSTING_BEHAVIOR_DEFAULT, "another thread has %d\n", Other);
    }

    SetLastError(0xdeadbeef);
    Value = pSetThreadDpiHostingBehavior((DPI_HOSTING_BEHAVIOR)5);
    ok(Value == DPI_HOSTING_BEHAVIOR_INVALID, "Set(5) returned %d\n", Value);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "error %lu\n", GetLastError());
    ok(pGetThreadDpiHostingBehavior() == DPI_HOSTING_BEHAVIOR_MIXED, "Get returned %d\n", pGetThreadDpiHostingBehavior());

    Value = pSetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_DEFAULT);
    ok(Value == DPI_HOSTING_BEHAVIOR_MIXED, "Set returned %d\n", Value);
    ok(pGetThreadDpiHostingBehavior() == DPI_HOSTING_BEHAVIOR_DEFAULT, "Get returned %d\n", pGetThreadDpiHostingBehavior());

    hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");
    ok(pGetWindowDpiHostingBehavior(hwnd) == DPI_HOSTING_BEHAVIOR_DEFAULT, "the window has %d\n",
       pGetWindowDpiHostingBehavior(hwnd));
    DestroyWindow(hwnd);
    ok(pGetWindowDpiHostingBehavior(hwnd) == DPI_HOSTING_BEHAVIOR_INVALID, "a destroyed window has %d\n",
       pGetWindowDpiHostingBehavior(hwnd));
}
