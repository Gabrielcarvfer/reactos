/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the cfgmgr32 device queries (DevCreateObjectQuery, DevGetObjects)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * TeamViewer's service and client import DevCreateObjectQuery
 * (api-ms-win-devices-query-l1-1-0): without it they do not start. A query
 * must be accepted and its callback must learn that the enumeration is
 * complete. The functions are loaded at run time (Windows 8+).
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>

#define DevObjectTypeDeviceInterface 1
#define DevQueryResultStateChange 0
#define DevQueryStateEnumCompleted 1

typedef struct
{
    ULONG ObjectType;
    PCWSTR pszObjectId;
    ULONG cPropertyCount;
    const void *pProperties;
} TEST_DEV_OBJECT;

typedef struct
{
    ULONG Action;
    union
    {
        ULONG State;
        TEST_DEV_OBJECT DeviceObject;
    } Data;
} TEST_ACTION_DATA;

typedef VOID (WINAPI *QUERY_CALLBACK)(PVOID, PVOID, const TEST_ACTION_DATA *);
typedef HRESULT (WINAPI *PFN_DevCreateObjectQuery)(ULONG, ULONG, ULONG, const void *, ULONG, const void *,
                                                   QUERY_CALLBACK, PVOID, PVOID *);
typedef VOID (WINAPI *PFN_DevCloseObjectQuery)(PVOID);
typedef HRESULT (WINAPI *PFN_DevGetObjects)(ULONG, ULONG, ULONG, const void *, ULONG, const void *,
                                            PULONG, const TEST_DEV_OBJECT **);
typedef VOID (WINAPI *PFN_DevFreeObjects)(ULONG, const TEST_DEV_OBJECT *);

static HANDLE CompletedEvent;
static LONG Callbacks;

static VOID WINAPI QueryCallback(PVOID Query, PVOID Context, const TEST_ACTION_DATA *Data)
{
    InterlockedIncrement(&Callbacks);
    ok(Context == (PVOID)0x1234, "Context %p\n", Context);
    ok(Query != NULL, "No query handle\n");
    if (Data->Action == DevQueryResultStateChange && Data->Data.State == DevQueryStateEnumCompleted)
        SetEvent(CompletedEvent);
}

START_TEST(DevQuery)
{
    HMODULE Cfgmgr32 = LoadLibraryW(L"cfgmgr32.dll");
    PFN_DevCreateObjectQuery pDevCreateObjectQuery;
    PFN_DevCloseObjectQuery pDevCloseObjectQuery;
    PFN_DevGetObjects pDevGetObjects;
    PFN_DevFreeObjects pDevFreeObjects;
    const TEST_DEV_OBJECT *Objects = NULL;
    ULONG Count = 0xdead;
    PVOID Query = NULL;
    HRESULT hr;

    pDevCreateObjectQuery = (PFN_DevCreateObjectQuery)GetProcAddress(Cfgmgr32, "DevCreateObjectQuery");
    pDevCloseObjectQuery = (PFN_DevCloseObjectQuery)GetProcAddress(Cfgmgr32, "DevCloseObjectQuery");
    pDevGetObjects = (PFN_DevGetObjects)GetProcAddress(Cfgmgr32, "DevGetObjects");
    pDevFreeObjects = (PFN_DevFreeObjects)GetProcAddress(Cfgmgr32, "DevFreeObjects");
    if (!pDevCreateObjectQuery || !pDevCloseObjectQuery || !pDevGetObjects || !pDevFreeObjects)
    {
        skip("No device queries (before Windows 8)\n");
        return;
    }

    hr = pDevGetObjects(DevObjectTypeDeviceInterface, 0, 0, NULL, 0, NULL, &Count, &Objects);
    ok(hr == S_OK, "DevGetObjects: 0x%lx\n", hr);
    ok(Count != 0xdead, "DevGetObjects did not set the count\n");
    if (hr == S_OK)
        pDevFreeObjects(Count, Objects);
    hr = pDevGetObjects(DevObjectTypeDeviceInterface, 0, 0, NULL, 0, NULL, NULL, &Objects);
    ok(FAILED(hr), "DevGetObjects without a count: 0x%lx\n", hr);

    hr = pDevCreateObjectQuery(DevObjectTypeDeviceInterface, 0, 0, NULL, 0, NULL, NULL, NULL, &Query);
    ok(hr == E_INVALIDARG, "DevCreateObjectQuery without a callback: 0x%lx\n", hr);

    CompletedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = pDevCreateObjectQuery(DevObjectTypeDeviceInterface, 0, 0, NULL, 0, NULL, QueryCallback,
                               (PVOID)0x1234, &Query);
    ok(hr == S_OK, "DevCreateObjectQuery: 0x%lx\n", hr);
    if (hr == S_OK)
    {
        ok(Query != NULL, "No query handle\n");
        ok(WaitForSingleObject(CompletedEvent, 10000) == WAIT_OBJECT_0,
           "The callback did not report the end of the enumeration (%ld calls)\n", Callbacks);
        pDevCloseObjectQuery(Query);
    }
    CloseHandle(CompletedEvent);
    FreeLibrary(Cfgmgr32);
}
