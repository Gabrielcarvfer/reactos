/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for icacls.exe
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Epic Games Launcher's MSI runs
 *   icacls.exe "DIR" /grant "BUILTIN\Users":(OI)(CI)F
 *   icacls.exe "DIR" /remove "BUILTIN\Users"
 * and fails (1603) when icacls is missing.
 */

#include <apitest.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>

/* (On a file system without ACLs, such as FAT, only the exit codes are checked) */
static BOOL HasAcls;
#define ok_acl(cond, ...) ok(!HasAcls || (cond), __VA_ARGS__)

static DWORD RunIcacls(const WCHAR *Args)
{
    WCHAR CommandLine[1024], System[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD ExitCode = 0xdeadbeef;

    GetSystemDirectoryW(System, MAX_PATH);
    _snwprintf(CommandLine, 1023, L"\"%s\\icacls.exe\" %s", System, Args);
    CommandLine[1023] = 0;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcess(%ls) failed: %lu\n", CommandLine, GetLastError());
        return ExitCode;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &ExitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ExitCode;
}

/* The explicit ACE of Sid (allowed or denied), if any */
static BOOL FindAce(LPWSTR Path, PSID Sid, BYTE Type, PDWORD Mask, PBYTE Flags)
{
    PSECURITY_DESCRIPTOR Sd;
    PACL Dacl;
    ACL_SIZE_INFORMATION Info;
    BOOL Found = FALSE;
    ULONG i;

    if (GetNamedSecurityInfoW(Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              NULL, NULL, &Dacl, NULL, &Sd) != ERROR_SUCCESS)
        return FALSE;
    if (Dacl && GetAclInformation(Dacl, &Info, sizeof(Info), AclSizeInformation))
    {
        for (i = 0; i < Info.AceCount; i++)
        {
            ACCESS_ALLOWED_ACE *Ace;

            if (!GetAce(Dacl, i, (LPVOID *)&Ace))
                continue;
            if (Ace->Header.AceType == Type && !(Ace->Header.AceFlags & INHERITED_ACE) &&
                EqualSid(Sid, &Ace->SidStart))
            {
                *Mask = Ace->Mask;
                *Flags = Ace->Header.AceFlags;
                Found = TRUE;
                break;
            }
        }
    }
    LocalFree(Sd);
    return Found;
}

START_TEST(icacls)
{
    WCHAR Dir[MAX_PATH], Args[MAX_PATH * 2];
    WCHAR Root[MAX_PATH];
    PSID Users, Guests;
    DWORD Mask, Ret, FsFlags = 0;
    BYTE Flags;

    ConvertStringSidToSidW(L"S-1-5-32-545", &Users);
    ConvertStringSidToSidW(L"S-1-5-32-546", &Guests);

    GetTempPathW(MAX_PATH, Dir);
    wcscat(Dir, L"icacls_apitest");
    RemoveDirectoryW(Dir);
    ok(CreateDirectoryW(Dir, NULL), "CreateDirectory failed: %lu\n", GetLastError());

    wcsncpy(Root, Dir, 3);
    Root[3] = 0;
    GetVolumeInformationW(Root, NULL, 0, NULL, NULL, &FsFlags, NULL, 0);
    HasAcls = (FsFlags & FS_PERSISTENT_ACLS) != 0;
    if (!HasAcls)
        skip("%ls has no ACLs\n", Root);

    /* Display */
    _snwprintf(Args, _countof(Args), L"\"%s\"", Dir);
    ok_long(RunIcacls(Args), 0);

    /* Grant, by name and by SID */
    _snwprintf(Args, _countof(Args), L"\"%s\" /grant \"BUILTIN\\Users\":(OI)(CI)F", Dir);
    ok_long(RunIcacls(Args), 0);
    ok_acl(FindAce(Dir, Users, ACCESS_ALLOWED_ACE_TYPE, &Mask, &Flags), "No ACE for Users\n");
    ok_acl(Mask == FILE_ALL_ACCESS, "Mask 0x%lx\n", Mask);
    ok_acl(Flags == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE), "Flags 0x%x\n", Flags);

    _snwprintf(Args, _countof(Args), L"\"%s\" /grant *S-1-5-32-546:(RX,W)", Dir);
    ok_long(RunIcacls(Args), 0);
    ok_acl(FindAce(Dir, Guests, ACCESS_ALLOWED_ACE_TYPE, &Mask, &Flags), "No ACE for Guests\n");
    ok_acl(Mask == ((FILE_GENERIC_READ | FILE_GENERIC_EXECUTE | FILE_GENERIC_WRITE)), "Mask 0x%lx\n", Mask);
    ok_acl(Flags == 0, "Flags 0x%x\n", Flags);

    /* Deny, then remove the denials only */
    _snwprintf(Args, _countof(Args), L"\"%s\" /deny *S-1-5-32-546:(D)", Dir);
    ok_long(RunIcacls(Args), 0);
    ok_acl(FindAce(Dir, Guests, ACCESS_DENIED_ACE_TYPE, &Mask, &Flags), "No denial for Guests\n");
    ok_acl(Mask == (DELETE | SYNCHRONIZE), "Mask 0x%lx\n", Mask);
    _snwprintf(Args, _countof(Args), L"\"%s\" /remove:d *S-1-5-32-546", Dir);
    ok_long(RunIcacls(Args), 0);
    ok_acl(!FindAce(Dir, Guests, ACCESS_DENIED_ACE_TYPE, &Mask, &Flags), "Denial for Guests left\n");
    ok_acl(FindAce(Dir, Guests, ACCESS_ALLOWED_ACE_TYPE, &Mask, &Flags), "Grant for Guests removed\n");

    /* Remove, by name */
    _snwprintf(Args, _countof(Args), L"\"%s\" /remove \"BUILTIN\\Users\" *S-1-5-32-546", Dir);
    ok_long(RunIcacls(Args), 0);
    ok_acl(!FindAce(Dir, Users, ACCESS_ALLOWED_ACE_TYPE, &Mask, &Flags), "ACE for Users left\n");
    ok_acl(!FindAce(Dir, Guests, ACCESS_ALLOWED_ACE_TYPE, &Mask, &Flags), "ACE for Guests left\n");

    /* Errors */
    _snwprintf(Args, _countof(Args), L"\"%s\" /grant NoSuchUser_12345:F", Dir);
    Ret = RunIcacls(Args);
    ok(Ret != 0, "Unknown user: %lu\n", Ret);
    _snwprintf(Args, _countof(Args), L"\"%s\\nothere\" /grant *S-1-5-32-545:F", Dir);
    ok_long(RunIcacls(Args), ERROR_FILE_NOT_FOUND);
    _snwprintf(Args, _countof(Args), L"\"%s\" /bogus", Dir);
    Ret = RunIcacls(Args);
    ok(Ret != 0, "Bad option: %lu\n", Ret);

    ok(RemoveDirectoryW(Dir), "RemoveDirectory failed: %lu\n", GetLastError());
    LocalFree(Users);
    LocalFree(Guests);
}
