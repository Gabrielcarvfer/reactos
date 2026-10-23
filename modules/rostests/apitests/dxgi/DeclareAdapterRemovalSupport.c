/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for DXGIDeclareAdapterRemovalSupport
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Paint.NET 5 calls DXGIDeclareAdapterRemovalSupport at startup on Windows 10
 * 21H2 and later: it failed with an EntryPointNotFoundException.
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>

typedef HRESULT (WINAPI *PFN_DECLARE)(void);

START_TEST(DeclareAdapterRemovalSupport)
{
    HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");
    PFN_DECLARE pDXGIDeclareAdapterRemovalSupport;
    HRESULT hr;

    ok(Dxgi != NULL, "LoadLibrary(dxgi) failed with %lu\n", GetLastError());
    if (!Dxgi)
        return;
    pDXGIDeclareAdapterRemovalSupport = (PFN_DECLARE)GetProcAddress(Dxgi, "DXGIDeclareAdapterRemovalSupport");
    if (!pDXGIDeclareAdapterRemovalSupport)
    {
        skip("DXGIDeclareAdapterRemovalSupport is missing (before Windows 10 1903)\n");
        FreeLibrary(Dxgi);
        return;
    }

    /* Before any DXGI factory exists in the process */
    hr = pDXGIDeclareAdapterRemovalSupport();
    ok(hr == S_OK, "DXGIDeclareAdapterRemovalSupport returned 0x%lx\n", hr);
    FreeLibrary(Dxgi);
}
