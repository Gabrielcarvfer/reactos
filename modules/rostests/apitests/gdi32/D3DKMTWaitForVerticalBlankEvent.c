/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for D3DKMTWaitForVerticalBlankEvent and IDXGIOutput::WaitForVBlank
 *
 * Chromium (its VSyncThreadWin), ANGLE and DXGI programs wait for the vertical
 * blank to pace their frames. Both calls failed at once on ReactOS, and newer
 * Chromium browsers and WebView2 never drew a frame.
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#define COBJMACROS
#include <objbase.h>
#include <initguid.h>
#include <dxgi.h>

/* (ddk/d3dkmthk.h) */
typedef struct { HDC hDc; UINT hAdapter; LUID AdapterLuid; UINT VidPnSourceId; } D3DKMT_OPENADAPTERFROMHDC;
typedef struct { UINT hAdapter; UINT hDevice; UINT VidPnSourceId; } D3DKMT_WAITFORVERTICALBLANKEVENT;

typedef NTSTATUS (APIENTRY *POPENFROMHDC)(D3DKMT_OPENADAPTERFROMHDC *);
typedef NTSTATUS (APIENTRY *PWAITVBLANK)(const D3DKMT_WAITFORVERTICALBLANKEVENT *);
typedef HRESULT (WINAPI *PCREATEFACTORY)(REFIID, void **);

DEFINE_GUID(IID_IDXGIFactory_test, 0x7b7166ec, 0x21c7, 0x44ae, 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69);

static double NowMs(void)
{
    LARGE_INTEGER Freq, Count;
    QueryPerformanceFrequency(&Freq);
    QueryPerformanceCounter(&Count);
    return Count.QuadPart * 1000.0 / Freq.QuadPart;
}

static void TestDxgi(void)
{
    PCREATEFACTORY pCreateDXGIFactory;
    IDXGIFactory *Factory;
    IDXGIAdapter *Adapter;
    IDXGIOutput *Output;
    HMODULE Dxgi;
    HRESULT hr;
    double Start, Each;
    int i;

    Dxgi = LoadLibraryW(L"dxgi.dll");
    pCreateDXGIFactory = Dxgi ? (PCREATEFACTORY)GetProcAddress(Dxgi, "CreateDXGIFactory") : NULL;
    if (!pCreateDXGIFactory)
    {
        skip("no dxgi.dll\n");
        return;
    }
    hr = pCreateDXGIFactory(&IID_IDXGIFactory_test, (void **)&Factory);
    ok(hr == S_OK, "CreateDXGIFactory: 0x%lx\n", hr);
    if (FAILED(hr))
        return;
    if (IDXGIFactory_EnumAdapters(Factory, 0, &Adapter) != S_OK)
    {
        skip("no adapter\n");
        IDXGIFactory_Release(Factory);
        return;
    }
    if (IDXGIAdapter_EnumOutputs(Adapter, 0, &Output) == S_OK)
    {
        Start = NowMs();
        for (i = 0; i < 10; i++)
        {
            hr = IDXGIOutput_WaitForVBlank(Output);
            ok(hr == S_OK, "WaitForVBlank: 0x%lx\n", hr);
        }
        Each = (NowMs() - Start) / 10;
        ok(Each > 4 && Each < 50, "WaitForVBlank took %.1f ms each\n", Each);
        IDXGIOutput_Release(Output);
    }
    else
    {
        skip("no output\n");
    }
    IDXGIAdapter_Release(Adapter);
    IDXGIFactory_Release(Factory);
}

START_TEST(D3DKMTWaitForVerticalBlankEvent)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");
    POPENFROMHDC pOpen = (POPENFROMHDC)GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromHdc");
    PWAITVBLANK pWait = (PWAITVBLANK)GetProcAddress(Gdi32, "D3DKMTWaitForVerticalBlankEvent");
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter = { 0 };
    D3DKMT_WAITFORVERTICALBLANKEVENT Wait = { 0 };
    NTSTATUS Status;
    double Start, Each;
    int i;

    if (!pOpen || !pWait)
    {
        skip("no D3DKMT functions\n");
        return;
    }
    OpenAdapter.hDc = GetDC(NULL);
    Status = pOpen(&OpenAdapter);
    ok(Status == 0 /* STATUS_SUCCESS */, "D3DKMTOpenAdapterFromHdc: 0x%lx\n", Status);
    ReleaseDC(NULL, OpenAdapter.hDc);

    Wait.hAdapter = OpenAdapter.hAdapter;
    Wait.VidPnSourceId = OpenAdapter.VidPnSourceId;
    Start = NowMs();
    for (i = 0; i < 10; i++)
    {
        Status = pWait(&Wait);
        ok(Status == 0 /* STATUS_SUCCESS */, "D3DKMTWaitForVerticalBlankEvent: 0x%lx\n", Status);
    }
    Each = (NowMs() - Start) / 10;
    ok(Each > 4 && Each < 50, "D3DKMTWaitForVerticalBlankEvent took %.1f ms each\n", Each);

    TestDxgi();
}
