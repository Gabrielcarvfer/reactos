/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for StretchDIBits and SetDIBitsToDevice into memory DCs
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

#define W 24
#define H 16

static DWORD
SrcPixel(int x, int y)
{
    /* Alpha in the high byte, as in image lists and icons with alpha */
    return ((DWORD)(x * 10 + 5) << 24) | ((x * 9) << 16) | ((y * 13) << 8) | ((x + y) * 5);
}

static HDC
CreateDibDC(int bpp, BOOL bTopDown, void **ppvBits, HBITMAP *phbm)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), W, bTopDown ? -H : H, 1, (WORD)bpp, BI_RGB } };
    HDC hdc = CreateCompatibleDC(NULL);

    *phbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, ppvBits, NULL, 0);
    ok(*phbm != NULL, "CreateDIBSection failed\n");
    SelectObject(hdc, *phbm);
    return hdc;
}

/* Pixel (x, y) (y from the top) of a W x H 32 bpp DIB section */
static DWORD
DibPixel32(void *pvBits, BOOL bTopDown, int x, int y)
{
    return ((DWORD *)pvBits)[(bTopDown ? y : H - 1 - y) * W + x];
}

/* A 32 bpp ARGB DIB copied 1:1 into a 32 bpp DIB section keeps every byte,
 * alpha included (image lists rely on it: icons with alpha) */
static void
Test_32bppKeepsAlpha(BOOL bSrcTopDown, BOOL bDstTopDown, BOOL bStretch)
{
    DWORD src[W * H];
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), W, bSrcTopDown ? -H : H, 1, 32, BI_RGB } };
    void *pvBits;
    HBITMAP hbm;
    HDC hdc;
    int x, y, ret, cWrong = 0;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            src[(bSrcTopDown ? y : H - 1 - y) * W + x] = SrcPixel(x, y);

    hdc = CreateDibDC(32, bDstTopDown, &pvBits, &hbm);
    memset(pvBits, 0x11, W * H * 4);

    if (bStretch)
        ret = StretchDIBits(hdc, 0, 0, W, H, 0, 0, W, H, src, &bmi, DIB_RGB_COLORS, SRCCOPY);
    else
        ret = SetDIBitsToDevice(hdc, 0, 0, W, H, 0, 0, 0, H, src, &bmi, DIB_RGB_COLORS);
    ok(ret == H, "%s returned %d\n", bStretch ? "StretchDIBits" : "SetDIBitsToDevice", ret);
    GdiFlush();

    for (y = 0; y < H; y++)
    {
        for (x = 0; x < W; x++)
        {
            DWORD got = DibPixel32(pvBits, bDstTopDown, x, y);
            if (got != SrcPixel(x, y) && cWrong++ < 3)
            {
                ok(0, "%s src %s dst %s: (%d,%d) is 0x%08lx, expected 0x%08lx\n",
                   bStretch ? "StretchDIBits" : "SetDIBitsToDevice",
                   bSrcTopDown ? "top-down" : "bottom-up", bDstTopDown ? "top-down" : "bottom-up",
                   x, y, got, SrcPixel(x, y));
            }
        }
    }
    ok(cWrong == 0, "%d pixels differ\n", cWrong);

    DeleteDC(hdc);
    DeleteObject(hbm);
}

/* One image out of a strip, like ImageList_Add does: source x/y offsets of a
 * bottom-up DIB. The result must match the same copy done with SetDIBits into a
 * DIB section and BitBlt (the reference path). */
