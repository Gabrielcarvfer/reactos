/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for wglShareLists between contexts of different pixel formats
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <windows.h>
#include <wine/test.h>

/* The first window format of the display driver's ICD (not generic) after 'after' */
static int find_icd_format(HDC dc, int after)
{
    PIXELFORMATDESCRIPTOR pfd;
    int i, count = DescribePixelFormat(dc, 1, sizeof(pfd), NULL);

    for (i = after + 1; i <= count; i++)
    {
        if (!DescribePixelFormat(dc, i, sizeof(pfd), &pfd))
            continue;
        if ((pfd.dwFlags & (PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW)) != (PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW))
            continue;
        if (pfd.dwFlags & PFD_GENERIC_FORMAT)
            continue;
        return i;
    }
    return 0;
}

static HGLRC make_context(HWND *wnd, int fmt)
{
    PIXELFORMATDESCRIPTOR pfd;
    HDC dc;

    *wnd = CreateWindowA("STATIC", "wglShareLists", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    dc = GetDC(*wnd);
    DescribePixelFormat(dc, fmt, sizeof(pfd), &pfd);
    ok(SetPixelFormat(dc, fmt, &pfd), "SetPixelFormat(%d) failed, error %lu\n", fmt, GetLastError());
    return wglCreateContext(dc);
}

START_TEST(wglShareLists)
{
    HWND probe, wnd1, wnd2;
    HDC dc;
    HGLRC rc1, rc2;
    int fmt1, fmt2;
    BOOL ret;

    probe = CreateWindowA("STATIC", "probe", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    dc = GetDC(probe);
    fmt1 = find_icd_format(dc, 0);
    fmt2 = fmt1 ? find_icd_format(dc, fmt1) : 0;
    ReleaseDC(probe, dc);
    DestroyWindow(probe);
    if (!fmt2)
    {
        skip("No OpenGL driver with two window pixel formats\n");
        return;
    }

    rc1 = make_context(&wnd1, fmt1);
    rc2 = make_context(&wnd2, fmt2);
    ok(rc1 != NULL && rc2 != NULL, "wglCreateContext failed, error %lu\n", GetLastError());
    if (rc1 && rc2)
    {
        /* Whether the driver can share is up to the driver (Mesa can);
         * opengl32 must not refuse it because the formats differ */
        SetLastError(0xdeadbeef);
        ret = wglShareLists(rc1, rc2);
        ok(ret || GetLastError() != ERROR_INVALID_PIXEL_FORMAT,
           "wglShareLists(format %d, format %d) refused with ERROR_INVALID_PIXEL_FORMAT\n", fmt1, fmt2);
    }
    if (rc2) wglDeleteContext(rc2);
    if (rc1) wglDeleteContext(rc1);
    DestroyWindow(wnd2);
    DestroyWindow(wnd1);
}
