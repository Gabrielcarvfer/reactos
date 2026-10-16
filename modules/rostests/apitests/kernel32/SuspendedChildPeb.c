/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for writing the PEB of a new suspended child
 *
 * Chromium's sandbox (ApplyZeroAppShimToSuspendedProcess) creates a child
 * suspended, with a restricted "lockdown" token, queries its PEB with
 * NtQueryInformationProcess and clears PEB->pShimData with
 * WriteProcessMemory. The process handle must carry VM_WRITE | VM_OPERATION
 * also when the child runs with the restricted token.
 */

#include "precomp.h"

#include <ndk/obfuncs.h>
#include <ndk/psfuncs.h>

static VOID
TestChild(
    _In_ PCSTR Label,
    _In_opt_ HANDLE hToken)
{
    WCHAR FileName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 48];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    PROCESS_BASIC_INFORMATION BasicInfo;
    OBJECT_BASIC_INFORMATION ObjectInfo;
    ULONG Length = 0;
    SIZE_T Written = 0;
    PVOID Zero = NULL;
    NTSTATUS Status;
    BOOL Ret;

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" SuspendedChildPeb child", FileName);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;

    if (hToken)
        Ret = CreateProcessAsUserW(hToken, FileName, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
                                   NULL, NULL, &StartupInfo, &ProcessInfo);
    else
        Ret = CreateProcessW(FileName, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
                             NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Ret, "%s: creating the suspended child failed with %lu\n", Label, GetLastError());
    if (!Ret)
        return;

    ZeroMemory(&ObjectInfo, sizeof(ObjectInfo));
    Status = NtQueryObject(ProcessInfo.hProcess, ObjectBasicInformation, &ObjectInfo, sizeof(ObjectInfo), NULL);
    ok(NT_SUCCESS(Status), "%s: NtQueryObject failed with 0x%lx\n", Label, Status);
    ok((ObjectInfo.GrantedAccess & (PROCESS_VM_WRITE | PROCESS_VM_OPERATION)) == (PROCESS_VM_WRITE | PROCESS_VM_OPERATION),
       "%s: process handle access 0x%lx lacks VM_WRITE | VM_OPERATION\n", Label, ObjectInfo.GrantedAccess);

    ZeroMemory(&BasicInfo, sizeof(BasicInfo));
    Status = NtQueryInformationProcess(ProcessInfo.hProcess, ProcessBasicInformation,
                                       &BasicInfo, sizeof(BasicInfo), &Length);
    ok(NT_SUCCESS(Status), "%s: ProcessBasicInformation failed with 0x%lx\n", Label, Status);
    ok(Length == sizeof(BasicInfo), "%s: ProcessBasicInformation length %lu, expected %lu\n",
       Label, Length, (ULONG)sizeof(BasicInfo));
    ok(BasicInfo.PebBaseAddress != NULL, "%s: no PEB address\n", Label);

    if (NT_SUCCESS(Status) && BasicInfo.PebBaseAddress)
    {
        Ret = WriteProcessMemory(ProcessInfo.hProcess,
                                 (PBYTE)BasicInfo.PebBaseAddress + FIELD_OFFSET(PEB, pShimData),
                                 &Zero, sizeof(Zero), &Written);
        ok(Ret && Written == sizeof(Zero), "%s: WriteProcessMemory(PEB->pShimData): %d, %Iu bytes, error %lu\n",
           Label, Ret, Written, GetLastError());
    }

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}

START_TEST(SuspendedChildPeb)
{
    SID_IDENTIFIER_AUTHORITY NullAuthority = { SECURITY_NULL_SID_AUTHORITY };
    SID_AND_ATTRIBUTES Restricting, Deny[64];
    HANDLE hToken = NULL, hRestricted = NULL;
    PSID NullSid = NULL;
    PTOKEN_GROUPS Groups = NULL;
    DWORD Length = 0, i, DenyCount = 0;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
        return;

    TestChild("normal child", NULL);

    /* Like Chromium's lockdown token: all groups but the logon SID deny-only,
       the NULL SID the only restricting SID */
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
    {
        skip("OpenProcessToken failed with %lu\n", GetLastError());
        return;
    }
    GetTokenInformation(hToken, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Groups || !GetTokenInformation(hToken, TokenGroups, Groups, Length, &Length) ||
        !AllocateAndInitializeSid(&NullAuthority, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &NullSid))
    {
        skip("Cannot read the token groups (error %lu)\n", GetLastError());
        goto Cleanup;
    }
    for (i = 0; i < Groups->GroupCount && DenyCount < _countof(Deny); i++)
    {
        if (Groups->Groups[i].Attributes & SE_GROUP_LOGON_ID)
            continue;
        Deny[DenyCount].Sid = Groups->Groups[i].Sid;
        Deny[DenyCount].Attributes = 0;
        DenyCount++;
    }
    Restricting.Sid = NullSid;
    Restricting.Attributes = 0;
    ok(CreateRestrictedToken(hToken, DISABLE_MAX_PRIVILEGE, DenyCount, Deny, 0, NULL, 1, &Restricting, &hRestricted),
       "CreateRestrictedToken failed with %lu\n", GetLastError());
    if (hRestricted)
        TestChild("lockdown token child", hRestricted);

Cleanup:
    if (hRestricted) CloseHandle(hRestricted);
    if (NullSid) FreeSid(NullSid);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    CloseHandle(hToken);
}
