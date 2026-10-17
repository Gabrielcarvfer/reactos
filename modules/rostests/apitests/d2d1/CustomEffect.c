/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test custom Direct2D effects whose factory returns a plain IUnknown
 *
 * PD2D1_EFFECT_FACTORY returns an IUnknown. Direct2D must ask it for
 * ID2D1EffectImpl: effects written in .NET (Paint.NET 5) return the
 * object's IUnknown identity, a different interface pointer. d2d1 called
 * "Initialize" through the IUnknown vtable, which ran another method and
 * overwrote the effect context: Paint.NET's setup crashed.
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <d2d1effectauthor.h>
#include <wincodec.h>

DEFINE_GUID(CLSID_TestUnknownEffect, 0x5b2f4d1e, 0x0b6a, 0x4a52, 0x9e, 0x47, 0x1d, 0x3c, 0x2b, 0x77, 0x6a, 0x10);

static const WCHAR EffectXml[] =
    L"<?xml version='1.0'?>"
    L"<Effect>"
    L"<Property name='DisplayName' type='string' value='TestUnknownEffect'/>"
    L"<Property name='Author' type='string' value='ReactOS'/>"
    L"<Property name='Category' type='string' value='Test'/>"
    L"<Property name='Description' type='string' value='Test effect.'/>"
    L"<Inputs><Input name='Source'/></Inputs>"
    L"</Effect>";

typedef struct _TEST_EFFECT
{
    IUnknown IUnknown_iface;            /* the identity, a separate vtable */
    ID2D1EffectImpl ID2D1EffectImpl_iface;
    LONG Refcount;
} TEST_EFFECT;

static LONG Objects;
static LONG WrongSlotCalls;
static LONG InitializeCalls;
static ID2D1EffectContext *InitContext;
static PVOID ContextVtbl;

static
HRESULT
STDMETHODCALLTYPE
Unknown_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    TEST_EFFECT *This = CONTAINING_RECORD(iface, TEST_EFFECT, IUnknown_iface);

    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &This->IUnknown_iface;
    else if (IsEqualGUID(riid, &IID_ID2D1EffectImpl))
        *out = &This->ID2D1EffectImpl_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&This->Refcount);
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
Unknown_AddRef(IUnknown *iface)
{
    TEST_EFFECT *This = CONTAINING_RECORD(iface, TEST_EFFECT, IUnknown_iface);
    return InterlockedIncrement(&This->Refcount);
}

static
ULONG
STDMETHODCALLTYPE
Unknown_Release(IUnknown *iface)
{
    TEST_EFFECT *This = CONTAINING_RECORD(iface, TEST_EFFECT, IUnknown_iface);
    LONG Refcount = InterlockedDecrement(&This->Refcount);

    if (!Refcount)
    {
        HeapFree(GetProcessHeap(), 0, This);
        InterlockedDecrement(&Objects);
    }
    return Refcount;
}

/* Reached only when the IUnknown is used as an ID2D1EffectImpl */
static
HRESULT
STDMETHODCALLTYPE
Unknown_WrongSlot(IUnknown *iface, void *Arg1, void *Arg2)
{
    InterlockedIncrement(&WrongSlotCalls);
    return E_FAIL;
}

static const struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG (STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT (STDMETHODCALLTYPE *Slots[3])(IUnknown *, void *, void *);
} UnknownVtbl =
{
    Unknown_QueryInterface, Unknown_AddRef, Unknown_Release,
    { Unknown_WrongSlot, Unknown_WrongSlot, Unknown_WrongSlot }
};

static inline TEST_EFFECT *impl_from_ID2D1EffectImpl(ID2D1EffectImpl *iface)
{
    return CONTAINING_RECORD(iface, TEST_EFFECT, ID2D1EffectImpl_iface);
}

static
HRESULT
STDMETHODCALLTYPE
EffectImpl_QueryInterface(ID2D1EffectImpl *iface, REFIID riid, void **out)
{
    return Unknown_QueryInterface(&impl_from_ID2D1EffectImpl(iface)->IUnknown_iface, riid, out);
}

static
ULONG
STDMETHODCALLTYPE
EffectImpl_AddRef(ID2D1EffectImpl *iface)
{
    return Unknown_AddRef(&impl_from_ID2D1EffectImpl(iface)->IUnknown_iface);
}

static
ULONG
STDMETHODCALLTYPE
EffectImpl_Release(ID2D1EffectImpl *iface)
{
    return Unknown_Release(&impl_from_ID2D1EffectImpl(iface)->IUnknown_iface);
}