static void
Test_SubRect(int bpp)
{
    enum { SW = W * 3, SH = H + 6 };
    BYTE src[SW * SH * 4];
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } bmi = { { sizeof(BITMAPINFOHEADER), SW, SH, 1, (WORD)bpp, BI_RGB } };
    void *pvBits, *pvRef, *pvSrcRef;
    HBITMAP hbm, hbmRef, hbmSrcRef;
    HDC hdc, hdcRef, hdcSrcRef;
    int i, ret, xSrc = W, ySrc = 2, cx = W - 4, cy = H - 5;

    for (i = 0; i < (int)sizeof(src); i++)
        src[i] = (BYTE)(i * 7 + i / 5);

    hdc = CreateDibDC(32, FALSE, &pvBits, &hbm);
    hdcRef = CreateDibDC(32, FALSE, &pvRef, &hbmRef);
    memset(pvBits, 0x22, W * H * 4);
    memset(pvRef, 0x22, W * H * 4);

    ret = StretchDIBits(hdc, 1, 2, cx, cy, xSrc, ySrc, cx, cy, src, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
    ok(ret == SH, "StretchDIBits returned %d\n", ret);

    /* Reference: the whole DIB into a DIB section of its format, then BitBlt
     * (StretchDIBits' source y counts from the bottom of the DIB) */
    hdcSrcRef = CreateCompatibleDC(NULL);
    hbmSrcRef = CreateDIBSection(hdcSrcRef, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, &pvSrcRef, NULL, 0);
    memcpy(pvSrcRef, src, ((SW * bpp + 31) / 32) * 4 * SH);
    SelectObject(hdcSrcRef, hbmSrcRef);
    BitBlt(hdcRef, 1, 2, cx, cy, hdcSrcRef, xSrc, SH - ySrc - cy, SRCCOPY);
    GdiFlush();

    if (bpp == 32)
    {
        ok(memcmp(pvBits, pvRef, W * H * 4) == 0, "%d bpp: sub-rectangle differs from the reference\n", bpp);
    }
    else
    {
        /* Compare the colors (the high byte isn't defined for these) */
        int cWrong = 0;
        for (i = 0; i < W * H; i++)
        {
            if ((((DWORD *)pvBits)[i] & 0xffffff) != (((DWORD *)pvRef)[i] & 0xffffff) && cWrong++ < 3)
                ok(0, "%d bpp: pixel %d is 0x%08lx, reference 0x%08lx\n", bpp, i, ((DWORD *)pvBits)[i], ((DWORD *)pvRef)[i]);
        }
        ok(cWrong == 0, "%d bpp: %d pixels differ from the reference\n", bpp, cWrong);
    }

    DeleteDC(hdcSrcRef);
    DeleteObject(hbmSrcRef);
    DeleteDC(hdc);
    DeleteObject(hbm);
    DeleteDC(hdcRef);
    DeleteObject(hbmRef);
}

/* Into a 24 bpp DIB section and into a bitmap compatible with the screen:
 * the colors must come through */
static void
Test_OtherTargets(void)
{
    DWORD src[W * H];
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), W, H, 1, 32, BI_RGB } };
    HDC hdcScreen = GetDC(NULL), hdc = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, W, H);
    COLORREF c;
    int x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            src[(H - 1 - y) * W + x] = 0xff000000 | RGB(0x30, 0x60 + y, 0x90 + x); /* BGRX: 0x9x is red */

    SelectObject(hdc, hbm);
    StretchDIBits(hdc, 0, 0, W, H, 0, 0, W, H, src, &bmi, DIB_RGB_COLORS, SRCCOPY);
    c = GetPixel(hdc, 3, 5);
    if (GetDeviceCaps(hdcScreen, BITSPIXEL) >= 24)
        ok(c == RGB(0x90 + 3, 0x60 + 5, 0x30), "compatible bitmap: got 0x%06lx\n", c);
    DeleteDC(hdc);
    DeleteObject(hbm);
    ReleaseDC(NULL, hdcScreen);
}

START_TEST(StretchDIBits)
{
    Test_32bppKeepsAlpha(FALSE, FALSE, TRUE);
    Test_32bppKeepsAlpha(TRUE, FALSE, TRUE);
    Test_32bppKeepsAlpha(FALSE, TRUE, TRUE);
    Test_32bppKeepsAlpha(TRUE, TRUE, TRUE);
    Test_32bppKeepsAlpha(FALSE, FALSE, FALSE);
    Test_32bppKeepsAlpha(TRUE, TRUE, FALSE);
    Test_SubRect(32);
    Test_SubRect(24);
    Test_SubRect(16);
    Test_OtherTargets();
}
