/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test ID2D1DeviceContext::CreateColorContext
 *
 * Paint.NET 5 creates sRGB and scRGB color contexts for its effects; d2d1
 * returned E_NOTIMPL and Paint.NET's setup showed an error.
 */

#define COBJMACROS
#include <apitest.h>
#include <d2d1_3.h>
#include <wincodec.h>

/* (not in the import libraries) */
static const GUID TestIID_ID2D1ColorContext1 = { 0x1ab42875, 0xc57f, 0x4be9, { 0xbd, 0x85, 0x9c, 0xd7, 0x8d, 0x6f, 0x55, 0xee } };

START_TEST(ColorContext)
{
    D2D1_FACTORY_OPTIONS Options = { D2D1_DEBUG_LEVEL_NONE };
    D2D1_RENDER_TARGET_PROPERTIES RtProps = { 0 };
    ID2D1DeviceContext *DeviceContext = NULL;
    IWICImagingFactory *WicFactory = NULL;
    ID2D1RenderTarget *RenderTarget = NULL;
    ID2D1ColorContext *ColorContext = NULL;
    ID2D1Factory1 *Factory = NULL, *Factory2 = NULL;
    IWICBitmap *Bitmap = NULL;
    BYTE Profile[16] = { 1, 2, 3, 4 }, Out[16];
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

    hr = ID2D1DeviceContext_CreateColorContext(DeviceContext, D2D1_COLOR_SPACE_SRGB, NULL, 0, &ColorContext);
    ok(hr == S_OK, "sRGB: hr %#lx\n", hr);
    if (ColorContext)
    {
        ok(ID2D1ColorContext_GetColorSpace(ColorContext) == D2D1_COLOR_SPACE_SRGB, "wrong color space\n");
        ID2D1ColorContext_GetFactory(ColorContext, (ID2D1Factory **)&Factory2);
        ok(Factory2 == Factory, "factory %p, expected %p\n", Factory2, Factory);
        if (Factory2) ID2D1Factory1_Release(Factory2);

        /* Windows 10: Paint.NET asks for ID2D1ColorContext1 */
        {
            ID2D1ColorContext1 *ColorContext1 = NULL;
            hr = ID2D1ColorContext_QueryInterface(ColorContext, &TestIID_ID2D1ColorContext1, (void **)&ColorContext1);
            ok(hr == S_OK, "QueryInterface(ID2D1ColorContext1): hr %#lx\n", hr);
            if (ColorContext1)
            {
                ok(ID2D1ColorContext1_GetColorContextType(ColorContext1) == D2D1_COLOR_CONTEXT_TYPE_ICC, "type %d\n",
                   ID2D1ColorContext1_GetColorContextType(ColorContext1));
                ok(ID2D1ColorContext1_GetDXGIColorSpace(ColorContext1) == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
                   "DXGI color space %d\n", ID2D1ColorContext1_GetDXGIColorSpace(ColorContext1));
                ID2D1ColorContext1_Release(ColorContext1);
            }
        }
        ID2D1ColorContext_Release(ColorContext);
        ColorContext = NULL;
    }

    hr = ID2D1DeviceContext_CreateColorContext(DeviceContext, D2D1_COLOR_SPACE_SCRGB, NULL, 0, &ColorContext);
    ok(hr == S_OK, "scRGB: hr %#lx\n", hr);
    if (ColorContext)
    {
        ok(ID2D1ColorContext_GetColorSpace(ColorContext) == D2D1_COLOR_SPACE_SCRGB, "wrong color space\n");
        ID2D1ColorContext_Release(ColorContext);
        ColorContext = NULL;
    }

    hr = ID2D1DeviceContext_CreateColorContext(DeviceContext, D2D1_COLOR_SPACE_CUSTOM, NULL, 0, &ColorContext);
    ok(hr == E_INVALIDARG, "custom without a profile: hr %#lx\n", hr);

    hr = ID2D1DeviceContext_CreateColorContext(DeviceContext, D2D1_COLOR_SPACE_CUSTOM, Profile, sizeof(Profile), &ColorContext);
    ok(hr == S_OK, "custom: hr %#lx\n", hr);
    if (ColorContext)
    {
        ok(ID2D1ColorContext_GetProfileSize(ColorContext) == sizeof(Profile), "profile size %u\n",
           ID2D1ColorContext_GetProfileSize(ColorContext));
        ok(ID2D1ColorContext_GetProfile(ColorContext, Out, sizeof(Out)) == S_OK && !memcmp(Out, Profile, sizeof(Profile)),
           "wrong profile\n");
        ID2D1ColorContext_Release(ColorContext);
    }

Cleanup:
    if (DeviceContext) ID2D1DeviceContext_Release(DeviceContext);
    if (RenderTarget) ID2D1RenderTarget_Release(RenderTarget);
    if (Bitmap) IWICBitmap_Release(Bitmap);
    if (WicFactory) IWICImagingFactory_Release(WicFactory);
    if (Factory) ID2D1Factory1_Release(Factory);
    CoUninitialize();
}
