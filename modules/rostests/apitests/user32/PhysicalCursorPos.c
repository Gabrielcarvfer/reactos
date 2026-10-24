/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for SetPhysicalCursorPos, the physical point functions
 *              and RegisterPointerInputTarget
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* NVDA 2026 binds all of these when it starts (winBindings.user32) */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GET_PHYSICAL_CURSOR_POS)(LPPOINT);
typedef BOOL (WINAPI *PFN_SET_PHYSICAL_CURSOR_POS)(int, int);
typedef BOOL (WINAPI *PFN_CONVERT_POINT)(HWND, LPPOINT);
typedef HWND (WINAPI *PFN_WINDOW_FROM_PHYSICAL_POINT)(POINT);
typedef BOOL (WINAPI *PFN_POINTER_INPUT_TARGET)(HWND, DWORD);

static void TestConversions(HMODULE User32)
{
    static const char *Names[] = { "LogicalToPhysicalPoint", "PhysicalToLogicalPoint",
                                   "LogicalToPhysicalPointForPerMonitorDPI", "PhysicalToLogicalPointForPerMonitorDPI" };
    PFN_WINDOW_FROM_PHYSICAL_POINT pWindowFromPhysicalPoint;
    PFN_POINTER_INPUT_TARGET pRegister, pUnregister;
    HWND hwnd;
    POINT pt;
    ULONG i;

    /* At 100% scale (as ReactOS always is) the coordinates don't change */
    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 100, 100, 50, 50, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");
    for (i = 0; i < _countof(Names); i++)
    {
        PFN_CONVERT_POINT pConvert = (PFN_CONVERT_POINT)GetProcAddress(User32, Names[i]);
        ok(pConvert != NULL, "%s is missing\n", Names[i]);
        if (!pConvert)
            continue;
        pt.x = 110;
        pt.y = 120;
        pConvert(hwnd, &pt);
        ok(pt.x == 110 && pt.y == 120, "%s: (%ld,%ld)\n", Names[i], pt.x, pt.y);
    }

    pWindowFromPhysicalPoint = (PFN_WINDOW_FROM_PHYSICAL_POINT)GetProcAddress(User32, "WindowFromPhysicalPoint");
    ok(pWindowFromPhysicalPoint != NULL, "WindowFromPhysicalPoint is missing\n");
    if (pWindowFromPhysicalPoint)
    {
        pt.x = 120;
        pt.y = 120;
        ok(pWindowFromPhysicalPoint(pt) == WindowFromPoint(pt), "Not the window of WindowFromPoint\n");
    }

    /* Only a process with UIAccess may take all the pointer input */
    pRegister = (PFN_POINTER_INPUT_TARGET)GetProcAddress(User32, "RegisterPointerInputTarget");
    pUnregister = (PFN_POINTER_INPUT_TARGET)GetProcAddress(User32, "UnregisterPointerInputTarget");
    ok(pRegister != NULL && pUnregister != NULL, "RegisterPointerInputTarget/UnregisterPointerInputTarget are missing\n");
    if (pRegister && pUnregister)
    {
        SetLastError(0xdeadbeef);
        ok(!pRegister(hwnd, 2 /* PT_TOUCH */), "RegisterPointerInputTarget succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "RegisterPointerInputTarget: error %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        ok(!pUnregister(hwnd, 2 /* PT_TOUCH */), "UnregisterPointerInputTarget succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "UnregisterPointerInputTarget: error %lu\n", GetLastError());
    }

    DestroyWindow(hwnd);
}

START_TEST(PhysicalCursorPos)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    PFN_GET_PHYSICAL_CURSOR_POS pGetPhysicalCursorPos;
    PFN_SET_PHYSICAL_CURSOR_POS pSetPhysicalCursorPos;
    POINT Old, New;

    pGetPhysicalCursorPos = (PFN_GET_PHYSICAL_CURSOR_POS)GetProcAddress(User32, "GetPhysicalCursorPos");
    pSetPhysicalCursorPos = (PFN_SET_PHYSICAL_CURSOR_POS)GetProcAddress(User32, "SetPhysicalCursorPos");
    ok(pGetPhysicalCursorPos != NULL, "GetPhysicalCursorPos is missing\n");
    ok(pSetPhysicalCursorPos != NULL, "SetPhysicalCursorPos is missing\n");
    if (!pGetPhysicalCursorPos || !pSetPhysicalCursorPos)
        return;

    ok(pGetPhysicalCursorPos(&Old), "GetPhysicalCursorPos failed: %lu\n", GetLastError());
    ok(pSetPhysicalCursorPos(10, 20), "SetPhysicalCursorPos failed: %lu\n", GetLastError());
    ok(pGetPhysicalCursorPos(&New), "GetPhysicalCursorPos failed: %lu\n", GetLastError());
    ok(New.x == 10 && New.y == 20, "The cursor is at (%ld,%ld)\n", New.x, New.y);
    pSetPhysicalCursorPos(Old.x, Old.y);

    TestConversions(User32);
}
