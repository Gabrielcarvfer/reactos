/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for SHGetKnownFolderItem
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Open-Shell's start menu (loaded into explorer.exe) calls it; the stub
 * returned S_OK without an item and explorer crashed.
 */

#include "shelltest.h"
#include <knownfolders.h>

typedef HRESULT (WINAPI *PFN_GET_ITEM)(REFKNOWNFOLDERID, DWORD, HANDLE, REFIID, void **);

START_TEST(SHGetKnownFolderItem)
{
    PFN_GET_ITEM pSHGetKnownFolderItem =
        (PFN_GET_ITEM)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetKnownFolderItem");
    WCHAR Expected[MAX_PATH];
    IShellItem *Item = NULL;
    LPWSTR Path = NULL;
    HRESULT hr;

    if (!pSHGetKnownFolderItem)
    {
        skip("SHGetKnownFolderItem is missing (before Windows 7)\n");
        return;
    }

    CoInitialize(NULL);

    hr = pSHGetKnownFolderItem(FOLDERID_Windows, 0, NULL, IID_IShellItem, (void **)&Item);
    ok_hex(hr, S_OK);
    ok(Item != NULL, "no item\n");
    if (Item)
    {
        hr = Item->GetDisplayName(SIGDN_FILESYSPATH, &Path);
        ok_hex(hr, S_OK);
        GetWindowsDirectoryW(Expected, _countof(Expected));
        ok(Path && !lstrcmpiW(Path, Expected), "path %S, expected %S\n", Path, Expected);
        CoTaskMemFree(Path);
        Item->Release();
    }

    Item = (IShellItem *)(ULONG_PTR)1;
    hr = pSHGetKnownFolderItem(GUID_NULL, 0, NULL, IID_IShellItem, (void **)&Item);
    ok(FAILED(hr), "an unknown folder gave 0x%lx\n", hr);
    ok(Item == NULL, "the item was not cleared\n");

    hr = pSHGetKnownFolderItem(FOLDERID_Windows, 0, NULL, IID_IShellItem, NULL);
    ok_hex(hr, E_INVALIDARG);

    CoUninitialize();
}
