/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The feature staging API set (api-ms-win-core-featurestaging)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * WinUI 3 and the Windows App SDK (Microsoft.Internal.FrameworkUdk.dll,
 * PowerToys) import these functions from api-ms-win-core-featurestaging-l1-1-0:
 * the set pointed to shell32, which has none of them, so the DLL did not load.
 */

#include <windows.h>
#include <apitest.h>

typedef int (WINAPI *PFN_GET_STATE)(UINT32, int);
typedef UINT32 (WINAPI *PFN_GET_VARIANT)(UINT32, int, UINT32 *, BOOL *);
typedef void (WINAPI *PFN_RECORD_USAGE)(UINT32, UINT32, UINT32, PCSTR);
typedef void (WINAPI *PFN_RECORD_ERROR)(UINT32, const void *);
typedef void (WINAPI *PFN_SUBSCRIBE)(HANDLE *, void (WINAPI *)(void *), void *);
typedef void (WINAPI *PFN_UNSUBSCRIBE)(HANDLE);

static void WINAPI Changed(void *Context)
{
}

START_TEST(featurestaging)
{
    HMODULE Module = LoadLibraryW(L"api-ms-win-core-featurestaging-l1-1-0.dll");
    PFN_GET_STATE pGetFeatureEnabledState;
    PFN_GET_VARIANT pGetFeatureVariant;
    PFN_RECORD_USAGE pRecordFeatureUsage;
    PFN_RECORD_ERROR pRecordFeatureError;
    PFN_SUBSCRIBE pSubscribe;
    PFN_UNSUBSCRIBE pUnsubscribe;
    HANDLE Subscription = NULL;
    UINT32 Payload = 0xdeadbeef;
    BOOL HasNotification = TRUE;

    if (!Module)
    {
        skip("No feature staging API set (before Windows 10 1809): %lu\n", GetLastError());
        return;
    }
    pGetFeatureEnabledState = (PFN_GET_STATE)GetProcAddress(Module, "GetFeatureEnabledState");
    pGetFeatureVariant = (PFN_GET_VARIANT)GetProcAddress(Module, "GetFeatureVariant");
    pRecordFeatureUsage = (PFN_RECORD_USAGE)GetProcAddress(Module, "RecordFeatureUsage");
    pRecordFeatureError = (PFN_RECORD_ERROR)GetProcAddress(Module, "RecordFeatureError");
    pSubscribe = (PFN_SUBSCRIBE)GetProcAddress(Module, "SubscribeFeatureStateChangeNotification");
    pUnsubscribe = (PFN_UNSUBSCRIBE)GetProcAddress(Module, "UnsubscribeFeatureStateChangeNotification");
    ok(pGetFeatureEnabledState != NULL, "No GetFeatureEnabledState\n");
    ok(pGetFeatureVariant != NULL, "No GetFeatureVariant\n");
    ok(pRecordFeatureUsage != NULL, "No RecordFeatureUsage\n");
    ok(pRecordFeatureError != NULL, "No RecordFeatureError\n");
    ok(pSubscribe != NULL, "No SubscribeFeatureStateChangeNotification\n");
    ok(pUnsubscribe != NULL, "No UnsubscribeFeatureStateChangeNotification\n");

    /* A feature nobody staged is in its default state */
    if (pGetFeatureEnabledState)
        ok(pGetFeatureEnabledState(0x7ffffff0, 0 /* FEATURE_CHANGE_TIME_READ */) == 0 /* DEFAULT */,
           "Unknown feature not in the default state\n");
    if (pGetFeatureVariant)
    {
        ok(pGetFeatureVariant(0x7ffffff0, 0, &Payload, &HasNotification) == 0, "Unknown feature has a variant\n");
        ok(Payload == 0 && !HasNotification, "Payload %u, notification %d\n", Payload, HasNotification);
    }
    if (pRecordFeatureUsage)
        pRecordFeatureUsage(0x7ffffff0, 0, 1, "ReactOS test");
    if (pSubscribe && pUnsubscribe)
    {
        pSubscribe(&Subscription, Changed, NULL);
        ok(Subscription != NULL, "No subscription\n");
        pUnsubscribe(Subscription);
    }
    FreeLibrary(Module);
}
