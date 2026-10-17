/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for reading back and moving what is drawn on the screen
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Display drivers may let GDI draw into a copy of the screen in system memory
 * and show the parts that changed later (GCAPS2_SYNCFLUSH): reading the screen
 * (screen captures, moving windows) must still see everything drawn.
 */

#include "precomp.h"

#define CX 160
#define CY 120

static DWORD
Pattern(int x, int y)
{
    return RGB(x * 3 % 256, y * 5 % 256, (x ^ y) % 256);
}

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, NULL);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void
Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

/* Read CX x CY screen pixels at (x, y) */
static void
ReadScreen(int x, int y, DWORD *pdwOut)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), CX, -CY, 1, 32, BI_RGB } };
    HDC hdcScreen = GetDC(NULL), hdcMem = CreateCompatibleDC(hdcScreen);
    void *pvBits;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    int i;

    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, CX, CY, hdcScreen, x, y, SRCCOPY);
    GdiFlush();
    for (i = 0; i < CX * CY; i++)
    {
        DWORD c = ((DWORD *)pvBits)[i];
        pdwOut[i] = RGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
    }
    DeleteDC(hdcMem);
    DeleteObject(hbm);
    ReleaseDC(NULL, hdcScreen);
}

/* Compare with the pattern moved by (dx, dy) */
static void
CheckPattern(const char *pszWhat, const DWORD *pdw, int dx, int dy, int cxCheck, int cyCheck)
{
    int x, y, cWrong = 0;

    for (y = 0; y < cyCheck; y++)
    {
        for (x = 0; x < cxCheck; x++)
        {
            DWORD expected = Pattern(x - dx, y - dy);
            if (x < dx || y < dy)
                continue;
            if (pdw[y * CX + x] != expected && cWrong++ < 3)
                ok(0, "%s: (%d,%d) is 0x%06lx, expected 0x%06lx\n", pszWhat, x, y, pdw[y * CX + x], expected);
        }
    }
    ok(cWrong == 0, "%s: %d pixels differ\n", pszWhat, cWrong);
}

START_TEST(ScreenBlt)
{
    static DWORD adwPattern[CX * CY], adwRead[CX * CY];
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), CX, -CY, 1, 32, BI_RGB } };
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    HDC hdc, hdcScreen;
    POINT pt = { 0, 0 };
    int x, y, bpp;

    hdcScreen = GetDC(NULL);
    bpp = GetDeviceCaps(hdcScreen, BITSPIXEL);
    ReleaseDC(NULL, hdcScreen);
    if (bpp < 24)
    {
        skip("The screen has %d bpp: colors don't come back exactly\n", bpp);
        return;
    }

    for (y = 0; y < CY; y++)
    {
        for (x = 0; x < CX; x++)
        {
            DWORD c = Pattern(x, y);
            adwPattern[y * CX + x] = (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
        }
    }

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ScreenBltTest";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"ScreenBltTest", L"", WS_POPUP | WS_VISIBLE,
                           40, 40, CX, CY, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");
    if (!hwnd)
        return;
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    Pump();
    Sleep(200);
    Pump();

    /* Draw, read back through the screen DC */
    hdc = GetDC(hwnd);
    ClientToScreen(hwnd, &pt);
    SetDIBitsToDevice(hdc, 0, 0, CX, CY, 0, 0, 0, CY, adwPattern, &bmi, DIB_RGB_COLORS);
    ReadScreen(pt.x, pt.y, adwRead);
    CheckPattern("SetDIBitsToDevice", adwRead, 0, 0, CX, CY);

    /* Screen to screen inside the window */
    BitBlt(hdc, 20, 10, CX - 20, CY - 10, hdc, 0, 0, SRCCOPY);
    ReadScreen(pt.x, pt.y, adwRead);
    CheckPattern("BitBlt screen to screen", adwRead, 20, 10, CX, CY);
    ReleaseDC(hwnd, hdc);

    /* Moving the window copies its bits (the window doesn't repaint) */
    hdc = GetDC(hwnd);
    SetDIBitsToDevice(hdc, 0, 0, CX, CY, 0, 0, 0, CY, adwPattern, &bmi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
    for (x = 1; x <= 5; x++)
    {
        SetWindowPos(hwnd, NULL, 40 + x * 13, 40 + x * 7, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump();
    }
    pt.x = pt.y = 0;
    ClientToScreen(hwnd, &pt);
    ok(pt.x == 40 + 5 * 13 && pt.y == 40 + 5 * 7, "Window at %ld,%ld\n", pt.x, pt.y);
    ReadScreen(pt.x, pt.y, adwRead);
    CheckPattern("SetWindowPos", adwRead, 0, 0, CX, CY);

    DestroyWindow(hwnd);
    UnregisterClassW(L"ScreenBltTest", wc.hInstance);
}
