/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for ANSI windows of the system controls (MDI client, button, edit...)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * XnView (a 32-bit ANSI program) failed to create its MDI children: the MDI
 * client had been subclassed with SetWindowLongPtrA and MDIClientWndProcW
 * then got an ANSI MDICREATESTRUCTA. Everything here is ANSI.
 */

#include "precomp.h"
#include <winnls.h>

static WNDPROC g_OldClientProc, g_OldEditProc;
static int g_ClientSubSeen, g_EditSubSeen;

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK ChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefMDIChildProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK ClientSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MDICREATE)
        g_ClientSubSeen++;
    return CallWindowProcA(g_OldClientProc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK EditSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETTEXT || msg == WM_GETTEXT)
        g_EditSubSeen++;
    return CallWindowProcA(g_OldEditProc, hwnd, msg, wp, lp);
}

/* SetWindowTextA / GetWindowTextA / WM_GETTEXT (A and W) / WM_GETTEXTLENGTH */
static void TextRoundTrip(const char *name, HWND hwnd, const char *text)
{
    char buf[256];
    WCHAR wbuf[256], wexp[256];
    LRESULT len;

    ok(SetWindowTextA(hwnd, text), "%s: SetWindowTextA failed (%lu)\n", name, GetLastError());

    memset(buf, 0, sizeof(buf));
    GetWindowTextA(hwnd, buf, sizeof(buf));
    ok(!strcmp(buf, text), "%s: GetWindowTextA gave \"%s\", expected \"%s\"\n", name, buf, text);

    memset(buf, 0, sizeof(buf));
    len = SendMessageA(hwnd, WM_GETTEXT, sizeof(buf), (LPARAM)buf);
    ok(!strcmp(buf, text) && len == (LRESULT)strlen(text),
       "%s: SendMessageA(WM_GETTEXT) gave \"%s\", length %ld\n", name, buf, (long)len);

    len = SendMessageA(hwnd, WM_GETTEXTLENGTH, 0, 0);
    ok(len == (LRESULT)strlen(text), "%s: WM_GETTEXTLENGTH %ld, expected %u\n",
       name, (long)len, (unsigned)strlen(text));

    memset(wbuf, 0, sizeof(wbuf));
    MultiByteToWideChar(CP_ACP, 0, text, -1, wexp, _countof(wexp));
    SendMessageW(hwnd, WM_GETTEXT, _countof(wbuf), (LPARAM)wbuf);
    ok(!lstrcmpW(wbuf, wexp), "%s: SendMessageW(WM_GETTEXT) gave %s\n", name, wine_dbgstr_w(wbuf));
}

