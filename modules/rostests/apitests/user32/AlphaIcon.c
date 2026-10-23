/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Icons with an alpha channel keep it at any screen depth
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"
#include <commctrl.h>

/* A 16x16 icon: transparent (alpha 0) border of 4 pixels, opaque red center.
 * Its AND mask is all zeros ("opaque" everywhere), like many PNG-style icons,
 * so only the alpha channel makes the border transparent. */
#define SZ 16
#define BG_COLOR 0x00FF00 /* green, 0xRRGGBB */
#define ICON_COLOR 0xFF0000 /* red */

static BOOL IsBorder(int x, int y)
{
    return x < 4 || y < 4 || x >= SZ - 4 || y >= SZ - 4;
}

static void FillArgb(DWORD *bits)
{
    int x, y;
    for (y = 0; y < SZ; y++)
        for (x = 0; x < SZ; x++)
            bits[y * SZ + x] = IsBorder(x, y) ? 0 : (0xFF000000 | ICON_COLOR);
}

static HICON CreateAlphaIconIndirect(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), SZ, -SZ, 1, 32, BI_RGB } };
    ICONINFO ii = { TRUE };
    DWORD *bits;
    BYTE mask[SZ * 2] = { 0 };
    HICON hIcon;

    ii.hbmColor = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    ii.hbmMask = CreateBitmap(SZ, SZ, 1, 1, mask);
    FillArgb(bits);
    hIcon = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return hIcon;
}

static HICON CreateAlphaIconFromResource(void)
{
    struct
    {
        BITMAPINFOHEADER bih;
        DWORD color[SZ * SZ];
        DWORD mask[SZ]; /* 1 bpp rows padded to 32 bits, all zero */
    } res;
    int y;
    DWORD tmp[SZ * SZ];

    ZeroMemory(&res, sizeof(res));
    res.bih.biSize = sizeof(res.bih);
    res.bih.biWidth = SZ;
    res.bih.biHeight = SZ * 2;
    res.bih.biPlanes = 1;
    res.bih.biBitCount = 32;
    res.bih.biCompression = BI_RGB;
    FillArgb(tmp);
    for (y = 0; y < SZ; y++) /* bottom-up */
        memcpy(&res.color[y * SZ], &tmp[(SZ - 1 - y) * SZ], SZ * sizeof(DWORD));
    return CreateIconFromResourceEx((PBYTE)&res, sizeof(res), TRUE, 0x00030000, SZ, SZ, 0);
}

static void CheckIconInfoAlpha(HICON hIcon, const char *what)
{
    ICONINFO ii;
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), SZ, -SZ, 1, 32, BI_RGB } };
    DWORD bits[SZ * SZ];
    HDC hdc = GetDC(NULL);
    int lines;

    ok(GetIconInfo(hIcon, &ii), "%s: GetIconInfo failed\n", what);
    ok(ii.hbmColor != NULL, "%s: no color bitmap\n", what);
    ZeroMemory(bits, sizeof(bits));
    lines = GetDIBits(hdc, ii.hbmColor, 0, SZ, bits, &bmi, DIB_RGB_COLORS);
    ok(lines == SZ, "%s: GetDIBits returned %d\n", what, lines);
    /* The alpha channel survives whatever the screen's depth */
    ok((bits[0] >> 24) == 0, "%s: border alpha 0x%lx\n", what, bits[0] >> 24);
    ok(bits[8 * SZ + 8] == (0xFF000000 | ICON_COLOR), "%s: center 0x%08lx\n", what, bits[8 * SZ + 8]);
    ReleaseDC(NULL, hdc);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
}

/* Draws into a DIB section of the given depth and returns the pixel at (x, y) */
static COLORREF DrawAndRead(HICON hIcon, HIMAGELIST himl, WORD bpp, int x, int y)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), SZ, -SZ, 1, bpp, BI_RGB } };
    HDC hdc = CreateCompatibleDC(NULL);
    void *bits;
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(hdc, hbm);
    HBRUSH hbr = CreateSolidBrush(RGB(0, 255, 0));
    RECT rc = { 0, 0, SZ, SZ };
    COLORREF c;

    FillRect(hdc, &rc, hbr);
    if (himl)
        ok(ImageList_Draw(himl, 0, hdc, 0, 0, ILD_TRANSPARENT), "ImageList_Draw failed\n");
    else
        ok(DrawIconEx(hdc, 0, 0, hIcon, SZ, SZ, 0, NULL, DI_NORMAL), "DrawIconEx failed\n");
    c = GetPixel(hdc, x, y);
    SelectObject(hdc, old);
    DeleteObject(hbm);
    DeleteObject(hbr);
    DeleteDC(hdc);
    return c;
}

static void TestIcon(HICON hIcon, const char *what)
{
    static const WORD depths[] = { 16, 24, 32 };
    HIMAGELIST himl;
    COLORREF c;
    int i;

    ok(hIcon != NULL, "%s: no icon\n", what);
    if (!hIcon)
        return;
    CheckIconInfoAlpha(hIcon, what);

    himl = ImageList_Create(SZ, SZ, ILC_COLOR32 | ILC_MASK, 0, 1);
    ok(ImageList_ReplaceIcon(himl, -1, hIcon) == 0, "%s: ImageList_ReplaceIcon failed\n", what);

    for (i = 0; i < ARRAYSIZE(depths); i++)
    {
        c = DrawAndRead(hIcon, NULL, depths[i], 1, 1);
        ok(c == RGB(0, 255, 0), "%s: DrawIconEx %u bpp: border 0x%06lx\n", what, depths[i], c);
        c = DrawAndRead(hIcon, NULL, depths[i], 8, 8);
        ok(c == RGB(255, 0, 0), "%s: DrawIconEx %u bpp: center 0x%06lx\n", what, depths[i], c);
        c = DrawAndRead(NULL, himl, depths[i], 1, 1);
        ok(c == RGB(0, 255, 0), "%s: ImageList_Draw %u bpp: border 0x%06lx\n", what, depths[i], c);
        c = DrawAndRead(NULL, himl, depths[i], 8, 8);
        ok(c == RGB(255, 0, 0), "%s: ImageList_Draw %u bpp: center 0x%06lx\n", what, depths[i], c);
    }

    ImageList_Destroy(himl);
    DestroyIcon(hIcon);
}

START_TEST(AlphaIcon)
{
    HDC hdc = GetDC(NULL);
    trace("Screen depth: %d bpp\n", GetDeviceCaps(hdc, BITSPIXEL));
    ReleaseDC(NULL, hdc);

    TestIcon(CreateAlphaIconIndirect(), "CreateIconIndirect");
    TestIcon(CreateAlphaIconFromResource(), "CreateIconFromResourceEx");
}
