/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test custom Direct2D effects that build transform graphs
 *
 * Paint.NET 5's effects ask the effect context for ID2D1EffectContext2,
 * nest other effects with CreateTransformNodeFromEffect and put offset or
 * blend transforms into their graphs. d2d1 failed all three, and
 * CreateEffect failed for any graph with a node that is not a draw
 * transform.
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <d2d1effectauthor.h>
#include <d2d1effects.h>
#include <wincodec.h>

DEFINE_GUID(CLSID_TestGraphEffect, 0x5b2f4d1e, 0x0b6a, 0x4a52, 0x9e, 0x47, 0x1d, 0x3c, 0x2b, 0x77, 0x6a, 0x11);

/* (not in our headers) */
static const GUID TestIID_ID2D1EffectContext1 = { 0x84ab595a, 0xfc81, 0x4546, { 0xba, 0xcd, 0xe8, 0xef, 0x4d, 0x8a, 0xbe, 0x7a } };
static const GUID TestIID_ID2D1EffectContext2 = { 0x577ad2a0, 0x9fc7, 0x4dda, { 0x8b, 0x18, 0xda, 0xb8, 0x10, 0x14, 0x00, 0x52 } };

static const WCHAR EffectXml[] =
    L"<?xml version='1.0'?>"
    L"<Effect>"
    L"<Property name='DisplayName' type='string' value='TestGraphEffect'/>"
    L"<Property name='Author' type='string' value='ReactOS'/>"
    L"<Property name='Category' type='string' value='Test'/>"
    L"<Property name='Description' type='string' value='Test effect.'/>"
    L"<Inputs><Input name='Source'/></Inputs>"
    L"</Effect>";

typedef struct _TEST_EFFECT
{
    ID2D1EffectImpl ID2D1EffectImpl_iface;
    LONG Refcount;
} TEST_EFFECT;

static HRESULT Context1Hr = E_FAIL, Context2Hr = E_FAIL, OffsetHr = E_FAIL, NestedHr = E_FAIL;
static HRESULT NodeHr = E_FAIL, AddHr = E_FAIL, AddNestedHr = E_FAIL, OutputHr = E_FAIL, ConnectHr = E_FAIL;
static UINT32 NestedInputs = 99;

static HRESULT STDMETHODCALLTYPE EffectImpl_QueryInterface(ID2D1EffectImpl *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID2D1EffectImpl))
    {
        *out = iface;
        ID2D1EffectImpl_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE EffectImpl_AddRef(ID2D1EffectImpl *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, TEST_EFFECT, ID2D1EffectImpl_iface)->Refcount);
}

static ULONG STDMETHODCALLTYPE EffectImpl_Release(ID2D1EffectImpl *iface)
{
    TEST_EFFECT *This = CONTAINING_RECORD(iface, TEST_EFFECT, ID2D1EffectImpl_iface);
    LONG Refcount = InterlockedDecrement(&This->Refcount);
    if (!Refcount)
        HeapFree(GetProcessHeap(), 0, This);
    return Refcount;
}

