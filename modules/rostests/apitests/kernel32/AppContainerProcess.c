/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for AppContainer processes (CreateProcess(AsUser) with
 *              PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES) and their SIDs
 *
 * Chromium-based browsers (Edge 155, WebView2 hosts) start their renderers
 * in AppContainers this way. The attribute was ignored and those launches
 * never produced a working renderer.
 */

#include "precomp.h"
#include <sddl.h>

#ifndef PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
#define PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES 0x00020009
#endif

#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

/* (TOKEN_APPCONTAINER_INFORMATION) */
typedef struct { PSID TokenAppContainer; } TEST_APPCONTAINER_INFORMATION;

typedef struct
{
    PSID AppContainerSid;
    PSID_AND_ATTRIBUTES Capabilities;
    DWORD CapabilityCount;
    DWORD Reserved;
} TEST_SECURITY_CAPABILITIES;

typedef HRESULT (WINAPI *PCREATEPROFILE)(PCWSTR, PCWSTR, PCWSTR, PSID_AND_ATTRIBUTES, DWORD, PSID *);
typedef HRESULT (WINAPI *PDERIVESID)(PCWSTR, PSID *);
typedef HRESULT (WINAPI *PDELETEPROFILE)(PCWSTR);

typedef BOOL (WINAPI *PINITIALIZE)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, PSIZE_T);
typedef BOOL (WINAPI *PUPDATE)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T);

static PINITIALIZE pInitializeProcThreadAttributeList;
static PUPDATE pUpdateProcThreadAttribute;

/* Start cmd.exe suspended in an AppContainer, check its token */
static void TestCreate(PSID Package, BOOL AsUser)
{
    TEST_SECURITY_CAPABILITIES Caps = { Package, NULL, 0, 0 };
    STARTUPINFOEXW si = { { sizeof(si) } };
    PROCESS_INFORMATION pi;
    WCHAR Cmd[MAX_PATH], Dir[MAX_PATH];
    HANDLE Token = NULL, Primary = NULL, ChildToken;
    UCHAR Buffer[256];
    ULONG IsAppContainer = 0, Length;
    SIZE_T Size = 0;
    BOOL Ok;

    pInitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    si.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(pInitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &Size), "Initialize: %lu\n", GetLastError());
    ok(pUpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                  &Caps, sizeof(Caps), NULL, NULL), "Update: %lu\n", GetLastError());
    /* (An AppContainer may not see our directory: start in the system one) */
    GetSystemDirectoryW(Dir, MAX_PATH);
    wcscpy(Cmd, Dir);
    wcscat(Cmd, L"\\cmd.exe");

    if (AsUser)
    {
        OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Token);
        DuplicateTokenEx(Token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &Primary);
        Ok = CreateProcessAsUserW(Primary, Cmd, NULL, NULL, NULL, FALSE,
                                  CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, NULL, Dir, &si.StartupInfo, &pi);
    }
    else
    {
        Ok = CreateProcessW(Cmd, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                            NULL, Dir, &si.StartupInfo, &pi);
    }
    ok(Ok, "%s: %lu\n", AsUser ? "CreateProcessAsUserW" : "CreateProcessW", GetLastError());
    if (Ok)
    {
        ok(OpenProcessToken(pi.hProcess, TOKEN_QUERY, &ChildToken), "OpenProcessToken: %lu\n", GetLastError());
        ok(GetTokenInformation(ChildToken, TokenIsAppContainer, &IsAppContainer, sizeof(IsAppContainer), &Length) &&
           IsAppContainer == 1, "%s: TokenIsAppContainer %lu (%lu)\n", AsUser ? "as user" : "own token",
           IsAppContainer, GetLastError());
        ok(GetTokenInformation(ChildToken, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length) &&
           ((TEST_APPCONTAINER_INFORMATION *)Buffer)->TokenAppContainer &&
           EqualSid(((TEST_APPCONTAINER_INFORMATION *)Buffer)->TokenAppContainer, Package),
           "%s: TokenAppContainerSid (%lu)\n", AsUser ? "as user" : "own token", GetLastError());
        CloseHandle(ChildToken);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (Primary) CloseHandle(Primary);
    if (Token) CloseHandle(Token);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
}

