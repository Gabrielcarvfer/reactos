/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for ClearType (subpixel) text rendering
 */

#include "precomp.h"

/* Draw black text on white in a 32bpp DIB and count the pixels whose red, green
 * and blue differ: ClearType renders color fringes, grayscale antialiasing and
 * plain (non-antialiased) text never do. */
static UINT CountColoredPixels(BYTE Quality)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), 200, -40, 1, 32, BI_RGB } };
    DWORD *pBits = NULL;
    HDC hdc = CreateCompatibleDC(NULL);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (PVOID *)&pBits, NULL, 0);
    HFONT hFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, Quality, 0, L"Tahoma");
    HGDIOBJ hbmOld, hFontOld;
    RECT rc = { 0, 0, 200, 40 };
    UINT i, Count = 0;

    ok(hbm != NULL && pBits != NULL, "CreateDIBSection failed\n");
    if (!hbm || !pBits)
    {
        DeleteDC(hdc);
        return 0;
    }
    hbmOld = SelectObject(hdc, hbm);
    hFontOld = SelectObject(hdc, hFont);
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
    SetTextColor(hdc, RGB(0, 0, 0));
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, 4, 4, L"ClearType wmw", 13);
    GdiFlush();

    for (i = 0; i < 200 * 40; i++)
    {
        BYTE b = (BYTE)pBits[i], g = (BYTE)(pBits[i] >> 8), r = (BYTE)(pBits[i] >> 16);
        if (r != g || g != b)
            Count++;
    }

    SelectObject(hdc, hFontOld);
    SelectObject(hdc, hbmOld);
    DeleteObject(hFont);
    DeleteObject(hbm);
    DeleteDC(hdc);
    return Count;
}

START_TEST(ClearTypeText)
{
    UINT Colored;

    Colored = CountColoredPixels(NONANTIALIASED_QUALITY);
    ok(Colored == 0, "Non-antialiased text has %u colored pixels\n", Colored);

    Colored = CountColoredPixels(ANTIALIASED_QUALITY);
    ok(Colored == 0, "Grayscale antialiased text has %u colored pixels\n", Colored);

    Colored = CountColoredPixels(CLEARTYPE_QUALITY);
    ok(Colored > 50, "ClearType text has only %u colored pixels (no subpixel rendering?)\n", Colored);
}
