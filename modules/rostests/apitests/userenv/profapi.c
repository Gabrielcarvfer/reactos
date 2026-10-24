/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test profapi.dll, the user profile helpers userenv builds on
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>

/* Ordinal 114 opens a registry key of a package family. The Windows App SDK
   (PowerToys) imports it, and fails to load where profapi.dll is missing.
   For a family that isn't installed: HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
   and no key (checked on Windows 11). */
typedef HRESULT (WINAPI *OPEN_PACKAGE_KEY)(PCWSTR, PVOID, PCWSTR, REGSAM, PHKEY);

START_TEST(profapi)
{
    HMODULE Module;
    OPEN_PACKAGE_KEY OpenPackageKey;
    HKEY Key;
    HRESULT hr;

    Module = LoadLibraryW(L"profapi.dll");
    ok(Module != NULL, "profapi.dll failed to load: %lu\n", GetLastError());
    if (!Module)
        return;

    OpenPackageKey = (OPEN_PACKAGE_KEY)GetProcAddress(Module, MAKEINTRESOURCEA(114));
    ok(OpenPackageKey != NULL, "ordinal 114 missing\n");
    if (OpenPackageKey)
    {
        Key = (HKEY)(ULONG_PTR)0x1234;
        hr = OpenPackageKey(L"NoSuchPackage_8wekyb3d8bbwe", NULL, NULL, KEY_READ, &Key);
        ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "hr 0x%lx\n", hr);
        ok(Key == NULL, "Key %p\n", Key);

        Key = (HKEY)(ULONG_PTR)0x1234;
        hr = OpenPackageKey(L"NoSuchPackage_8wekyb3d8bbwe", NULL, L"Software", KEY_READ, &Key);
        ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "hr 0x%lx\n", hr);
        ok(Key == NULL, "Key %p\n", Key);
    }
    FreeLibrary(Module);
}