START_TEST(AppContainerProcess)
{
    static const WCHAR Name[] = L"ReactOS.Apitest.AppContainerProcess";
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll"), Userenv = LoadLibraryW(L"userenv.dll");
    PCREATEPROFILE pCreateAppContainerProfile;
    PDERIVESID pDeriveAppContainerSidFromAppContainerName;
    PDELETEPROFILE pDeleteAppContainerProfile;
    PSID Package = NULL;
    HRESULT hr;

    pInitializeProcThreadAttributeList = (PINITIALIZE)GetProcAddress(Kernel32, "InitializeProcThreadAttributeList");
    pUpdateProcThreadAttribute = (PUPDATE)GetProcAddress(Kernel32, "UpdateProcThreadAttribute");
    if (!pInitializeProcThreadAttributeList || !pUpdateProcThreadAttribute)
    {
        skip("No process attribute lists\n");
        return;
    }
    pCreateAppContainerProfile = (PCREATEPROFILE)GetProcAddress(Userenv, "CreateAppContainerProfile");
    pDeriveAppContainerSidFromAppContainerName = (PDERIVESID)GetProcAddress(Userenv, "DeriveAppContainerSidFromAppContainerName");
    pDeleteAppContainerProfile = (PDELETEPROFILE)GetProcAddress(Userenv, "DeleteAppContainerProfile");
    if (!pCreateAppContainerProfile || !pDeriveAppContainerSidFromAppContainerName || !pDeleteAppContainerProfile)
    {
        skip("No AppContainer profiles\n");
        return;
    }

    /* (As Chromium: the profile first; Windows needs it for the process) */
    hr = pCreateAppContainerProfile(Name, Name, Name, NULL, 0, &Package);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = pDeriveAppContainerSidFromAppContainerName(Name, &Package);
    ok(hr == S_OK && Package, "AppContainer profile: 0x%lx\n", hr);
    if (FAILED(hr))
        return;
    TestCreate(Package, FALSE);
    TestCreate(Package, TRUE);
    FreeSid(Package);
    pDeleteAppContainerProfile(Name);
}

typedef BOOL (WINAPI *PDERIVECAPS)(LPCWSTR, PSID **, DWORD *, PSID **, DWORD *);
typedef HRESULT (WINAPI *PDERIVERESTRICTED)(PSID, PCWSTR, PSID *);

static void CheckSid(PSID Sid, const WCHAR *Expected, const char *What)
{
    LPWSTR String = NULL;
    ok(Sid && ConvertSidToStringSidW(Sid, &String) && !wcscmp(String, Expected),
       "%s: %ls, expected %ls\n", What, String ? String : L"(none)", Expected);
    LocalFree(String);
}

/* The SIDs Chromium derives for its sandbox (values from Windows 11) */
START_TEST(AppContainerSids)
{
    HMODULE KernelBase = LoadLibraryW(L"kernelbase.dll"), Userenv = LoadLibraryW(L"userenv.dll");
    PDERIVECAPS pDeriveCapabilitySidsFromName = (PDERIVECAPS)GetProcAddress(KernelBase, "DeriveCapabilitySidsFromName");
    PDERIVERESTRICTED pDeriveRestricted =
        (PDERIVERESTRICTED)GetProcAddress(Userenv, "DeriveRestrictedAppContainerSidFromAppContainerSidAndRestrictedName");
    PDERIVESID pDeriveAppContainerSidFromAppContainerName = (PDERIVESID)GetProcAddress(Userenv, "DeriveAppContainerSidFromAppContainerName");
    PSID *Groups, *Caps, Parent, Restricted;
    DWORD GroupCount, CapCount;
    HRESULT hr;

    if (!pDeriveCapabilitySidsFromName || !pDeriveRestricted || !pDeriveAppContainerSidFromAppContainerName)
    {
        skip("No capability SID functions\n");
        return;
    }

    /* A Windows 8 capability has a SID of its own; the group SID is a hash */
    ok(pDeriveCapabilitySidsFromName(L"InternetClient", &Groups, &GroupCount, &Caps, &CapCount),
       "DeriveCapabilitySidsFromName: %lu\n", GetLastError());
    ok(GroupCount == 1 && CapCount == 1, "%lu groups, %lu capabilities\n", GroupCount, CapCount);
    if (GroupCount && CapCount)
    {
        CheckSid(Groups[0], L"S-1-5-32-2779705173-1925339129-2667939958-2414465498-3395756507-4015878651-158944808-788332705", "internetClient group");
        CheckSid(Caps[0], L"S-1-15-3-1", "internetClient");
    }
    ok(pDeriveCapabilitySidsFromName(L"lpacChromeInstallFiles", &Groups, &GroupCount, &Caps, &CapCount),
       "DeriveCapabilitySidsFromName: %lu\n", GetLastError());
    if (GroupCount && CapCount)
    {
        CheckSid(Groups[0], L"S-1-5-32-2302894289-466761758-1166120688-1039016420-2430351297-4240214049-4028510897-3317428798", "lpacChromeInstallFiles group");
        CheckSid(Caps[0], L"S-1-15-3-1024-2302894289-466761758-1166120688-1039016420-2430351297-4240214049-4028510897-3317428798", "lpacChromeInstallFiles");
    }

    hr = pDeriveAppContainerSidFromAppContainerName(L"cr.sb.test", &Parent);
    ok(hr == S_OK, "DeriveAppContainerSidFromAppContainerName: 0x%lx\n", hr);
    if (FAILED(hr))
        return;
    CheckSid(Parent, L"S-1-15-2-3280928306-1834467258-1517837443-1265375191-2115420137-3099475159-2940450926", "cr.sb.test");
    hr = pDeriveRestricted(Parent, L"Renderer", &Restricted);
    ok(hr == S_OK, "DeriveRestrictedAppContainerSid...: 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        CheckSid(Restricted, L"S-1-15-2-3280928306-1834467258-1517837443-1265375191-2115420137-3099475159-2940450926-2835175488-3952735414-1995425142-3422167275", "cr.sb.test/Renderer");
        FreeSid(Restricted);
    }
    FreeSid(Parent);
}
