/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for IDXGIFactory4::EnumWarpAdapter
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Paint.NET 5 asks for the WARP adapter for its default GPU setting; with
 * E_NOTIMPL its crash reports (and the setting) failed.
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dxgi1_6.h>

START_TEST(EnumWarpAdapter)
{
    IDXGIFactory4 *Factory = NULL;
    IDXGIAdapter1 *Adapter = NULL;
    DXGI_ADAPTER_DESC1 Desc;
    HRESULT hr;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&Factory);
    if (FAILED(hr))
    {
        skip("No IDXGIFactory4, hr %#lx\n", hr);
        return;
    }

    hr = IDXGIFactory4_EnumWarpAdapter(Factory, &IID_IDXGIAdapter1, (void **)&Adapter);
    ok(hr == S_OK, "EnumWarpAdapter returned %#lx\n", hr);
    if (Adapter)
    {
        hr = IDXGIAdapter1_GetDesc1(Adapter, &Desc);
        ok(hr == S_OK, "GetDesc1 returned %#lx\n", hr);
        IDXGIAdapter1_Release(Adapter);
    }

    hr = IDXGIFactory4_EnumWarpAdapter(Factory, &IID_IDXGIAdapter1, NULL);
    ok(hr == DXGI_ERROR_INVALID_CALL, "NULL output: %#lx\n", hr);

    IDXGIFactory4_Release(Factory);
}
