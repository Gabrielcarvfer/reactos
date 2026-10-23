/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for the current-package functions of kernel32
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * A desktop program has no package: the functions return
 * APPMODEL_ERROR_NO_PACKAGE and leave the last error alone. Paint.NET's
 * launcher reads the last error after GetCurrentPackageId; ReactOS set
 * ERROR_CALL_NOT_IMPLEMENTED there and Paint.NET exited with 0x80070078.
 */

#include "precomp.h"

#ifndef APPMODEL_ERROR_NO_PACKAGE
#define APPMODEL_ERROR_NO_PACKAGE 15700
#endif

typedef LONG (WINAPI *PFN_GET_ID)(UINT32 *, BYTE *);
typedef LONG (WINAPI *PFN_GET_NAME)(UINT32 *, PWSTR);

START_TEST(CurrentPackage)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_GET_ID pGetCurrentPackageId = (PFN_GET_ID)GetProcAddress(Kernel32, "GetCurrentPackageId");
    PFN_GET_NAME pGetCurrentPackageFullName = (PFN_GET_NAME)GetProcAddress(Kernel32, "GetCurrentPackageFullName");
    PFN_GET_NAME pGetCurrentPackageFamilyName = (PFN_GET_NAME)GetProcAddress(Kernel32, "GetCurrentPackageFamilyName");
    UINT32 Length;
    WCHAR Name[256];
    BYTE Buffer[512];
    LONG Ret;

    if (!pGetCurrentPackageId || !pGetCurrentPackageFullName || !pGetCurrentPackageFamilyName)
    {
        skip("The package functions are missing (before Windows 8)\n");
        return;
    }

    Length = 0;
    SetLastError(0xdeadbeef);
    Ret = pGetCurrentPackageId(&Length, NULL);
    ok(Ret == APPMODEL_ERROR_NO_PACKAGE, "GetCurrentPackageId returned %ld\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "GetCurrentPackageId set the last error to %lu\n", GetLastError());

    Length = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    Ret = pGetCurrentPackageId(&Length, Buffer);
    ok(Ret == APPMODEL_ERROR_NO_PACKAGE, "GetCurrentPackageId returned %ld\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "GetCurrentPackageId set the last error to %lu\n", GetLastError());

    Length = _countof(Name);
    SetLastError(0xdeadbeef);
    Ret = pGetCurrentPackageFullName(&Length, Name);
    ok(Ret == APPMODEL_ERROR_NO_PACKAGE, "GetCurrentPackageFullName returned %ld\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "GetCurrentPackageFullName set the last error to %lu\n", GetLastError());

    Length = _countof(Name);
    SetLastError(0xdeadbeef);
    Ret = pGetCurrentPackageFamilyName(&Length, Name);
    ok(Ret == APPMODEL_ERROR_NO_PACKAGE, "GetCurrentPackageFamilyName returned %ld\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "GetCurrentPackageFamilyName set the last error to %lu\n", GetLastError());
}
