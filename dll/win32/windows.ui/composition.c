/*
 * PROJECT:     ReactOS WinRT UI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.UI.Composition.Core.CompositorController (minimal)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * ReactOS has no compositor yet. Paint.NET creates a CompositorController
 * at startup (on a thread with a DispatcherQueue) and commits it; it asks for
 * the Compositor only to put swap chains into windows, and draws through GDI
 * instead when that fails. So the controller exists, Commit succeeds, and
 * the Compositor property reports "not implemented".
 */

#define COBJMACROS
#include <windows.h>
#include <winstring.h>
#include <inspectable.h>
#include <activation.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(composition);

static const WCHAR ControllerClassName[] = L"Windows.UI.Composition.Core.CompositorController";

static const GUID IID_Inspectable_ = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_ActivationFactory_ = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_AgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
/* Windows.Foundation.IClosable */
static const GUID IID_IClosable_ = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};
/* Windows.UI.Composition.Core.ICompositorController (public WinRT metadata) */
static const GUID IID_ICompositorController = {0x2d75f35a, 0x70a7, 0x4395, {0xba, 0x2d, 0xce, 0xf0, 0xb1, 0x83, 0x99, 0xf9}};

typedef struct { INT64 value; } EventRegistrationToken;

/* ICompositorController: IInspectable, then these */
typedef struct ICompositorControllerVtbl_
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *get_Compositor)(IInspectable *, IInspectable **);
    HRESULT (WINAPI *Commit)(IInspectable *);
    HRESULT (WINAPI *EnsurePreviousCommitCompletedAsync)(IInspectable *, IInspectable **);
    HRESULT (WINAPI *add_CommitNeeded)(IInspectable *, IUnknown *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_CommitNeeded)(IInspectable *, EventRegistrationToken);
} ICompositorControllerVtbl_;

/* IClosable: IInspectable, then Close */
typedef struct IClosableVtbl_
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *Close)(IInspectable *);
} IClosableVtbl_;

#define MAX_HANDLERS 16

typedef struct
{
    IInspectable IController_iface;   /* ICompositorController, the default interface */
    IInspectable IClosable_iface;
    LONG refs;
    CRITICAL_SECTION cs;
    IUnknown *handlers[MAX_HANDLERS];   /* CommitNeeded (never raised: nothing to compose) */
    INT64 tokens[MAX_HANDLERS];
    INT64 next_token;
} CompositorController;

static CompositorController *impl_from(IInspectable *iface, int index)
{
    return CONTAINING_RECORD((iface - index), CompositorController, IController_iface);
}

static HRESULT query_interface(CompositorController *This, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_Inspectable_) ||
        IsEqualGUID(riid, &IID_AgileObject_) || IsEqualGUID(riid, &IID_ICompositorController))
        *out = &This->IController_iface;
    else if (IsEqualGUID(riid, &IID_IClosable_))
        *out = &This->IClosable_iface;
    else
    {
        TRACE("no interface %s\n", debugstr_guid(riid));
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&This->refs);
    return S_OK;
}

static void close_controller(CompositorController *This)
{
    int i;

    EnterCriticalSection(&This->cs);
    for (i = 0; i < MAX_HANDLERS; i++)
    {
        if (This->handlers[i]) IUnknown_Release(This->handlers[i]);
        This->handlers[i] = NULL;
    }
    LeaveCriticalSection(&This->cs);
}

static ULONG release(CompositorController *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        close_controller(This);
        DeleteCriticalSection(&This->cs);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refs;
}

static HRESULT get_iids(ULONG *count, IID **iids)
{
    IID *list = CoTaskMemAlloc(2 * sizeof(IID));
    if (!list) return E_OUTOFMEMORY;
    list[0] = IID_ICompositorController;
    list[1] = IID_IClosable_;
    *count = 2;
    *iids = list;
    return S_OK;
}

