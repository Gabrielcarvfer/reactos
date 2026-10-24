/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for SetPhysicalCursorPos (Vista)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* NVDA 2026 needs it: its winBindings.user32 binds it when loading */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GET_PHYSICAL_CURSOR_POS)(LPPOINT);
typedef BOOL (WINAPI *PFN_SET_PHYSICAL_CURSOR_POS)(int, int);

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
}
