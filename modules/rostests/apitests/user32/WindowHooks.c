/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for thread window hooks (WH_CBT, WH_GETMESSAGE, WH_CALLWNDPROC, WH_CALLWNDPROCRET)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * What programs do with hooks on their own thread: move a window from
 * HCBT_CREATEWND, change a posted message in WH_GETMESSAGE, watch sent
 * messages and WM_CREATE. Written while bringing up 32-bit programs on
 * 64-bit ReactOS (WoW64), where the hook callbacks cross the 32/64 boundary.
 */

#include "precomp.h"

static int g_CbtCreate, g_CbtActivate, g_GetMsgChanged, g_CwpSent, g_CwpRetCreate, g_Seen42;
static HHOOK g_HookCbt, g_HookGetMsg, g_HookCwp, g_HookCwpRet;

static LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND)
    {
        CBT_CREATEWNDW *cw = (CBT_CREATEWNDW *)lp;
        if (cw->lpcs->cx == 300)
        {
            cw->lpcs->x = 123;
            cw->lpcs->y = 77;
            g_CbtCreate++;
        }
    }
    if (code == HCBT_ACTIVATE)
        g_CbtActivate++;
    return CallNextHookEx(g_HookCbt, code, wp, lp);
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp)
{
    MSG *msg = (MSG *)lp;
    if (code == HC_ACTION && msg->message == WM_USER + 1 && msg->wParam == 1)
    {
        msg->wParam = 42; /* the window must see 42 */
        g_GetMsgChanged++;
    }
    return CallNextHookEx(g_HookGetMsg, code, wp, lp);
}

static LRESULT CALLBACK CwpProc(int code, WPARAM wp, LPARAM lp)
{
    CWPSTRUCT *cwp = (CWPSTRUCT *)lp;
    if (cwp->message == WM_USER + 2 && cwp->wParam == 7)
        g_CwpSent++;
    return CallNextHookEx(g_HookCwp, code, wp, lp);
}

static LRESULT CALLBACK CwpRetProc(int code, WPARAM wp, LPARAM lp)
{
    CWPRETSTRUCT *cwpr = (CWPRETSTRUCT *)lp;
    if (cwpr->message == WM_CREATE && cwpr->lParam)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)cwpr->lParam;
        if (cs->cx == 300)
            g_CwpRetCreate++;
    }
    return CallNextHookEx(g_HookCwpRet, code, wp, lp);
}

static LRESULT CALLBACK HookTestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER + 1 && wp == 42)
        g_Seen42 = 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

START_TEST(WindowHooks)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    RECT rc = { 0 };
    MSG msg;
    DWORD tid = GetCurrentThreadId();
    DWORD start;

    g_HookCbt = SetWindowsHookExW(WH_CBT, CbtProc, NULL, tid);
    ok(g_HookCbt != NULL, "SetWindowsHookExW(WH_CBT) failed (%lu)\n", GetLastError());
    g_HookGetMsg = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, NULL, tid);
    ok(g_HookGetMsg != NULL, "SetWindowsHookExW(WH_GETMESSAGE) failed (%lu)\n", GetLastError());
    g_HookCwp = SetWindowsHookExW(WH_CALLWNDPROC, CwpProc, NULL, tid);
    ok(g_HookCwp != NULL, "SetWindowsHookExW(WH_CALLWNDPROC) failed (%lu)\n", GetLastError());
    g_HookCwpRet = SetWindowsHookExW(WH_CALLWNDPROCRET, CwpRetProc, NULL, tid);
    ok(g_HookCwpRet != NULL, "SetWindowsHookExW(WH_CALLWNDPROCRET) failed (%lu)\n", GetLastError());

    wc.lpfnWndProc = HookTestWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WindowHooksTest";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed (%lu)\n", GetLastError());
    hwnd = CreateWindowW(L"WindowHooksTest", L"hooked", WS_OVERLAPPEDWINDOW, 10, 10, 300, 200,
                         NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowW failed (%lu)\n", GetLastError());
    if (!hwnd)
    {
        skip("No window\n");
        goto cleanup;
    }

    GetWindowRect(hwnd, &rc);
    ok(g_CbtCreate == 1, "HCBT_CREATEWND seen %d times\n", g_CbtCreate);
    ok(rc.left == 123 && rc.top == 77, "HCBT_CREATEWND did not move the window: at %ld,%ld\n", rc.left, rc.top);
    ok(g_CwpRetCreate > 0, "WH_CALLWNDPROCRET did not see WM_CREATE\n");

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    ok(g_CbtActivate > 0, "HCBT_ACTIVATE not seen\n");

    SendMessageW(hwnd, WM_USER + 2, 7, 0);
    ok(g_CwpSent == 1, "WH_CALLWNDPROC saw the sent message %d times\n", g_CwpSent);

    PostMessageW(hwnd, WM_USER + 1, 1, 0);
    PostQuitMessage(0);
    start = GetTickCount();
    while (GetTickCount() - start < 5000 && GetMessageW(&msg, NULL, 0, 0) > 0)
        DispatchMessageW(&msg);
    ok(g_GetMsgChanged == 1, "WH_GETMESSAGE changed the message %d times\n", g_GetMsgChanged);
    ok(g_Seen42 == 1, "the window did not get the wParam set by WH_GETMESSAGE\n");

    DestroyWindow(hwnd);

cleanup:
    if (g_HookCbt)
        ok(UnhookWindowsHookEx(g_HookCbt), "UnhookWindowsHookEx(WH_CBT) failed\n");
    if (g_HookGetMsg)
        ok(UnhookWindowsHookEx(g_HookGetMsg), "UnhookWindowsHookEx(WH_GETMESSAGE) failed\n");
    if (g_HookCwp)
        ok(UnhookWindowsHookEx(g_HookCwp), "UnhookWindowsHookEx(WH_CALLWNDPROC) failed\n");
    if (g_HookCwpRet)
        ok(UnhookWindowsHookEx(g_HookCwpRet), "UnhookWindowsHookEx(WH_CALLWNDPROCRET) failed\n");
    UnregisterClassW(L"WindowHooksTest", GetModuleHandleW(NULL));
}