static
HRESULT
STDMETHODCALLTYPE
EffectImpl_Initialize(ID2D1EffectImpl *iface, ID2D1EffectContext *Context, ID2D1TransformGraph *Graph)
{
    InterlockedIncrement(&InitializeCalls);
    InitContext = Context;
    ContextVtbl = *(PVOID *)Context;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
EffectImpl_PrepareForRender(ID2D1EffectImpl *iface, D2D1_CHANGE_TYPE Type)
{
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
EffectImpl_SetGraph(ID2D1EffectImpl *iface, ID2D1TransformGraph *Graph)
{
    return E_NOTIMPL;
}

/* Spelled out: MinGW's headers nest the IUnknown part of the vtable */
static const struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID2D1EffectImpl *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID2D1EffectImpl *);
    ULONG (STDMETHODCALLTYPE *Release)(ID2D1EffectImpl *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(ID2D1EffectImpl *, ID2D1EffectContext *, ID2D1TransformGraph *);
    HRESULT (STDMETHODCALLTYPE *PrepareForRender)(ID2D1EffectImpl *, D2D1_CHANGE_TYPE);
    HRESULT (STDMETHODCALLTYPE *SetGraph)(ID2D1EffectImpl *, ID2D1TransformGraph *);
} EffectImplVtbl =
{
    EffectImpl_QueryInterface,
    EffectImpl_AddRef,
    EffectImpl_Release,
    EffectImpl_Initialize,
    EffectImpl_PrepareForRender,
    EffectImpl_SetGraph,
};

/* Returns the IUnknown identity, not the ID2D1EffectImpl interface */
static
HRESULT
CALLBACK
TestEffectFactory(IUnknown **Effect)
{
    TEST_EFFECT *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));

    if (!This)
        return E_OUTOFMEMORY;
    This->IUnknown_iface.lpVtbl = (IUnknownVtbl *)&UnknownVtbl;
    This->ID2D1EffectImpl_iface.lpVtbl = (void *)&EffectImplVtbl;
    This->Refcount = 1;
    InterlockedIncrement(&Objects);
    *Effect = &This->IUnknown_iface;
    return S_OK;
}

START_TEST(CustomEffect)
{
    D2D1_FACTORY_OPTIONS Options = { D2D1_DEBUG_LEVEL_NONE };
    D2D1_RENDER_TARGET_PROPERTIES RtProps = { 0 };
    ID2D1DeviceContext *DeviceContext = NULL;
    IWICImagingFactory *WicFactory = NULL;
    ID2D1RenderTarget *RenderTarget = NULL;
    ID2D1Factory1 *Factory = NULL;
    IWICBitmap *Bitmap = NULL;
    ID2D1Effect *Effect = NULL;
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

    hr = ID2D1Factory1_RegisterEffectFromString(Factory, &CLSID_TestUnknownEffect, EffectXml,
                                                NULL, 0, TestEffectFactory);
    ok(hr == S_OK, "RegisterEffectFromString failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;

    hr = ID2D1DeviceContext_CreateEffect(DeviceContext, &CLSID_TestUnknownEffect, &Effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx\n", hr);
    ok(WrongSlotCalls == 0, "a method was called through the IUnknown vtable %ld times\n", WrongSlotCalls);
    ok(InitializeCalls == 1, "Initialize called %ld times\n", InitializeCalls);
    ok(Objects == 1, "%ld implementation objects\n", Objects);
    if (InitContext)
    {
        ok(*(PVOID *)InitContext == ContextVtbl, "the effect context was overwritten\n");
    }

    if (Effect)
    {
        ID2D1Effect_Release(Effect);
        ok(Objects == 0, "%ld implementation objects left after Release\n", Objects);
    }

    hr = ID2D1Factory1_UnregisterEffect(Factory, &CLSID_TestUnknownEffect);
    ok(hr == S_OK, "UnregisterEffect failed, hr %#lx\n", hr);

Cleanup:
    if (DeviceContext) ID2D1DeviceContext_Release(DeviceContext);
    if (RenderTarget) ID2D1RenderTarget_Release(RenderTarget);
    if (Bitmap) IWICBitmap_Release(Bitmap);
    if (WicFactory) IWICImagingFactory_Release(WicFactory);
    if (Factory) ID2D1Factory1_Release(Factory);
    CoUninitialize();
}
