/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for BeginPanningFeedback, UpdatePanningFeedback and EndPanningFeedback
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Open-Shell and CCleaner import them (Windows 7 touch panning) and failed
 * to load without them.
 */

#include <apitest.h>
#include <windows.h>

typedef BOOL (WINAPI *PFN_BEGIN)(HWND);
typedef BOOL (WINAPI *PFN_UPDATE)(HWND, LONG, LONG, BOOL);
typedef BOOL (WINAPI *PFN_END)(HWND, BOOL);

START_TEST(PanningFeedback)
{
    HMODULE Uxtheme = LoadLibraryW(L"uxtheme.dll");
    PFN_BEGIN pBegin = (PFN_BEGIN)GetProcAddress(Uxtheme, "BeginPanningFeedback");
    PFN_UPDATE pUpdate = (PFN_UPDATE)GetProcAddress(Uxtheme, "UpdatePanningFeedback");
    PFN_END pEnd = (PFN_END)GetProcAddress(Uxtheme, "EndPanningFeedback");
    RECT Start, Moved, End;
    HWND hwnd;

    if (!pBegin || !pUpdate || !pEnd)
    {
        skip("The panning feedback functions are missing (before Windows 7)\n");
        return;
    }

    hwnd = CreateWindowExW(0, L"STATIC", L"Panning", WS_POPUP | WS_VISIBLE, 100, 100, 200, 150, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");
    if (!hwnd)
        return;
    GetWindowRect(hwnd, &Start);

    ok(pBegin(hwnd), "BeginPanningFeedback failed\n");
    ok(pUpdate(hwnd, 10, 20, FALSE), "UpdatePanningFeedback failed\n");
    GetWindowRect(hwnd, &Moved);
    ok(Moved.left == Start.left + 10 && Moved.top == Start.top + 20, "window at (%ld,%ld), started at (%ld,%ld)\n",
       Moved.left, Moved.top, Start.left, Start.top);
    ok(Moved.right - Moved.left == Start.right - Start.left, "the size changed\n");
    ok(pEnd(hwnd, FALSE), "EndPanningFeedback failed\n");
    GetWindowRect(hwnd, &End);
    ok(EqualRect(&End, &Start), "window at (%ld,%ld) after the end, started at (%ld,%ld)\n",
       End.left, End.top, Start.left, Start.top);

    DestroyWindow(hwnd);
    ok(!pBegin(hwnd), "BeginPanningFeedback succeeded on a destroyed window\n");
    FreeLibrary(Uxtheme);
}
