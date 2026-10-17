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

static HDC hdcBig;
static HBITMAP hbmBig, hbmBigOld;
static PULONG pulBig;
#define DST_CX 1024
#define DST_CY 768

static void
FillPattern(PULONG pulBits, INT cx, INT cy, ULONG ulSeed)
{
    INT x, y;
    for (y = 0; y < cy; y++)
        for (x = 0; x < cx; x++)
            pulBits[y * cx + x] = ((x * 7 + y * 13 + ulSeed) * 2654435761u) & 0xFFFFFF;
}

/* A top-down 32 bpp picture of cx x cy, drawn 1:1 at (0,0): every pixel must
 * arrive (repeated calls of the same and of other sizes reuse buffers) */
static void
Test_LargePicture(INT cx, INT cy, ULONG ulSeed)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB } };
    PULONG pulSrc;
    INT ret, x, y, cBad = 0;

    pulSrc = HeapAlloc(GetProcessHeap(), 0, cx * cy * 4);
    ok(pulSrc != NULL, "out of memory\n");
    if (!pulSrc) return;
    FillPattern(pulSrc, cx, cy, ulSeed);
    memset(pulBig, 0xAA, DST_CX * DST_CY * 4);

    ret = StretchDIBits(hdcBig, 0, 0, cx, cy, 0, 0, cx, cy, pulSrc, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ok(ret == cy, "%dx%d: StretchDIBits returned %d\n", cx, cy, ret);
    GdiFlush();

    for (y = 0; y < cy; y++)
        for (x = 0; x < cx; x++)
            if ((pulBig[y * DST_CX + x] & 0xFFFFFF) != pulSrc[y * cx + x]) cBad++;
    ok(cBad == 0, "%dx%d seed %lu: %d wrong pixels\n", cx, cy, ulSeed, cBad);
    if (cx < DST_CX)
        ok((pulBig[DST_CX - 1] & 0xFFFFFF) == 0xAAAAAA, "%dx%d: pixel outside changed: 0x%lx\n",
           cx, cy, pulBig[DST_CX - 1]);
    HeapFree(GetProcessHeap(), 0, pulSrc);
}

/* Bits that can't be read: nothing is drawn and nothing crashes (win32k
 * reads the caller's bits itself) */
static void
Test_UnreadableBits(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), 640, -480, 1, 32, BI_RGB } };
    SIZE_T cjPicture = 640 * 480 * 4;
    PUCHAR pjMem;
    DWORD dwOld;
    INT ret;

    /* The whole picture is inaccessible */
    pjMem = VirtualAlloc(NULL, cjPicture, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    ok(pjMem != NULL, "VirtualAlloc failed\n");
    if (!pjMem) return;
    memset(pulBig, 0xAA, DST_CX * DST_CY * 4);
    ret = StretchDIBits(hdcBig, 0, 0, 640, 480, 0, 0, 640, 480, pjMem, &bmi, DIB_RGB_COLORS, SRCCOPY);
    /* (Windows returns 0 or the line count, depending on the target) */
    ok(ret == 0 || ret == 480, "no-access bits: StretchDIBits returned %d\n", ret);
    GdiFlush();
    ok(pulBig[0] == 0xAAAAAAAA && pulBig[640 * 480 / 2] == 0xAAAAAAAA,
       "no-access bits: pixels changed to 0x%lx, 0x%lx\n", pulBig[0], pulBig[640 * 480 / 2]);

    /* Only the last page is inaccessible */
    ok(VirtualProtect(pjMem, cjPicture - 4096, PAGE_READWRITE, &dwOld), "VirtualProtect failed\n");
    memset(pjMem, 0x11, cjPicture - 4096);
    ret = StretchDIBits(hdcBig, 0, 0, 640, 480, 0, 0, 640, 480, pjMem, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ok(ret == 0 || ret == 480, "partly readable bits: StretchDIBits returned %d\n", ret);

    /* All readable again: works */
    ok(VirtualProtect(pjMem, cjPicture, PAGE_READONLY, &dwOld), "VirtualProtect failed\n");
    ret = StretchDIBits(hdcBig, 0, 0, 640, 480, 0, 0, 640, 480, pjMem, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ok(ret == 480, "read-only bits: StretchDIBits returned %d\n", ret);
    GdiFlush();
    ok((pulBig[0] & 0xFFFFFF) == 0x111111, "read-only bits: pixel is 0x%lx\n", pulBig[0]);

    VirtualFree(pjMem, 0, MEM_RELEASE);
}

static void
Test_LargeAndUnreadable(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), DST_CX, -DST_CY, 1, 32, BI_RGB } };

    hdcBig = CreateCompatibleDC(NULL);
    hbmBig = CreateDIBSection(hdcBig, &bmi, DIB_RGB_COLORS, (PVOID*)&pulBig, NULL, 0);
    ok(hdcBig && hbmBig, "could not create the target\n");
    if (!hdcBig || !hbmBig) return;
    hbmBigOld = SelectObject(hdcBig, hbmBig);

    Test_LargePicture(640, 480, 1);
    Test_LargePicture(640, 480, 2);
    Test_LargePicture(1024, 768, 3);
    Test_LargePicture(800, 600, 4);
    Test_LargePicture(320, 240, 5);
    Test_LargePicture(1024, 768, 6);
    Test_UnreadableBits();
    Test_LargePicture(640, 480, 7);

    SelectObject(hdcBig, hbmBigOld);
    DeleteObject(hbmBig);
    DeleteDC(hdcBig);
}

