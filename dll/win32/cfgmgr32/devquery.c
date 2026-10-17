/*
 * PROJECT:     ReactOS Configuration Manager Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Device queries (devquery.h, Windows 8+): DevCreateObjectQuery,
 *              DevGetObjects and their helpers
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * ReactOS has no device query engine (on Windows, the DeviceAccess / DevQuery
 * service). These functions accept every query and answer it with no objects:
 * an asynchronous query reports "enumeration completed" to its callback, as
 * Windows does when nothing matches. Programs that watch for devices this way
 * (TeamViewer's service and client import DevCreateObjectQuery) then start and
 * see no such devices.
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <guiddef.h>
#include <devpropdef.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(cfgmgr);

/* devquery.h / devquerydef.h */
typedef enum _DEVPROPSTORE
{
    DEVPROP_STORE_SYSTEM,
    DEVPROP_STORE_USER,
} DEVPROPSTORE;

typedef enum _DEV_OBJECT_TYPE
{
    DevObjectTypeUnknown,
    DevObjectTypeDeviceInterface,
    DevObjectTypeDeviceContainer,
    DevObjectTypeDevice,
    DevObjectTypeDeviceInterfaceClass,
    DevObjectTypeAEP,
    DevObjectTypeAEPContainer,
    DevObjectTypeDeviceInstallerClass,
    DevObjectTypeDeviceInterfaceDisplay,
    DevObjectTypeDeviceContainerDisplay,
    DevObjectTypeAEPService,
    DevObjectTypeDevicePanel,
    DevObjectTypeAEPProtocol,
} DEV_OBJECT_TYPE;

typedef enum _DEV_QUERY_FLAGS
{
    DevQueryFlagNone = 0x0,
    DevQueryFlagUpdateResults = 0x1,
    DevQueryFlagAllProperties = 0x2,
    DevQueryFlagLocalize = 0x4,
    DevQueryFlagAsyncClose = 0x8,
} DEV_QUERY_FLAGS;

typedef enum _DEV_QUERY_STATE
{
    DevQueryStateInitialized,
    DevQueryStateEnumCompleted,
    DevQueryStateAborted,
    DevQueryStateClosed,
} DEV_QUERY_STATE;

typedef enum _DEV_QUERY_RESULT_ACTION
{
    DevQueryResultStateChange,
    DevQueryResultAdd,
    DevQueryResultUpdate,
    DevQueryResultRemove,
} DEV_QUERY_RESULT_ACTION;

typedef struct _DEVPROPCOMPKEY
{
    DEVPROPKEY Key;
    DEVPROPSTORE Store;
    PCWSTR LocaleName;
} DEVPROPCOMPKEY;

typedef struct _DEVPROPERTY
{
    DEVPROPCOMPKEY CompKey;
    DEVPROPTYPE Type;
    ULONG BufferSize;
    PVOID Buffer;
} DEVPROPERTY;

typedef struct _DEV_OBJECT
{
    DEV_OBJECT_TYPE ObjectType;
    PCWSTR pszObjectId;
    ULONG cPropertyCount;
    const DEVPROPERTY *pProperties;
} DEV_OBJECT;

typedef struct _DEV_QUERY_RESULT_ACTION_DATA
{
    DEV_QUERY_RESULT_ACTION Action;
    union
    {
        DEV_QUERY_STATE State;
        DEV_OBJECT DeviceObject;
    } Data;
} DEV_QUERY_RESULT_ACTION_DATA;

typedef struct _DEVPROP_FILTER_EXPRESSION DEVPROP_FILTER_EXPRESSION;
typedef struct _DEV_QUERY_PARAMETER DEV_QUERY_PARAMETER;

struct _HDEVQUERY;
typedef struct _HDEVQUERY *HDEVQUERY;
typedef VOID (WINAPI *PDEV_QUERY_RESULT_CALLBACK)(HDEVQUERY hDevQuery, PVOID pContext,
                                                  const DEV_QUERY_RESULT_ACTION_DATA *pActionData);

#define DEV_QUERY_VALID_FLAGS (DevQueryFlagUpdateResults | DevQueryFlagAllProperties | \
                               DevQueryFlagLocalize | DevQueryFlagAsyncClose)

typedef struct _DEV_QUERY
{
    PDEV_QUERY_RESULT_CALLBACK Callback;
    PVOID Context;
    ULONG Flags;
    LONG RefCount;      /* the caller's handle and each queued callback */
    LONG Closed;
    DWORD CallbackThread;
    HANDLE Idle;        /* set while no callback runs or is queued */
} DEV_QUERY;

