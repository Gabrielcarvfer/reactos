/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test COM activation of a class served by a Windows service
 *              (AppID LocalService + ServiceParameters)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * This mirrors what Google's updater does: its COM server is a service with a
 * long name ("GoogleUpdaterInternalService156.0.8067.0", longer than a GUID
 * string) and it only registers its class objects when the service is started
 * with the "--com-service" argument from the AppID's ServiceParameters value.
 */

#define COBJMACROS
#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winsvc.h>
#include <ole2.h>
#include <shlwapi.h>

/* Longer than CHARS_IN_GUID (39) on purpose */
#define TEST_SERVICE_NAME L"RosOle32LocalServiceTest_WithAVeryLongName_1.2.3.4"
#define TEST_CLSID_STR    L"{6E1A3C7B-91D2-4F4A-B0C3-5D2E7A9F1B64}"
#define TEST_SERVICE_ARG  L"--com-service"

static const CLSID CLSID_TestLocalService =
    {0x6e1a3c7b, 0x91d2, 0x4f4a, {0xb0, 0xc3, 0x5d, 0x2e, 0x7a, 0x9f, 0x1b, 0x64}};

/*** Service (server) side ***/

static SERVICE_STATUS_HANDLE status_handle;
static HANDLE stop_event;

static void set_status(DWORD state)
{
    SERVICE_STATUS status = {0};

    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    if (state == SERVICE_RUNNING)
        status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(status_handle, &status);
}

static void WINAPI service_handler(DWORD ctrl)
{
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN)
    {
        set_status(SERVICE_STOP_PENDING);
        SetEvent(stop_event);
    }
}

static HRESULT WINAPI cf_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI cf_AddRef(IClassFactory *iface) { return 2; }
static ULONG WINAPI cf_Release(IClassFactory *iface) { return 1; }

static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    if (outer)
        return CLASS_E_NOAGGREGATION;
    /* The factory doubles as the object: it only needs IUnknown */
    return IClassFactory_QueryInterface(iface, riid, ppv);
}

static HRESULT WINAPI cf_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }

static IClassFactoryVtbl cf_vtbl =
{
    cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer
};
static IClassFactory test_cf = { &cf_vtbl };

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    DWORD cookie = 0;
    HRESULT hr;

    /* Own-process services may pass any name here, like Chromium's "" */
    status_handle = RegisterServiceCtrlHandlerW(L"", service_handler);
    if (!status_handle)
        return;

    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    /* Like the updater: serve COM only when started with --com-service,
     * so a missing ServiceParameters makes the client's activation fail */
    if (SUCCEEDED(hr) && argc >= 2 && !wcscmp(argv[1], TEST_SERVICE_ARG))
    {
        hr = CoRegisterClassObject(&CLSID_TestLocalService, (IUnknown *)&test_cf,
                                   CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    }

    set_status(SERVICE_RUNNING);
    WaitForSingleObject(stop_event, 60000);

    if (cookie)
        CoRevokeClassObject(cookie);
    CoUninitialize();
    CloseHandle(stop_event);
    set_status(SERVICE_STOPPED);
}

static void run_service(void)
{
    SERVICE_TABLE_ENTRYW table[] =
    {
        { (LPWSTR)L"", service_main },
        { NULL, NULL }
    };
    StartServiceCtrlDispatcherW(table);
}

/*** Client side ***/

static void cleanup_registry(void)
{
    SHDeleteKeyW(HKEY_CLASSES_ROOT, L"CLSID\\" TEST_CLSID_STR);
    SHDeleteKeyW(HKEY_CLASSES_ROOT, L"AppID\\" TEST_CLSID_STR);
}

static BOOL setup_registry(void)
{
    HKEY hkey;
    LONG r;

    r = RegCreateKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\" TEST_CLSID_STR, 0, NULL, 0,
                        KEY_WRITE, NULL, &hkey, NULL);
    if (r != ERROR_SUCCESS)
        return FALSE;
    RegSetValueExW(hkey, L"AppID", 0, REG_SZ, (const BYTE *)TEST_CLSID_STR,
                   sizeof(TEST_CLSID_STR));
    RegCloseKey(hkey);

    r = RegCreateKeyExW(HKEY_CLASSES_ROOT, L"AppID\\" TEST_CLSID_STR, 0, NULL, 0,
                        KEY_WRITE, NULL, &hkey, NULL);
    if (r != ERROR_SUCCESS)
        return FALSE;
    RegSetValueExW(hkey, L"LocalService", 0, REG_SZ, (const BYTE *)TEST_SERVICE_NAME,
                   sizeof(TEST_SERVICE_NAME));
    RegSetValueExW(hkey, L"ServiceParameters", 0, REG_SZ, (const BYTE *)TEST_SERVICE_ARG,
                   sizeof(TEST_SERVICE_ARG));
    RegCloseKey(hkey);
    return TRUE;
}

static void stop_and_delete_service(SC_HANDLE service)
{
    SERVICE_STATUS status;
    int i;

    ControlService(service, SERVICE_CONTROL_STOP, &status);
    for (i = 0; i < 50; i++)
    {
        if (!QueryServiceStatus(service, &status) || status.dwCurrentState == SERVICE_STOPPED)
            break;
        Sleep(100);
    }
    DeleteService(service);
    CloseServiceHandle(service);
}

START_TEST(LocalService)
{
    WCHAR path[MAX_PATH], cmdline[MAX_PATH + 64];
    SC_HANDLE scm, service;
    IUnknown *unk = NULL;
    SERVICE_STATUS status;
    HRESULT hr;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "service"))
    {
        run_service();
        return;
    }

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        skip("OpenSCManagerW failed: %lu (not an administrator?)\n", GetLastError());
        return;
    }

    /* Leftovers from an aborted run */
    service = OpenServiceW(scm, TEST_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (service)
        stop_and_delete_service(service);
    cleanup_registry();

    GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    _snwprintf(cmdline, ARRAYSIZE(cmdline), L"\"%s\" LocalService service", path);
    service = CreateServiceW(scm, TEST_SERVICE_NAME, TEST_SERVICE_NAME, SERVICE_ALL_ACCESS,
                             SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                             SERVICE_ERROR_IGNORE, cmdline, NULL, NULL, NULL, NULL, NULL);
    ok(service != NULL, "CreateServiceW failed: %lu\n", GetLastError());
    if (!service)
    {
        CloseServiceHandle(scm);
        return;
    }

    if (!setup_registry())
    {
        skip("Cannot write the class registration\n");
        goto cleanup;
    }

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx failed: 0x%lx\n", hr);

    /* COM must start the service (by its long name, with ServiceParameters
     * as ServiceMain arguments) and wait for its class object */
    hr = CoCreateInstance(&CLSID_TestLocalService, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "CoCreateInstance(CLSCTX_LOCAL_SERVER) failed: 0x%lx\n", hr);
    ok(unk != NULL, "No object returned\n");
    if (unk)
        IUnknown_Release(unk);

    ok(QueryServiceStatus(service, &status), "QueryServiceStatus failed: %lu\n", GetLastError());
    ok(status.dwCurrentState == SERVICE_RUNNING || status.dwCurrentState == SERVICE_START_PENDING,
       "Service state %lu, expected it to be started by COM\n", status.dwCurrentState);

    CoUninitialize();

cleanup:
    stop_and_delete_service(service);
    cleanup_registry();
    CloseServiceHandle(scm);
}