START_TEST(MdiAnsi)
{
    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    CLIENTCREATESTRUCT ccs;
    MDICREATESTRUCTA mcs;
    HWND hFrame, hClient, hClient2, hChild, hButton, hEdit, hList, hCombo, hStatic;
    WNDPROC proc;
    char buf[256];
    LRESULT r;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = "MdiAnsiFrameClass";
    ok(RegisterClassA(&wc) != 0, "RegisterClassA(frame) failed (%lu)\n", GetLastError());

    wc.lpfnWndProc = ChildProc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "MdiAnsiChildClass";
    ok(RegisterClassA(&wc) != 0, "RegisterClassA(child) failed (%lu)\n", GetLastError());

    hFrame = CreateWindowExA(0, "MdiAnsiFrameClass", "mdiansi frame", WS_OVERLAPPEDWINDOW,
                             50, 50, 640, 480, NULL, NULL, hInst, NULL);
    ok(hFrame != NULL, "frame not created (%lu)\n", GetLastError());
    if (!hFrame)
    {
        skip("No frame window\n");
        goto cleanup;
    }

    /* MDI client, subclassed with SetWindowLongPtrA, WM_MDICREATE through SendMessageA */
    ccs.hWindowMenu = NULL;
    ccs.idFirstChild = 1000;
    hClient = CreateWindowExA(WS_EX_CLIENTEDGE, "MDICLIENT", NULL,
                              WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                              0, 0, 600, 400, hFrame, (HMENU)1, hInst, &ccs);
    ok(hClient != NULL, "MDICLIENT not created (%lu)\n", GetLastError());
    if (hClient)
    {
        trace("MDICLIENT IsWindowUnicode = %d\n", IsWindowUnicode(hClient));
        g_OldClientProc = (WNDPROC)SetWindowLongPtrA(hClient, GWLP_WNDPROC, (LONG_PTR)ClientSubProc);
        ok(g_OldClientProc != NULL, "MDICLIENT not subclassed\n");

        memset(&mcs, 0, sizeof(mcs));
        mcs.szClass = "MdiAnsiChildClass";
        mcs.szTitle = "Ansi MDI child";
        mcs.hOwner = hInst;
        mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
        hChild = (HWND)SendMessageA(hClient, WM_MDICREATE, 0, (LPARAM)&mcs);
        ok(hChild != NULL, "SendMessageA(WM_MDICREATE) on the subclassed client gave no child\n");
        ok(g_ClientSubSeen == 1, "subclass proc saw WM_MDICREATE %d times\n", g_ClientSubSeen);
        if (hChild)
        {
            memset(buf, 0, sizeof(buf));
            GetWindowTextA(hChild, buf, sizeof(buf));
            ok(!strcmp(buf, "Ansi MDI child"), "MDI child title \"%s\"\n", buf);
            memset(buf, 0, sizeof(buf));
            GetClassNameA(hChild, buf, sizeof(buf));
            ok(!strcmp(buf, "MdiAnsiChildClass"), "MDI child class \"%s\"\n", buf);
        }

        /* Not subclassed: SendMessageA directly */
        hClient2 = CreateWindowExA(0, "MDICLIENT", NULL, WS_CHILD, 0, 0, 100, 100,
                                   hFrame, (HMENU)2, hInst, &ccs);
        ok(hClient2 != NULL, "second MDICLIENT not created (%lu)\n", GetLastError());
        if (hClient2)
        {
            mcs.szTitle = "Second child";
            hChild = (HWND)SendMessageA(hClient2, WM_MDICREATE, 0, (LPARAM)&mcs);
            ok(hChild != NULL, "SendMessageA(WM_MDICREATE) on a plain client gave no child\n");
            DestroyWindow(hClient2);
        }
    }

    /* BUTTON */
    hButton = CreateWindowExA(0, "BUTTON", "Hello button", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              10, 10, 120, 30, hFrame, (HMENU)10, hInst, NULL);
    ok(hButton != NULL, "BUTTON not created (%lu)\n", GetLastError());
    if (hButton)
    {
        memset(buf, 0, sizeof(buf));
        GetWindowTextA(hButton, buf, sizeof(buf));
        ok(!strcmp(buf, "Hello button"), "BUTTON: creation text \"%s\"\n", buf);
        TextRoundTrip("BUTTON", hButton, "Changed button text");

        /* Its proc as GetWindowLongPtrA gives it, called directly with an ANSI buffer */
        proc = (WNDPROC)GetWindowLongPtrA(hButton, GWLP_WNDPROC);
        ok(proc != NULL, "BUTTON: no window procedure\n");
        if (proc)
        {
            memset(buf, 0, sizeof(buf));
            r = proc(hButton, WM_GETTEXT, sizeof(buf), (LPARAM)buf);
            ok(!strcmp(buf, "Changed button text"),
               "BUTTON: GetWindowLongPtrA(GWLP_WNDPROC) called directly gave \"%s\" (%ld)\n", buf, (long)r);
            memset(buf, 0, sizeof(buf));
            CallWindowProcA(proc, hButton, WM_GETTEXT, sizeof(buf), (LPARAM)buf);
            ok(!strcmp(buf, "Changed button text"), "BUTTON: CallWindowProcA gave \"%s\"\n", buf);
        }
    }

    /* EDIT, then subclassed */
    hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "Hello edit", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            10, 50, 200, 24, hFrame, (HMENU)11, hInst, NULL);
    ok(hEdit != NULL, "EDIT not created (%lu)\n", GetLastError());
    if (hEdit)
    {
        memset(buf, 0, sizeof(buf));
        GetWindowTextA(hEdit, buf, sizeof(buf));
        ok(!strcmp(buf, "Hello edit"), "EDIT: creation text \"%s\"\n", buf);
        TextRoundTrip("EDIT", hEdit, "Changed edit text");

        g_OldEditProc = (WNDPROC)SetWindowLongPtrA(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
        ok(g_OldEditProc != NULL, "EDIT not subclassed\n");
        TextRoundTrip("EDIT(subclassed)", hEdit, "Subclassed edit text");
        ok(g_EditSubSeen > 0, "EDIT subclass proc saw no text messages\n");
        SetWindowLongPtrA(hEdit, GWLP_WNDPROC, (LONG_PTR)g_OldEditProc);
        TextRoundTrip("EDIT(restored)", hEdit, "Restored edit text");
    }

    /* STATIC */
    hStatic = CreateWindowExA(0, "STATIC", "Hello static", WS_CHILD | WS_VISIBLE,
                              10, 80, 200, 20, hFrame, (HMENU)12, hInst, NULL);
    ok(hStatic != NULL, "STATIC not created (%lu)\n", GetLastError());
    if (hStatic)
        TextRoundTrip("STATIC", hStatic, "Changed static text");

    /* LISTBOX */
    hList = CreateWindowExA(0, "LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER,
                            220, 10, 150, 100, hFrame, (HMENU)13, hInst, NULL);
    ok(hList != NULL, "LISTBOX not created (%lu)\n", GetLastError());
    if (hList)
    {
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"First list item");
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Second list item");
        memset(buf, 0, sizeof(buf));
        r = SendMessageA(hList, LB_GETTEXT, 1, (LPARAM)buf);
        ok(!strcmp(buf, "Second list item") && r == 16, "LISTBOX: LB_GETTEXT gave \"%s\" (%ld)\n", buf, (long)r);
    }

    /* COMBOBOX */
    hCombo = CreateWindowExA(0, "COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                             220, 120, 150, 200, hFrame, (HMENU)14, hInst, NULL);
    ok(hCombo != NULL, "COMBOBOX not created (%lu)\n", GetLastError());
    if (hCombo)
    {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Combo item one");
        memset(buf, 0, sizeof(buf));
        r = SendMessageA(hCombo, CB_GETLBTEXT, 0, (LPARAM)buf);
        ok(!strcmp(buf, "Combo item one") && r == 14, "COMBOBOX: CB_GETLBTEXT gave \"%s\" (%ld)\n", buf, (long)r);
    }

    DestroyWindow(hFrame);

cleanup:
    UnregisterClassA("MdiAnsiChildClass", hInst);
    UnregisterClassA("MdiAnsiFrameClass", hInst);
}