static void ReleaseQuery(DEV_QUERY *Query)
{
    if (InterlockedDecrement(&Query->RefCount))
        return;
    CloseHandle(Query->Idle);
    HeapFree(GetProcessHeap(), 0, Query);
}

static void CallCallback(DEV_QUERY *Query, DEV_QUERY_STATE State)
{
    DEV_QUERY_RESULT_ACTION_DATA Data;

    ZeroMemory(&Data, sizeof(Data));
    Data.Action = DevQueryResultStateChange;
    Data.Data.State = State;
    Query->CallbackThread = GetCurrentThreadId();
    Query->Callback((HDEVQUERY)Query, Query->Context, &Data);
    Query->CallbackThread = 0;
}

/* The enumeration: no objects, so it is completed at once */
static DWORD WINAPI EnumWorker(PVOID Param)
{
    DEV_QUERY *Query = Param;

    if (!Query->Closed)
        CallCallback(Query, DevQueryStateEnumCompleted);
    SetEvent(Query->Idle);
    ReleaseQuery(Query);
    return 0;
}

/* DevQueryFlagAsyncClose: the callback learns that the query is closed */
static DWORD WINAPI CloseWorker(PVOID Param)
{
    DEV_QUERY *Query = Param;

    WaitForSingleObject(Query->Idle, INFINITE);
    CallCallback(Query, DevQueryStateClosed);
    ReleaseQuery(Query);
    return 0;
}

static HRESULT CreateQuery(ULONG QueryFlags, PDEV_QUERY_RESULT_CALLBACK pCallback, PVOID pContext,
                           HDEVQUERY *phDevQuery)
{
    DEV_QUERY *Query;

    if (!phDevQuery)
        return E_POINTER;
    *phDevQuery = NULL;
    if (!pCallback || (QueryFlags & ~DEV_QUERY_VALID_FLAGS))
        return E_INVALIDARG;

    Query = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Query));
    if (!Query)
        return E_OUTOFMEMORY;
    Query->Callback = pCallback;
    Query->Context = pContext;
    Query->Flags = QueryFlags;
    Query->RefCount = 2;
    Query->Idle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Query->Idle)
    {
        HeapFree(GetProcessHeap(), 0, Query);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!QueueUserWorkItem(EnumWorker, Query, WT_EXECUTEDEFAULT))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());

        CloseHandle(Query->Idle);
        HeapFree(GetProcessHeap(), 0, Query);
        return hr;
    }
    *phDevQuery = (HDEVQUERY)Query;
    return S_OK;
}

HRESULT WINAPI
DevCreateObjectQuery(DEV_OBJECT_TYPE ObjectType, ULONG QueryFlags, ULONG cRequestedProperties,
                     const DEVPROPCOMPKEY *pRequestedProperties, ULONG cFilterExpressionCount,
                     const DEVPROP_FILTER_EXPRESSION *pFilter, PDEV_QUERY_RESULT_CALLBACK pCallback,
                     PVOID pContext, HDEVQUERY *phDevQuery)
{
    FIXME("(%d, 0x%lx, %lu, %p, %lu, %p, %p, %p, %p): no objects\n", ObjectType, QueryFlags,
          cRequestedProperties, pRequestedProperties, cFilterExpressionCount, pFilter, pCallback,
          pContext, phDevQuery);
    return CreateQuery(QueryFlags, pCallback, pContext, phDevQuery);
}

HRESULT WINAPI
DevCreateObjectQueryEx(DEV_OBJECT_TYPE ObjectType, ULONG QueryFlags, ULONG cRequestedProperties,
                       const DEVPROPCOMPKEY *pRequestedProperties, ULONG cFilterExpressionCount,
                       const DEVPROP_FILTER_EXPRESSION *pFilter, ULONG cExtendedParameterCount,
                       const DEV_QUERY_PARAMETER *pExtendedParameters, PDEV_QUERY_RESULT_CALLBACK pCallback,
                       PVOID pContext, HDEVQUERY *phDevQuery)
{
    FIXME("(%d, 0x%lx, %lu, %p, %lu, %p, %lu, %p, %p, %p, %p): no objects\n", ObjectType, QueryFlags,
          cRequestedProperties, pRequestedProperties, cFilterExpressionCount, pFilter,
          cExtendedParameterCount, pExtendedParameters, pCallback, pContext, phDevQuery);
    return CreateQuery(QueryFlags, pCallback, pContext, phDevQuery);
}