#define INSPECTABLE_METHODS(prefix, index) \
static HRESULT WINAPI prefix##_QueryInterface(IInspectable *iface, REFIID riid, void **out) \
{ return query_interface(impl_from(iface, index), riid, out); } \
static ULONG WINAPI prefix##_AddRef(IInspectable *iface) \
{ return InterlockedIncrement(&impl_from(iface, index)->refs); } \
static ULONG WINAPI prefix##_Release(IInspectable *iface) \
{ return release(impl_from(iface, index)); } \
static HRESULT WINAPI prefix##_GetIids(IInspectable *iface, ULONG *count, IID **iids) \
{ return get_iids(count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(IInspectable *iface, HSTRING *name) \
{ return WindowsCreateString(ControllerClassName, ARRAYSIZE(ControllerClassName) - 1, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(IInspectable *iface, TrustLevel *level) \
{ *level = BaseTrust; return S_OK; }

INSPECTABLE_METHODS(controller, 0)
INSPECTABLE_METHODS(closable, 1)

static HRESULT WINAPI controller_get_Compositor(IInspectable *iface, IInspectable **value)
{
    FIXME("iface %p: no compositor\n", iface);
    if (!value) return E_POINTER;
    *value = NULL;
    /* (a failure, not a null object: callers fall back to drawing without it) */
    return E_NOTIMPL;
}

static HRESULT WINAPI controller_Commit(IInspectable *iface)
{
    TRACE("iface %p\n", iface);
    return S_OK;
}

static HRESULT WINAPI controller_EnsurePreviousCommitCompletedAsync(IInspectable *iface, IInspectable **action)
{
    FIXME("iface %p stub\n", iface);
    if (action) *action = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI controller_add_CommitNeeded(IInspectable *iface, IUnknown *handler, EventRegistrationToken *token)
{
    CompositorController *This = impl_from(iface, 0);
    int i;

    TRACE("iface %p, handler %p\n", iface, handler);
    if (!handler || !token) return E_POINTER;

    EnterCriticalSection(&This->cs);
    for (i = 0; i < MAX_HANDLERS; i++)
    {
        if (!This->handlers[i])
        {
            This->handlers[i] = handler;
            IUnknown_AddRef(handler);
            This->tokens[i] = ++This->next_token;
            token->value = This->tokens[i];
            break;
        }
    }
    LeaveCriticalSection(&This->cs);
    return i < MAX_HANDLERS ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI controller_remove_CommitNeeded(IInspectable *iface, EventRegistrationToken token)
{
    CompositorController *This = impl_from(iface, 0);
    IUnknown *handler = NULL;
    int i;

    EnterCriticalSection(&This->cs);
    for (i = 0; i < MAX_HANDLERS; i++)
    {
        if (This->handlers[i] && This->tokens[i] == token.value)
        {
            handler = This->handlers[i];
            This->handlers[i] = NULL;
            break;
        }
    }
    LeaveCriticalSection(&This->cs);
    if (handler) IUnknown_Release(handler);
    return S_OK;
}

static const ICompositorControllerVtbl_ controller_vtbl =
{
    controller_QueryInterface,
    controller_AddRef,
    controller_Release,
    controller_GetIids,
    controller_GetRuntimeClassName,
    controller_GetTrustLevel,
    controller_get_Compositor,
    controller_Commit,
    controller_EnsurePreviousCommitCompletedAsync,
    controller_add_CommitNeeded,
    controller_remove_CommitNeeded,
};

static HRESULT WINAPI closable_Close(IInspectable *iface)
{
    close_controller(impl_from(iface, 1));
    return S_OK;
}

static const IClosableVtbl_ closable_vtbl =
{
    closable_QueryInterface,
    closable_AddRef,
    closable_Release,
    closable_GetIids,
    closable_GetRuntimeClassName,
    closable_GetTrustLevel,
    closable_Close,
};

/* The activation factory */

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_Inspectable_) ||
        IsEqualGUID(riid, &IID_AgileObject_) || IsEqualGUID(riid, &IID_ActivationFactory_))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    return WindowsCreateString(ControllerClassName, ARRAYSIZE(ControllerClassName) - 1, name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    CompositorController *controller;

    TRACE("instance %p\n", instance);
    if (!instance) return E_POINTER;
    controller = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*controller));
    if (!controller) return E_OUTOFMEMORY;
    controller->IController_iface.lpVtbl = (void *)&controller_vtbl;
    controller->IClosable_iface.lpVtbl = (void *)&closable_vtbl;
    controller->refs = 1;
    InitializeCriticalSection(&controller->cs);
    *instance = &controller->IController_iface;
    return S_OK;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static IActivationFactory Factory = { (IActivationFactoryVtbl *)&factory_vtbl };

HRESULT compositor_controller_get_factory(const WCHAR *name, IActivationFactory **factory)
{
    if (wcscmp(name, ControllerClassName))
        return CLASS_E_CLASSNOTAVAILABLE;
    *factory = &Factory;
    return S_OK;
}
