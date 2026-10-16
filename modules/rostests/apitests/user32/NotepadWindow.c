/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for what a Notepad-like window program does (child controls, subclassing, regions)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Written while bringing up 32-bit window programs on 64-bit ReactOS (WoW64):
 * the process heap got damaged by the window calls, and the update region
 * did not match regions made by gdi32. The steps are Notepad's: a multiline
 * edit box created without a size, subclassed, given a font, sized later.
 */

#include "precomp.h"

static WNDPROC g_OldEditProc;
static int g_Created, g_EditSubclassCalls;

static LRESULT CALLBACK EditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    g_EditSubclassCalls++;
    return CallWindowProcW(g_OldEditProc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK NotepadWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE)
        g_Created = 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL HeapOk(void)
{
    return HeapValidate(GetProcessHeap(), 0, NULL);
}

static void PumpMessages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void CheckUpdateRect(HWND hwnd, HRGN rgn, LONG l, LONG t, LONG r, LONG b, const char *what)
{
    RECT upd = { 0 };
    BOOL ret;

    ValidateRect(hwnd, NULL);
    InvalidateRgn(hwnd, rgn, TRUE);
    ret = GetUpdateRect(hwnd, &upd, FALSE);
    ok(ret && upd.left == l && upd.top == t && upd.right == r && upd.bottom == b,
       "InvalidateRgn(%s): update %d %ld,%ld-%ld,%ld, expected %ld,%ld-%ld,%ld\n",
       what, ret, upd.left, upd.top, upd.right, upd.bottom, l, t, r, b);
    ValidateRect(hwnd, NULL);
}

