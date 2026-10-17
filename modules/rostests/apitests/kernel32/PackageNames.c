/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for the app package name functions of kernel32
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Microsoft.WindowsAppRuntime.dll (PowerToys) imports PackageFamilyNameFromId,
 * Edge's msedge.dll (WebView2) GetPackageApplicationIds: both programs failed
 * to load without them.
 */

#include "precomp.h"

#include <appmodel.h>

typedef LONG (WINAPI *PFN_NAME_FROM_ID)(const PACKAGE_ID *, UINT32 *, PWSTR);
typedef LONG (WINAPI *PFN_GET_APPLICATION_IDS)(PVOID, UINT32 *, BYTE *, UINT32 *);

static const WCHAR MicrosoftPublisher[] =
    L"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US";

START_TEST(PackageNames)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_NAME_FROM_ID pPackageFamilyNameFromId = (PFN_NAME_FROM_ID)GetProcAddress(Kernel32, "PackageFamilyNameFromId");
    PFN_NAME_FROM_ID pPackageFullNameFromId = (PFN_NAME_FROM_ID)GetProcAddress(Kernel32, "PackageFullNameFromId");
    PFN_GET_APPLICATION_IDS pGetPackageApplicationIds = (PFN_GET_APPLICATION_IDS)GetProcAddress(Kernel32, "GetPackageApplicationIds");
    PACKAGE_ID Id = { 0 };
    WCHAR Name[200];
    UINT32 Length, Count;
    BYTE Buffer[64];
    LONG Ret;

    if (!pPackageFamilyNameFromId || !pPackageFullNameFromId || !pGetPackageApplicationIds)
    {
        skip("No package name functions (before Windows 8)\n");
        return;
    }

    Id.name = (PWSTR)L"Microsoft.WindowsAppRuntime.1.8";
    Id.publisher = (PWSTR)MicrosoftPublisher;
    Id.processorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    Id.version.Major = 8000;
    Id.version.Minor = 616;
    Id.version.Build = 304;
    Id.version.Revision = 0;

    /* The size first, then the name; the publisher id is a hash of the publisher */
    Length = 0;
    Ret = pPackageFamilyNameFromId(&Id, &Length, NULL);
    ok(Ret == ERROR_INSUFFICIENT_BUFFER, "PackageFamilyNameFromId (size): %ld\n", Ret);
    ok(Length == wcslen(L"Microsoft.WindowsAppRuntime.1.8_8wekyb3d8bbwe") + 1, "Length %u\n", Length);
    Length = _countof(Name);
    Ret = pPackageFamilyNameFromId(&Id, &Length, Name);
    ok(Ret == ERROR_SUCCESS, "PackageFamilyNameFromId: %ld\n", Ret);
    ok(!wcscmp(Name, L"Microsoft.WindowsAppRuntime.1.8_8wekyb3d8bbwe"), "Family name %ls\n", Name);

    Length = _countof(Name);
    Ret = pPackageFullNameFromId(&Id, &Length, Name);
    ok(Ret == ERROR_SUCCESS, "PackageFullNameFromId: %ld\n", Ret);
    ok(!wcscmp(Name, L"Microsoft.WindowsAppRuntime.1.8_8000.616.304.0_x64__8wekyb3d8bbwe"), "Full name %ls\n", Name);
    ok(Length == wcslen(Name) + 1, "Length %u for %ls\n", Length, Name);

    /* Another publisher, a resource id */
    Id.publisher = (PWSTR)L"CN=ReactOS Test";
    Id.resourceId = (PWSTR)L"en-us";
    Id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    Length = _countof(Name);
    Ret = pPackageFullNameFromId(&Id, &Length, Name);
    ok(Ret == ERROR_SUCCESS, "PackageFullNameFromId: %ld\n", Ret);
    ok(!wcsncmp(Name, L"Microsoft.WindowsAppRuntime.1.8_8000.616.304.0_x86_en-us_", 57) && wcslen(Name) == 57 + 13,
       "Full name %ls\n", Name);

    /* No name or publisher */
    Id.name = NULL;
    Length = _countof(Name);
    Ret = pPackageFamilyNameFromId(&Id, &Length, Name);
    ok(Ret == ERROR_INVALID_PARAMETER, "PackageFamilyNameFromId without a name: %ld\n", Ret);

    /* No package can be opened here */
    Length = sizeof(Buffer);
    Count = 0xdeadbeef;
    Ret = pGetPackageApplicationIds(NULL, &Length, Buffer, &Count);
    ok(Ret == ERROR_INVALID_PARAMETER, "GetPackageApplicationIds(NULL): %ld\n", Ret);
}
