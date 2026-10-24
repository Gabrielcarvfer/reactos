/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for AccSetRunningUtilityState, AccNotifyTouchInteraction
 *              and CreateStdAccessibleProxyW
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* NVDA 2026 binds them when it starts (winBindings.oleacc) */

#include <apitest.h>
#define COBJMACROS
#include <ole2.h>
#include <oleacc.h>

typedef HRESULT (WINAPI *PFN_SET_UTILITY_STATE)(HWND, DWORD, DWORD);
typedef HRESULT (WINAPI *PFN_NOTIFY_TOUCH)(HWND, HWND, POINT);
typedef HRESULT (WINAPI *PFN_CREATE_PROXY)(HWND, LPCWSTR, LONG, REFIID, void **);

#define ANRUS_ON_SCREEN_KEYBOARD_ACTIVE 0x0000001

START_TEST(UtilityState)
{
    HMODULE Oleacc = GetModuleHandleW(L"oleacc.dll");
    PFN_SET_UTILITY_STATE pAccSetRunningUtilityState;
    PFN_NOTIFY_TOUCH pAccNotifyTouchInteraction;
    PFN_CREATE_PROXY pCreateStdAccessibleProxyW;
    IAccessible *Acc = NULL;
    HRESULT hr;
    HWND hwnd;

    if (!Oleacc)
        Oleacc = LoadLibraryW(L"oleacc.dll");
    pAccSetRunningUtilityState = (PFN_SET_UTILITY_STATE)GetProcAddress(Oleacc, "AccSetRunningUtilityState");
    pAccNotifyTouchInteraction = (PFN_NOTIFY_TOUCH)GetProcAddress(Oleacc, "AccNotifyTouchInteraction");
    pCreateStdAccessibleProxyW = (PFN_CREATE_PROXY)GetProcAddress(Oleacc, "CreateStdAccessibleProxyW");
    ok(pAccSetRunningUtilityState != NULL, "AccSetRunningUtilityState is missing\n");
    ok(pAccNotifyTouchInteraction != NULL, "AccNotifyTouchInteraction is missing\n");
    ok(pCreateStdAccessibleProxyW != NULL, "CreateStdAccessibleProxyW is missing\n");

    hwnd = CreateWindowExW(0, L"BUTTON", L"Test", WS_POPUP, 0, 0, 50, 20, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed\n");

    if (pCreateStdAccessibleProxyW)
    {
        hr = pCreateStdAccessibleProxyW(hwnd, L"Button", OBJID_CLIENT, &IID_IAccessible, (void **)&Acc);
        ok(hr == S_OK && Acc != NULL, "CreateStdAccessibleProxyW: 0x%08lx\n", hr);
        if (Acc)
            IAccessible_Release(Acc);
    }

    if (pAccSetRunningUtilityState)
    {
        /* (Only for a process with UIAccess) */
        hr = pAccSetRunningUtilityState(hwnd, ANRUS_ON_SCREEN_KEYBOARD_ACTIVE, 0);
        ok(hr == E_ACCESSDENIED, "AccSetRunningUtilityState: 0x%08lx\n", hr);
    }

    DestroyWindow(hwnd);
}