VOID WINAPI
DevCloseObjectQuery(HDEVQUERY hDevQuery)
{
    DEV_QUERY *Query = (DEV_QUERY *)hDevQuery;

    TRACE("(%p)\n", hDevQuery);

    if (!Query || InterlockedExchange(&Query->Closed, 1))
        return;

    if (Query->Flags & DevQueryFlagAsyncClose)
    {
        InterlockedIncrement(&Query->RefCount);
        if (!QueueUserWorkItem(CloseWorker, Query, WT_EXECUTEDEFAULT))
            ReleaseQuery(Query);
    }
    else if (Query->CallbackThread != GetCurrentThreadId())
    {
        /* No callback may run after this returns (unless called from one) */
        WaitForSingleObject(Query->Idle, INFINITE);
    }
    ReleaseQuery(Query);
}

HRESULT WINAPI
DevGetObjects(DEV_OBJECT_TYPE ObjectType, ULONG QueryFlags, ULONG cRequestedProperties,
              const DEVPROPCOMPKEY *pRequestedProperties, ULONG cFilterExpressionCount,
              const DEVPROP_FILTER_EXPRESSION *pFilter, PULONG pcObjectCount, const DEV_OBJECT **ppObjects)
{
    FIXME("(%d, 0x%lx, %lu, %p, %lu, %p, %p, %p): no objects\n", ObjectType, QueryFlags,
          cRequestedProperties, pRequestedProperties, cFilterExpressionCount, pFilter, pcObjectCount,
          ppObjects);

    if (!pcObjectCount || !ppObjects)
        return E_POINTER;
    *pcObjectCount = 0;
    *ppObjects = NULL;
    if (QueryFlags & ~(DevQueryFlagAllProperties | DevQueryFlagLocalize))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI
DevGetObjectsEx(DEV_OBJECT_TYPE ObjectType, ULONG QueryFlags, ULONG cRequestedProperties,
                const DEVPROPCOMPKEY *pRequestedProperties, ULONG cFilterExpressionCount,
                const DEVPROP_FILTER_EXPRESSION *pFilter, ULONG cExtendedParameterCount,
                const DEV_QUERY_PARAMETER *pExtendedParameters, PULONG pcObjectCount,
                const DEV_OBJECT **ppObjects)
{
    return DevGetObjects(ObjectType, QueryFlags, cRequestedProperties, pRequestedProperties,
                         cFilterExpressionCount, pFilter, pcObjectCount, ppObjects);
}

VOID WINAPI
DevFreeObjects(ULONG cObjectCount, const DEV_OBJECT *pObjects)
{
    TRACE("(%lu, %p)\n", cObjectCount, pObjects);
    /* DevGetObjects returns no objects: nothing was allocated */
}

HRESULT WINAPI
DevGetObjectProperties(DEV_OBJECT_TYPE ObjectType, PCWSTR pszObjectId, ULONG QueryFlags,
                       ULONG cRequestedProperties, const DEVPROPCOMPKEY *pRequestedProperties,
                       PULONG pcPropertyCount, const DEVPROPERTY **ppProperties)
{
    FIXME("(%d, %S, 0x%lx, %lu, %p, %p, %p): no objects\n", ObjectType, pszObjectId, QueryFlags,
          cRequestedProperties, pRequestedProperties, pcPropertyCount, ppProperties);

    if (!pcPropertyCount || !ppProperties)
        return E_POINTER;
    *pcPropertyCount = 0;
    *ppProperties = NULL;
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

VOID WINAPI
DevFreeObjectProperties(ULONG cPropertyCount, const DEVPROPERTY *pProperties)
{
    TRACE("(%lu, %p)\n", cPropertyCount, pProperties);
}

const DEVPROPERTY * WINAPI
DevFindProperty(const DEVPROPKEY *pKey, DEVPROPSTORE Store, PCWSTR pszLocaleName,
                ULONG cProperties, const DEVPROPERTY *pProperties)
{
    ULONG i;

    if (!pKey)
        return NULL;
    for (i = 0; i < cProperties && pProperties; i++)
    {
        const DEVPROPCOMPKEY *Key = &pProperties[i].CompKey;

        if (IsEqualDevPropKey(Key->Key, *pKey) && Key->Store == Store &&
            ((!Key->LocaleName && !pszLocaleName) ||
             (Key->LocaleName && pszLocaleName && !lstrcmpiW(Key->LocaleName, pszLocaleName))))
        {
            return &pProperties[i];
        }
    }
    return NULL;
}
