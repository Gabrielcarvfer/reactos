/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test which instance handles find a local window class
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * Windows ignores the low word of the instance handle when it looks up a
 * local class: AnyDesk registers its classes with its image base + 0x4000
 * and creates its windows with the image base. ReactOS required the exact
 * handle, and AnyDesk died on "Could not create window".
 */
static void TestCase(PCWSTR Name, ULONG_PTR RegisterOffset, ULONG_PTR CreateOffset, BOOL Found)
{
    HINSTANCE Base = GetModuleHandleW(NULL);
    WNDCLASSEXW Class = { sizeof(Class) };
    WNDCLASSEXW Info = { sizeof(Info) };
    HWND Window;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = (HINSTANCE)((ULONG_PTR)Base + RegisterOffset);
    Class.lpszClassName = Name;
    ok(RegisterClassExW(&Class) != 0, "%S: RegisterClassExW failed: %lu\n", Name, GetLastError());

    SetLastError(0xdeadbeef);
    Window = CreateWindowExW(0, Name, L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL,
                             (HINSTANCE)((ULONG_PTR)Base + CreateOffset), NULL);
    if (Found)
    {
        ok(Window != NULL, "%S: registered with base+%Ix, not found with base+%Ix (%lu)\n",
           Name, RegisterOffset, CreateOffset, GetLastError());
        ok(GetClassInfoExW((HINSTANCE)((ULONG_PTR)Base + CreateOffset), Name, &Info),
           "%S: GetClassInfoExW failed: %lu\n", Name, GetLastError());
    }
    else
    {
        ok(Window == NULL, "%S: registered with base+%Ix, found with base+%Ix\n", Name, RegisterOffset, CreateOffset);
        ok(GetLastError() == ERROR_CANNOT_FIND_WND_CLASS, "%S: error %lu\n", Name, GetLastError());
    }
    if (Window) DestroyWindow(Window);
    UnregisterClassW(Name, Class.hInstance);
}

START_TEST(ClassInstance)
{
    TestCase(L"ClassInstanceA", 0x4000, 0, TRUE);
    TestCase(L"ClassInstanceB", 0, 0x4000, TRUE);
    TestCase(L"ClassInstanceC", 0x123, 0, TRUE);
    TestCase(L"ClassInstanceD", 0x10000, 0, FALSE);
    TestCase(L"ClassInstanceE", 0, 0x7fff0000, FALSE);
}
