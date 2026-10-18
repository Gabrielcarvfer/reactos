/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     GetKeyState follows the input messages the thread removed,
 *              not the ones PeekMessage(PM_NOREMOVE) or a filter looked at
 *
 * Chromium/VS Code look ahead for WM_CHAR with PeekMessage(PM_NOREMOVE):
 * when that moved the key state, "hello2.js" was typed as "HELLO@>JS".
 */

#include "precomp.h"

static void SendKey(WORD wVk, BOOL bUp)
{
    INPUT input = { INPUT_KEYBOARD };
    input.ki.wVk = wVk;
    input.ki.wScan = (WORD)MapVirtualKeyW(wVk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = bUp ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(input));
}

START_TEST(KeyStatePeek)
{
    HWND hwnd;
    MSG msg;
    int nKeyDown = 0;
    BOOL bShiftAtA = FALSE, bShiftAtB = TRUE;
    DWORD dwStart;

    hwnd = CreateWindowW(L"EDIT", L"KeyStatePeek", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         100, 100, 300, 100, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindow failed\n");
    if (!hwnd)
        return;
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
    if (GetForegroundWindow() != hwnd)
    {
        skip("Can't be the foreground window\n");
        DestroyWindow(hwnd);
        return;
    }

    /* Shift+A, then B: all queued before we look */
    SendKey(VK_SHIFT, FALSE);
    SendKey('A', FALSE);
    SendKey('A', TRUE);
    SendKey(VK_SHIFT, TRUE);
    SendKey('B', FALSE);
    SendKey('B', TRUE);
    Sleep(200);

    /* Look at everything without removing it, and with a filter that
       matches none of the queued messages */
    ok(PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE), "no message queued\n");
    PeekMessageW(&msg, NULL, WM_CHAR, WM_CHAR, PM_NOREMOVE);
    PeekMessageW(&msg, NULL, WM_KEYUP, WM_KEYUP, PM_NOREMOVE);
    ok(!(GetKeyState(VK_SHIFT) & 0x8000), "Shift down before any key message was removed\n");

    dwStart = GetTickCount();
    while (nKeyDown < 2 && GetTickCount() - dwStart < 3000)
    {
        if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            Sleep(10);
            continue;
        }
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'A' || msg.wParam == 'B'))
        {
            /* What Chromium does: look ahead for the WM_CHAR */
            MSG next;
            PeekMessageW(&next, NULL, WM_CHAR, WM_DEADCHAR, PM_NOREMOVE);
            PeekMessageW(&next, NULL, 0, 0, PM_NOREMOVE);
            if (msg.wParam == 'A')
                bShiftAtA = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            else
                bShiftAtB = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            nKeyDown++;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ok(nKeyDown == 2, "got %d key downs\n", nKeyDown);
    ok(bShiftAtA, "Shift not down while handling Shift+A\n");
    ok(!bShiftAtB, "Shift still down while handling B\n");

    DestroyWindow(hwnd);
}
