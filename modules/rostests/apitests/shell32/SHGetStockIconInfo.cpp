/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for SHGetStockIconInfo
 *
 * The icon location (SHGSI_ICONLOCATION) must name an icon that can be
 * extracted from the file: .NET's SystemIcons.GetStockIcon passes it to
 * SHDefExtractIcon (Windows Forms' error dialog, Paint.NET's setup).
 */

#include "shelltest.h"

typedef struct
{
    DWORD cbSize;
    HICON hIcon;
    int iSysImageIndex;
    int iIcon;
    WCHAR szPath[MAX_PATH];
} TEST_SHSTOCKICONINFO;

typedef HRESULT (WINAPI *PFN_SHGETSTOCKICONINFO)(UINT, UINT, TEST_SHSTOCKICONINFO *);

#define TEST_SHGSI_ICONLOCATION 0
#define TEST_SHGSI_ICON 0x100
#define TEST_SHGSI_SMALLICON 0x1

START_TEST(SHGetStockIconInfo)
{
    static const UINT Ids[] = { 0 /* document */, 3 /* folder */, 77 /* shield */, 78 /* warning */,
                                79 /* information */, 80 /* error */ };
    PFN_SHGETSTOCKICONINFO pSHGetStockIconInfo;
    TEST_SHSTOCKICONINFO Info;
    HICON hIcon;
    HRESULT hr;
    UINT i;

    pSHGetStockIconInfo = (PFN_SHGETSTOCKICONINFO)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetStockIconInfo");
    if (!pSHGetStockIconInfo)
    {
        skip("SHGetStockIconInfo is missing\n");
        return;
    }

    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info) - 1;
    ok_hex(pSHGetStockIconInfo(Ids[0], TEST_SHGSI_ICONLOCATION, &Info), E_INVALIDARG);

    for (i = 0; i < _countof(Ids); i++)
    {
        ZeroMemory(&Info, sizeof(Info));
        Info.cbSize = sizeof(Info);
        hr = pSHGetStockIconInfo(Ids[i], TEST_SHGSI_ICONLOCATION, &Info);
        ok(hr == S_OK, "%u: hr 0x%lx\n", Ids[i], hr);
        ok(Info.hIcon == NULL, "%u: an icon without SHGSI_ICON\n", Ids[i]);
        ok(Info.szPath[0] != 0, "%u: no path\n", Ids[i]);

        /* What SystemIcons.GetStockIcon does with the location */
        hIcon = NULL;
        hr = SHDefExtractIconW(Info.szPath, Info.iIcon, 0, &hIcon, NULL, MAKELONG(32, 32));
        ok(hr == S_OK && hIcon != NULL, "%u: SHDefExtractIconW(%S, %d) returned 0x%lx, %p\n",
           Ids[i], Info.szPath, Info.iIcon, hr, hIcon);
        if (hIcon)
            DestroyIcon(hIcon);

        ZeroMemory(&Info, sizeof(Info));
        Info.cbSize = sizeof(Info);
        hr = pSHGetStockIconInfo(Ids[i], TEST_SHGSI_ICON | TEST_SHGSI_SMALLICON, &Info);
        ok(hr == S_OK && Info.hIcon != NULL, "%u: SHGSI_ICON gave 0x%lx, %p\n", Ids[i], hr, Info.hIcon);
        if (Info.hIcon)
            DestroyIcon(Info.hIcon);
    }
}
