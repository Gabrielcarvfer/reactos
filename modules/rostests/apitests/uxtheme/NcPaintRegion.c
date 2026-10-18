/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The WM_NCPAINT update region belongs to the caller
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * DefWindowProc paints the frame for WM_NCPAINT (themed through uxtheme's
 * hooks) but must not delete the region in wParam: the system deletes it
 * after the message. uxtheme used to delete it, so win32k then deleted an
 * invalid handle.
 */

#include <windows.h>
#include <uxtheme.h>
#include <apitest.h>

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

START_TEST(NcPaintRegion)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    HRGN hrgn;
    RECT rc;
    int i;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"NcPaintRegionTest";
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"WM_NCPAINT region", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           50, 50, 300, 200, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");
    if (!hwnd)
        return;
    UpdateWindow(hwnd);

    trace("Themed: %d\n", IsThemeActive() && IsAppThemed());

    for (i = 0; i < 2; i++)
    {
        /* The whole window, in screen coordinates (as the system passes it) */
        GetWindowRect(hwnd, &rc);
        hrgn = CreateRectRgnIndirect(&rc);
        ok(hrgn != NULL, "CreateRectRgnIndirect failed\n");

        if (i == 0)
            SendMessageW(hwnd, WM_NCPAINT, (WPARAM)hrgn, 0);
        else
            DefWindowProcW(hwnd, WM_NCPAINT, (WPARAM)hrgn, 0);

        /* Deleting objects can be batched: flush before checking */
        GdiFlush();
        ok(GetObjectType(hrgn) == OBJ_REGION, "%d: the region was deleted (type %lu)\n", i, GetObjectType(hrgn));
        ok(DeleteObject(hrgn), "%d: DeleteObject failed\n", i);
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