START_TEST(NotepadWindow)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd, button, edit, ml;
    LOGFONTW lf = { 0 };
    HFONT font = NULL;
    HRGN r1, r2, r3, r4;
    RECT box;
    WCHAR text[128];
    int type;

    ok(HeapOk(), "process heap damaged before the test\n");

    wc.lpfnWndProc = NotepadWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NotepadWindowTest";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed (%lu)\n", GetLastError());
    ok(HeapOk(), "process heap damaged by RegisterClassW\n");

    hwnd = CreateWindowW(L"NotepadWindowTest", L"32-bit window", WS_OVERLAPPEDWINDOW, 100, 100, 520, 320,
                         NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowW failed (%lu)\n", GetLastError());
    ok(g_Created, "WM_CREATE not received\n");
    ok(HeapOk(), "process heap damaged by CreateWindowW\n");
    if (!hwnd)
    {
        skip("No window\n");
        goto cleanup;
    }

    button = CreateWindowW(L"BUTTON", L"A button", WS_CHILD | WS_VISIBLE,
                           260, 60, 180, 40, hwnd, (HMENU)1, NULL, NULL);
    ok(button != NULL, "BUTTON not created (%lu)\n", GetLastError());
    edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Text in an edit box",
                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           20, 140, 420, 28, hwnd, (HMENU)2, NULL, NULL);
    ok(edit != NULL, "EDIT not created (%lu)\n", GetLastError());

    /* Like Notepad: multiline, scroll bars, its font, no size yet */
    lf.lfHeight = -16;
    lstrcpyW(lf.lfFaceName, L"Lucida Console");
    font = CreateFontIndirectW(&lf);
    ok(font != NULL, "CreateFontIndirectW failed\n");
    ml = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Multiline edit\r\nsecond line",
                         WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL | WS_HSCROLL | ES_NOHIDESEL,
                         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                         hwnd, (HMENU)3, NULL, NULL);
    ok(ml != NULL, "multiline EDIT not created (%lu)\n", GetLastError());
    if (ml)
    {
        SendMessageW(ml, WM_SETFONT, (WPARAM)font, FALSE);
        SendMessageW(ml, EM_SETSEL, 1000, 1000);
        SendMessageW(ml, EM_REPLACESEL, FALSE, (LPARAM)L" + replaced");
        ok(GetWindowTextLengthW(ml) == 38, "multiline edit length %d, expected 38\n", GetWindowTextLengthW(ml));

        g_OldEditProc = (WNDPROC)SetWindowLongPtrW(ml, GWLP_WNDPROC, (LONG_PTR)EditSubclass);
        ok(g_OldEditProc != NULL, "edit not subclassed\n");
        ok((WNDPROC)GetWindowLongPtrW(ml, GWLP_WNDPROC) == EditSubclass, "subclass proc not set\n");
    }
    ok(HeapOk(), "process heap damaged by the child windows\n");

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    PumpMessages();
    ok(HeapOk(), "process heap damaged by showing and painting\n");

    if (ml)
    {
        /* Changed later, then sized and focused (Notepad sizes it in WM_SIZE) */
        ok(SetWindowTextW(ml, L"Changed later\r\n(painted from the message loop)"),
           "SetWindowTextW failed (%lu)\n", GetLastError());
        ok(g_EditSubclassCalls > 0, "subclass proc not called\n");
        ok(MoveWindow(ml, 20, 175, 420, 70, TRUE), "MoveWindow failed (%lu)\n", GetLastError());
        GetWindowRect(ml, &box);
        ok(box.right - box.left == 420 && box.bottom - box.top == 70,
           "edit size %ldx%ld, expected 420x70\n", box.right - box.left, box.bottom - box.top);
        SetFocus(ml);
        SendMessageW(ml, EM_SETSEL, 1000, 1000);
        GetWindowTextW(ml, text, _countof(text));
        ok(!lstrcmpW(text, L"Changed later\r\n(painted from the message loop)"), "edit text %s\n", wine_dbgstr_w(text));
        PumpMessages();
        ok(HeapOk(), "process heap damaged by the edit box changes\n");
    }

    /* Regions made by gdi32, used by win32k */
    r1 = CreateRectRgn(30, 30, 130, 60);
    r2 = CreateRectRgn(100, 40, 200, 80);
    type = GetRgnBox(r1, &box);
    ok(type == SIMPLEREGION && box.left == 30 && box.top == 30 && box.right == 130 && box.bottom == 60,
       "r1: type %d box %ld,%ld-%ld,%ld\n", type, box.left, box.top, box.right, box.bottom);
    type = CombineRgn(r1, r1, r2, RGN_OR);
    GetRgnBox(r1, &box);
    ok(type == COMPLEXREGION && box.left == 30 && box.top == 30 && box.right == 200 && box.bottom == 80,
       "r1 | r2: type %d box %ld,%ld-%ld,%ld\n", type, box.left, box.top, box.right, box.bottom);
    SetRectRgn(r2, 5, 6, 50, 60);
    OffsetRgn(r2, 10, 10);
    GetRgnBox(r2, &box);
    ok(box.left == 15 && box.top == 16 && box.right == 60 && box.bottom == 70,
       "SetRectRgn + OffsetRgn: box %ld,%ld-%ld,%ld, expected 15,16-60,70\n",
       box.left, box.top, box.right, box.bottom);

    r3 = CreateRectRgnIndirect(&box);
    r4 = CreateRectRgn(0, 0, 0, 0);
    CheckUpdateRect(hwnd, r2, 15, 16, 60, 70, "SetRectRgn region");
    CheckUpdateRect(hwnd, r3, 15, 16, 60, 70, "CreateRectRgnIndirect region");
    CombineRgn(r4, r3, r1, RGN_AND);
    GetRgnBox(r4, &box);
    ok(box.left == 30 && box.top == 30 && box.right == 60 && box.bottom == 60,
       "r3 & r1: box %ld,%ld-%ld,%ld, expected 30,30-60,60\n", box.left, box.top, box.right, box.bottom);
    CheckUpdateRect(hwnd, r4, 30, 30, 60, 60, "CombineRgn result");
    DeleteObject(r1);
    DeleteObject(r2);
    DeleteObject(r3);
    DeleteObject(r4);

    DestroyWindow(hwnd);
    PumpMessages();
    ok(HeapOk(), "process heap damaged at the end\n");

cleanup:
    if (font)
        DeleteObject(font);
    UnregisterClassW(L"NotepadWindowTest", GetModuleHandleW(NULL));
}