static HRESULT STDMETHODCALLTYPE EffectImpl_Initialize(ID2D1EffectImpl *iface, ID2D1EffectContext *Context,
                                                       ID2D1TransformGraph *Graph)
{
    D2D1_POINT_2L Offset = { 1, 2 };
    ID2D1OffsetTransform *OffsetTransform = NULL;
    ID2D1TransformNode *NestedNode = NULL;
    ID2D1Effect *Nested = NULL;
    IUnknown *Unknown;

    Context1Hr = ID2D1EffectContext_QueryInterface(Context, &TestIID_ID2D1EffectContext1, (void **)&Unknown);
    if (SUCCEEDED(Context1Hr)) IUnknown_Release(Unknown);
    Context2Hr = ID2D1EffectContext_QueryInterface(Context, &TestIID_ID2D1EffectContext2, (void **)&Unknown);
    if (SUCCEEDED(Context2Hr)) IUnknown_Release(Unknown);

    /* A nested built-in effect as a node */
    NestedHr = ID2D1EffectContext_CreateEffect(Context, &CLSID_D2D1Crop, &Nested);
    if (SUCCEEDED(NestedHr))
    {
        NodeHr = ID2D1EffectContext_CreateTransformNodeFromEffect(Context, Nested, &NestedNode);
        if (SUCCEEDED(NodeHr))
        {
            NestedInputs = ID2D1TransformNode_GetInputCount(NestedNode);
            AddNestedHr = ID2D1TransformGraph_AddNode(Graph, NestedNode);
        }
        ID2D1Effect_Release(Nested);
    }

    /* An offset transform (not a draw transform) as the output */
    OffsetHr = ID2D1EffectContext_CreateOffsetTransform(Context, Offset, &OffsetTransform);
    if (SUCCEEDED(OffsetHr))
    {
        AddHr = ID2D1TransformGraph_AddNode(Graph, (ID2D1TransformNode *)OffsetTransform);
        if (NestedNode)
            ConnectHr = ID2D1TransformGraph_ConnectNode(Graph, NestedNode, (ID2D1TransformNode *)OffsetTransform, 0);
        OutputHr = ID2D1TransformGraph_SetOutputNode(Graph, (ID2D1TransformNode *)OffsetTransform);
        ID2D1OffsetTransform_Release(OffsetTransform);
    }
    if (NestedNode)
        ID2D1TransformNode_Release(NestedNode);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE EffectImpl_PrepareForRender(ID2D1EffectImpl *iface, D2D1_CHANGE_TYPE Type)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE EffectImpl_SetGraph(ID2D1EffectImpl *iface, ID2D1TransformGraph *Graph)
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
    EffectImpl_QueryInterface, EffectImpl_AddRef, EffectImpl_Release,
    EffectImpl_Initialize, EffectImpl_PrepareForRender, EffectImpl_SetGraph,
};

static HRESULT CALLBACK TestEffectFactory(IUnknown **Effect)
{
    TEST_EFFECT *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;
    This->ID2D1EffectImpl_iface.lpVtbl = (void *)&EffectImplVtbl;
    This->Refcount = 1;
    *Effect = (IUnknown *)&This->ID2D1EffectImpl_iface;
    return S_OK;
}

START_TEST(EffectGraph)
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

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, &Options, (void **)&Factory);
    if (FAILED(hr))
    {
        skip("No ID2D1Factory1, hr %#lx\n", hr);
        goto Cleanup;
    }
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&WicFactory);
    ok(SUCCEEDED(hr), "CoCreateInstance(WICImagingFactory) failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;
    hr = IWICImagingFactory_CreateBitmap(WicFactory, 16, 16, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &Bitmap);
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

    hr = ID2D1Factory1_RegisterEffectFromString(Factory, &CLSID_TestGraphEffect, EffectXml, NULL, 0, TestEffectFactory);
    ok(hr == S_OK, "RegisterEffectFromString failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto Cleanup;

    hr = ID2D1DeviceContext_CreateEffect(DeviceContext, &CLSID_TestGraphEffect, &Effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx\n", hr);
    ok(Context1Hr == S_OK, "QueryInterface(ID2D1EffectContext1): %#lx\n", Context1Hr);
    ok(Context2Hr == S_OK, "QueryInterface(ID2D1EffectContext2): %#lx\n", Context2Hr);
    ok(NestedHr == S_OK, "CreateEffect(Crop): %#lx\n", NestedHr);
    ok(NodeHr == S_OK, "CreateTransformNodeFromEffect: %#lx\n", NodeHr);
    ok(NestedInputs == 1, "the nested effect node has %u inputs\n", NestedInputs);
    ok(AddNestedHr == S_OK, "AddNode(nested effect): %#lx\n", AddNestedHr);
    ok(OffsetHr == S_OK, "CreateOffsetTransform: %#lx\n", OffsetHr);
    ok(AddHr == S_OK, "AddNode(offset): %#lx\n", AddHr);
    ok(ConnectHr == S_OK, "ConnectNode: %#lx\n", ConnectHr);
    ok(OutputHr == S_OK, "SetOutputNode: %#lx\n", OutputHr);
    if (Effect)
        ID2D1Effect_Release(Effect);
    ID2D1Factory1_UnregisterEffect(Factory, &CLSID_TestGraphEffect);

Cleanup:
    if (DeviceContext) ID2D1DeviceContext_Release(DeviceContext);
    if (RenderTarget) ID2D1RenderTarget_Release(RenderTarget);
    if (Bitmap) IWICBitmap_Release(Bitmap);
    if (WicFactory) IWICImagingFactory_Release(WicFactory);
    if (Factory) ID2D1Factory1_Release(Factory);
    CoUninitialize();
}
