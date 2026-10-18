/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test reading Win32_NetworkAdapterConfiguration through WMI
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <initguid.h>
#include <wbemcli.h>

/*
 * The ReactOS build of wbemprox left IPAddress and IPSubnet of each row
 * unset: garbage, as the table's memory is not zeroed, and reading them
 * crashed. GetAdaptersAddresses gave records of the XP size, and the
 * gateway list (DefaultIPGateway) was read from the adapter's name.
 * AnyDesk's service queries this class at startup.
 */

static void CheckArrayOrNull(IWbemClassObject *Object, const WCHAR *Name)
{
    VARIANT Value;
    CIMTYPE Type;
    HRESULT hr;
    LONG Upper, i;
    BSTR String;

    VariantInit(&Value);
    hr = IWbemClassObject_Get(Object, Name, 0, &Value, &Type, NULL);
    ok(hr == S_OK, "Get(%S) returned 0x%lx\n", Name, hr);
    if (hr != S_OK) return;

    ok(V_VT(&Value) == VT_NULL || V_VT(&Value) == (VT_ARRAY | VT_BSTR), "%S: type %u\n", Name, V_VT(&Value));
    if (V_VT(&Value) == (VT_ARRAY | VT_BSTR))
    {
        hr = SafeArrayGetUBound(V_ARRAY(&Value), 1, &Upper);
        ok(hr == S_OK, "%S: SafeArrayGetUBound returned 0x%lx\n", Name, hr);
        for (i = 0; hr == S_OK && i <= Upper; i++)
        {
            String = NULL;
            ok(SafeArrayGetElement(V_ARRAY(&Value), &i, &String) == S_OK, "%S[%ld]: no element\n", Name, i);
            SysFreeString(String);
        }
    }
    VariantClear(&Value);
}

START_TEST(NetworkAdapterConfig)
{
    IWbemLocator *Locator = NULL;
    IWbemServices *Services = NULL;
    IEnumWbemClassObject *Enum = NULL;
    IWbemClassObject *Object;
    BSTR Path, Language, Query;
    ULONG Count, Rows = 0;
    HRESULT hr;

    CoInitialize(NULL);
    /* (Windows denies the query with the default impersonation level) */
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                         NULL, EOAC_NONE, NULL);
    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&Locator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%lx\n", hr);
    if (FAILED(hr)) goto Cleanup;

    Path = SysAllocString(L"ROOT\\CIMV2");
    hr = IWbemLocator_ConnectServer(Locator, Path, NULL, NULL, NULL, 0, NULL, NULL, &Services);
    SysFreeString(Path);
    ok(hr == S_OK, "ConnectServer returned 0x%lx\n", hr);
    if (FAILED(hr)) goto Cleanup;

    Language = SysAllocString(L"WQL");
    Query = SysAllocString(L"SELECT * FROM Win32_NetworkAdapterConfiguration");
    hr = IWbemServices_ExecQuery(Services, Language, Query, 0, NULL, &Enum);
    SysFreeString(Language);
    SysFreeString(Query);
    ok(hr == S_OK, "ExecQuery returned 0x%lx\n", hr);
    if (FAILED(hr)) goto Cleanup;

    while (IEnumWbemClassObject_Next(Enum, 10000, 1, &Object, &Count) == S_OK && Count)
    {
        CheckArrayOrNull(Object, L"IPAddress");
        CheckArrayOrNull(Object, L"IPSubnet");
        CheckArrayOrNull(Object, L"DefaultIPGateway");
        IWbemClassObject_Release(Object);
        Rows++;
    }
    ok(Rows > 0, "No network adapter configuration\n");

Cleanup:
    if (Enum) IEnumWbemClassObject_Release(Enum);
    if (Services) IWbemServices_Release(Services);
    if (Locator) IWbemLocator_Release(Locator);
    CoUninitialize();
}
