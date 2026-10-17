/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test Flush and EndDraw of a Direct2D device context
 *
 * Flush outside BeginDraw/EndDraw fails with D2DERR_WRONG_STATE. Paint.NET 5
 * uses that to find out whether a device context is drawing; d2d1 returned
 * E_NOTIMPL and Paint.NET's setup stopped with a NotImplementedException.
 */

#define COBJMACROS
#include <apitest.h>
#include <d2d1_1.h>
#include <wincodec.h>

#ifndef D2DERR_WRONG_STATE
#define D2DERR_WRONG_STATE ((HRESULT)0x88990001)
#endif

START_TEST(DrawState)
{
    D2D1_FACTORY_OPTIONS Options = { D2D1_DEBUG_LEVEL_NONE };
    D2D1_RENDER_TARGET_PROPERTIES RtProps = { 0 };
    ID2D1DeviceContext *DeviceContext = NULL;
    IWICImagingFactory *WicFactory = NULL;
    ID2D1RenderTarget *RenderTarget = NULL;
    ID2D1Factory1 *Factory = NULL;
    IWICBitmap *Bitmap = NULL;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx failed, hr %#lx\n", hr);

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, &Options, (void **)&Factory);
    if (FAILED(hr))
    {
        skip("No ID2D1Factory1, hr %#lx\n", hr);
        goto Cleanup;
    }
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&WicFactory);
    ok(SUCCEEDED(hr), "CoCreateInstance(WICImagingFactory) failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;
    hr = IWICImagingFactory_CreateBitmap(WicFactory, 16, 16, &GUID_WICPixelFormat32bppPBGRA,
                                         WICBitmapCacheOnDemand, &Bitmap);
    ok(SUCCEEDED(hr), "CreateBitmap failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;
    RtProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    RtProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    hr = ID2D1Factory1_CreateWicBitmapRenderTarget(Factory, Bitmap, &RtProps, &RenderTarget);
    if (FAILED(hr))
    {
        skip("No WIC render target, hr %#lx\n", hr);
        goto Cleanup;
    }
    hr = ID2D1RenderTarget_QueryInterface(RenderTarget, &IID_ID2D1DeviceContext, (void **)&DeviceContext);
    if (FAILED(hr))
    {
        skip("No ID2D1DeviceContext, hr %#lx\n", hr);
        goto Cleanup;
    }

    hr = ID2D1DeviceContext_Flush(DeviceContext, NULL, NULL);
    ok(hr == D2DERR_WRONG_STATE, "Flush before BeginDraw returned %#lx\n", hr);

    ID2D1DeviceContext_BeginDraw(DeviceContext);
    hr = ID2D1DeviceContext_Flush(DeviceContext, NULL, NULL);
    ok(hr == S_OK, "Flush while drawing returned %#lx\n", hr);
    hr = ID2D1DeviceContext_EndDraw(DeviceContext, NULL, NULL);
    ok(hr == S_OK, "EndDraw returned %#lx\n", hr);

    hr = ID2D1DeviceContext_Flush(DeviceContext, NULL, NULL);
    ok(hr == D2DERR_WRONG_STATE, "Flush after EndDraw returned %#lx\n", hr);

Cleanup:
    if (DeviceContext) ID2D1DeviceContext_Release(DeviceContext);
    if (RenderTarget) ID2D1RenderTarget_Release(RenderTarget);
    if (Bitmap) IWICBitmap_Release(Bitmap);
    if (WicFactory) IWICImagingFactory_Release(WicFactory);
    if (Factory) ID2D1Factory1_Release(Factory);
    CoUninitialize();
}
