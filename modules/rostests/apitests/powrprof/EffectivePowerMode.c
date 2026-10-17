/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for PowerRegisterForEffectivePowerModeNotifications
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * The Windows App SDK (Microsoft.WindowsAppRuntime.dll, used by PowerToys)
 * imports it: the DLL did not load without it. The callback gets the current
 * mode right after the registration.
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>

typedef VOID WINAPI EFFECTIVE_POWER_MODE_CALLBACK(int Mode, VOID *Context);
typedef HRESULT (WINAPI *PFN_REGISTER)(ULONG, EFFECTIVE_POWER_MODE_CALLBACK *, VOID *, VOID **);
typedef HRESULT (WINAPI *PFN_UNREGISTER)(VOID *);

static LONG Calls;
static int LastMode = -1;

static VOID WINAPI Callback(int Mode, VOID *Context)
{
    LastMode = Mode;
    InterlockedIncrement(&Calls);
    SetEvent((HANDLE)Context);
}

START_TEST(EffectivePowerMode)
{
    HMODULE Powrprof = LoadLibraryW(L"powrprof.dll");
    PFN_REGISTER pRegister = (PFN_REGISTER)GetProcAddress(Powrprof, "PowerRegisterForEffectivePowerModeNotifications");
    PFN_UNREGISTER pUnregister = (PFN_UNREGISTER)GetProcAddress(Powrprof, "PowerUnregisterFromEffectivePowerModeNotifications");
    HANDLE Event;
    VOID *Handle = NULL;
    HRESULT hr;

    if (!pRegister || !pUnregister)
    {
        skip("No effective power mode notifications (before Windows 10 1809)\n");
        return;
    }

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);

    hr = pRegister(0x55, Callback, Event, &Handle);
    ok(hr == E_INVALIDARG, "Unknown version: 0x%08lx\n", hr);
    hr = pRegister(1 /* EFFECTIVE_POWER_MODE_V1 */, NULL, Event, &Handle);
    ok(hr == E_INVALIDARG, "No callback: 0x%08lx\n", hr);

    hr = pRegister(1 /* EFFECTIVE_POWER_MODE_V1 */, Callback, Event, &Handle);
    ok(hr == S_OK, "Register: 0x%08lx\n", hr);
    ok(Handle != NULL, "No handle\n");
    if (hr == S_OK)
    {
        /* The current mode comes right away */
        ok(WaitForSingleObject(Event, 5000) == WAIT_OBJECT_0, "No callback with the current mode\n");
        ok(Calls >= 1, "%ld calls\n", Calls);
        ok(LastMode >= 0 && LastMode <= 6, "Mode %d\n", LastMode);
        hr = pUnregister(Handle);
        ok(hr == S_OK, "Unregister: 0x%08lx\n", hr);
    }

    CloseHandle(Event);
}
