/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for RtlGetDeviceFamilyInfoEnum / RtlConvertDeviceFamilyInfoToString
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * The setup.exe of Microsoft Edge / WebView2 (PowerToys, TeamViewer) looks
 * RtlGetDeviceFamilyInfoEnum up and stops (int3) without it; it expects the
 * desktop family. Loaded at run time (Windows 10+).
 */

#include "precomp.h"

typedef VOID (NTAPI *PFN_RtlGetDeviceFamilyInfoEnum)(PULONGLONG, PULONG, PULONG);
typedef ULONG (NTAPI *PFN_RtlConvertDeviceFamilyInfoToString)(PULONG, PULONG, PWSTR, PWSTR);

START_TEST(RtlGetDeviceFamilyInfoEnum)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_RtlGetDeviceFamilyInfoEnum pGetInfo;
    PFN_RtlConvertDeviceFamilyInfoToString pToString;
    ULONGLONG UapInfo = 0;
    ULONG Family = 0xdead, Form = 0xdead, FamilySize, FormSize;
    WCHAR FamilyName[64], FormName[64];
    RTL_OSVERSIONINFOW Version = { sizeof(Version) };
    ULONG Status;

    pGetInfo = (PFN_RtlGetDeviceFamilyInfoEnum)GetProcAddress(Ntdll, "RtlGetDeviceFamilyInfoEnum");
    pToString = (PFN_RtlConvertDeviceFamilyInfoToString)GetProcAddress(Ntdll, "RtlConvertDeviceFamilyInfoToString");
    if (!pGetInfo || !pToString)
    {
        skip("No RtlGetDeviceFamilyInfoEnum (before Windows 10)\n");
        return;
    }

    pGetInfo(&UapInfo, &Family, &Form);
    ok(Family == 3 /* DEVICEFAMILYINFOENUM_DESKTOP */, "Family %lu\n", Family);
    ok(Form != 0xdead, "Form not set\n");
    RtlGetVersion(&Version);
    ok((UapInfo >> 48) == Version.dwMajorVersion, "UAP info 0x%I64x, major %lu\n", UapInfo, Version.dwMajorVersion);
    ok(((UapInfo >> 16) & 0xFFFF) == (Version.dwBuildNumber & 0xFFFF), "UAP info 0x%I64x, build %lu\n",
       UapInfo, Version.dwBuildNumber);
    pGetInfo(NULL, NULL, NULL);

    FamilySize = FormSize = 0;
    Status = pToString(&FamilySize, &FormSize, FamilyName, FormName);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "Status 0x%lx\n", Status);
    ok(FamilySize == sizeof(L"Windows.Desktop"), "Family size %lu\n", FamilySize);
    ok(FormSize != 0, "Form size 0\n");

    FamilySize = sizeof(FamilyName);
    FormSize = sizeof(FormName);
    Status = pToString(&FamilySize, &FormSize, FamilyName, FormName);
    ok(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    ok(!wcscmp(FamilyName, L"Windows.Desktop"), "Family '%ls'\n", FamilyName);
}
