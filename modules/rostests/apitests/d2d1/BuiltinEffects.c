/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that the built-in effects Paint.NET uses are registered
 *
 * Paint.NET 5 reads the category of several built-in effects at startup
 * (ID2D1Factory1::GetEffectProperties) and creates others; with
 * ERROR_NOT_FOUND or D2DERR_EFFECT_IS_NOT_REGISTERED it crashed.
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <d2d1effects.h>

DEFINE_GUID(TestCLSID_AlphaMask, 0xc80ecff0, 0x3fd5, 0x4f05, 0x83, 0x28, 0xc5, 0xd1, 0x72, 0x4b, 0x4f, 0x0a);
DEFINE_GUID(TestCLSID_Contrast, 0xb648a78a, 0x0ed5, 0x4f80, 0xa9, 0x4a, 0x8e, 0x82, 0x5a, 0xca, 0x6b, 0x77);
DEFINE_GUID(TestCLSID_Emboss, 0xb1c5eb2b, 0x0348, 0x43f0, 0x81, 0x07, 0x49, 0x57, 0xca, 0xcb, 0xa2, 0xae);
DEFINE_GUID(TestCLSID_Opacity, 0x811d79a4, 0xde28, 0x4454, 0x80, 0x94, 0xc6, 0x46, 0x85, 0xf8, 0xbd, 0x4c);

static const struct
{
    const GUID *Clsid;
    const WCHAR *Category;
    UINT32 Inputs;
}
Effects[] =
{
    { &CLSID_D2D1Histogram, L"Analysis", 1 },
    { &CLSID_D2D1OpacityMetadata, L"Color", 1 },
    { &TestCLSID_AlphaMask, L"Composite", 2 },
    { &CLSID_D2D1ConvolveMatrix, L"Filter", 1 },
    { &TestCLSID_Contrast, L"Photo", 1 },
    { &CLSID_D2D1BitmapSource, L"Source", 0 },
    { &TestCLSID_Emboss, L"Stylize", 1 },
    { &TestCLSID_Opacity, L"Transparency", 1 },
    { &CLSID_D2D1ColorManagement, NULL, 1 },
    { &CLSID_D2D1UnPremultiply, NULL, 1 },
    { &CLSID_D2D1Premultiply, NULL, 1 },
    { &CLSID_D2D1Border, NULL, 1 },
    { &CLSID_D2D1GaussianBlur, NULL, 1 },
    { &CLSID_D2D1Flood, NULL, 0 },
};

START_TEST(BuiltinEffects)
{
    D2D1_FACTORY_OPTIONS Options = { D2D1_DEBUG_LEVEL_NONE };
    ID2D1Factory1 *Factory = NULL;
    ID2D1Properties *Props;
    WCHAR Value[64];
    UINT32 Inputs;
    HRESULT hr;
    UINT i;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, &Options, (void **)&Factory);
    if (FAILED(hr))
    {
        skip("No ID2D1Factory1, hr %#lx\n", hr);
        return;
    }

    for (i = 0; i < _countof(Effects); i++)
    {
        hr = ID2D1Factory1_GetEffectProperties(Factory, Effects[i].Clsid, &Props);
        ok(hr == S_OK, "%u: GetEffectProperties returned %#lx\n", i, hr);
        if (FAILED(hr))
            continue;

        if (Effects[i].Category)
        {
            hr = ID2D1Properties_GetValue(Props, D2D1_PROPERTY_CATEGORY, D2D1_PROPERTY_TYPE_STRING,
                                          (BYTE *)Value, sizeof(Value));
            ok(hr == S_OK && !wcscmp(Value, Effects[i].Category), "%u: category %#lx %S\n", i, hr, Value);
        }
        Inputs = 99;
        hr = ID2D1Properties_GetValue(Props, D2D1_PROPERTY_MIN_INPUTS, D2D1_PROPERTY_TYPE_UINT32,
                                      (BYTE *)&Inputs, sizeof(Inputs));
        ok(hr == S_OK && Inputs == Effects[i].Inputs, "%u: %#lx, %u inputs\n", i, hr, Inputs);
        ID2D1Properties_Release(Props);
    }

    ID2D1Factory1_Release(Factory);
}