/* Bits in read-only private memory and in the executable's image: win32k
 * reads them in place or copies them, the result is the same */
static const DWORD g_ImageBits[4 * 4] =
{
    0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
    0x01020304, 0x05060708, 0x090a0b0c, 0x0d0e0f10,
    0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f000,
    0xffffffff, 0x00000000, 0x80808080, 0x7f7f7f7f,
};

static void
Test_BitsSource(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), 4, -4, 1, 32, BI_RGB } };
    BITMAPINFO bmiDst = { { sizeof(BITMAPINFOHEADER), 4, -4, 1, 32, BI_RGB } };
    HDC hdc = CreateCompatibleDC(NULL);
    void *pvDst;
    HBITMAP hbm = CreateDIBSection(hdc, &bmiDst, DIB_RGB_COLORS, &pvDst, NULL, 0);
    DWORD *pdwRO, dwOld;
    int ret;

    SelectObject(hdc, hbm);

    /* In the image */
    memset(pvDst, 0, sizeof(g_ImageBits));
    ret = StretchDIBits(hdc, 0, 0, 4, 4, 0, 0, 4, 4, g_ImageBits, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ok(ret == 4, "StretchDIBits returned %d\n", ret);
    GdiFlush();
    ok(memcmp(pvDst, g_ImageBits, sizeof(g_ImageBits)) == 0, "StretchDIBits from the image: wrong bits\n");

    memset(pvDst, 0, sizeof(g_ImageBits));
    ret = SetDIBitsToDevice(hdc, 0, 0, 4, 4, 0, 0, 0, 4, g_ImageBits, &bmi, DIB_RGB_COLORS);
    ok(ret == 4, "SetDIBitsToDevice returned %d\n", ret);
    GdiFlush();
    ok(memcmp(pvDst, g_ImageBits, sizeof(g_ImageBits)) == 0, "SetDIBitsToDevice from the image: wrong bits\n");

    /* Read-only private memory, followed by an inaccessible page */
    pdwRO = VirtualAlloc(NULL, 2 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(pdwRO != NULL, "VirtualAlloc failed\n");
    if (pdwRO)
    {
        DWORD *pdwEnd = (DWORD *)((BYTE *)pdwRO + 4096) - 16;
        memcpy(pdwEnd, g_ImageBits, sizeof(g_ImageBits));
        ok(VirtualProtect(pdwRO, 4096, PAGE_READONLY, &dwOld), "VirtualProtect failed\n");
        ok(VirtualProtect((BYTE *)pdwRO + 4096, 4096, PAGE_NOACCESS, &dwOld), "VirtualProtect failed\n");

        memset(pvDst, 0, sizeof(g_ImageBits));
        ret = StretchDIBits(hdc, 0, 0, 4, 4, 0, 0, 4, 4, pdwEnd, &bmi, DIB_RGB_COLORS, SRCCOPY);
        ok(ret == 4, "StretchDIBits returned %d\n", ret);
        GdiFlush();
        ok(memcmp(pvDst, g_ImageBits, sizeof(g_ImageBits)) == 0, "StretchDIBits from read-only memory: wrong bits\n");

        memset(pvDst, 0, sizeof(g_ImageBits));
        ret = SetDIBitsToDevice(hdc, 0, 0, 4, 4, 0, 0, 0, 4, pdwEnd, &bmi, DIB_RGB_COLORS);
        ok(ret == 4, "SetDIBitsToDevice returned %d\n", ret);
        GdiFlush();
        ok(memcmp(pvDst, g_ImageBits, sizeof(g_ImageBits)) == 0, "SetDIBitsToDevice from read-only memory: wrong bits\n");

        /* The memory is still the caller's to change and free */
        ok(VirtualProtect(pdwRO, 4096, PAGE_READWRITE, &dwOld), "VirtualProtect failed after the blits\n");
        ok(VirtualFree(pdwRO, 0, MEM_RELEASE), "VirtualFree failed after the blits\n");
    }

    DeleteDC(hdc);
    DeleteObject(hbm);
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
    Test_BitsSource();
    Test_LargeAndUnreadable();
}
