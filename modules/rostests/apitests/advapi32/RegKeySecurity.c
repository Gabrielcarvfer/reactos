/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for registry key security (Get/SetNamedSecurityInfo, RegGetKeySecurity)
 *
 * What Opera's installer does before it creates registry keys ("Failed
 * obtaining permissions on MACHINE\Software\Classes\CLSID"): read the parent
 * key's security by its "MACHINE\..." name, check with AccessCheck that this
 * process may create subkeys, add an ACE for the user with SetEntriesInAcl and
 * write it back with SetNamedSecurityInfo. The HKLM keys are only read; the
 * write-back is done on a key of the test under HKCU.
 *
 * Also what Google's updater does: SetSecurityInfo through a KEY_WRITE handle.
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

#define TEST_KEY L"Software\\ReactOS_RegKeySecurity_Test"

static void CheckAccess(PCSTR What, PSECURITY_DESCRIPTOR Sd)
{
    HANDLE Token = NULL, Imp = NULL;
    GENERIC_MAPPING Mapping = { KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS };
    PRIVILEGE_SET Privileges;
    DWORD PrivilegesLength, Granted = 0, Wanted = KEY_CREATE_SUB_KEY | KEY_SET_VALUE | WRITE_DAC;
    BOOL Status = FALSE, Ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &Token))
    {
        ok(0, "%s: OpenProcessToken failed: %lu\n", What, GetLastError());
        return;
    }
    Ret = DuplicateToken(Token, SecurityImpersonation, &Imp);
    ok(Ret, "%s: DuplicateToken failed: %lu\n", What, GetLastError());
    if (Ret)
    {
        MapGenericMask(&Wanted, &Mapping);
        PrivilegesLength = sizeof(Privileges);
        Ret = AccessCheck(Sd, Imp, Wanted, &Mapping, &Privileges, &PrivilegesLength, &Granted, &Status);
        ok(Ret && Status, "%s: AccessCheck create-subkey|set-value|write-dac: ret %d error %lu status %d granted 0x%lx\n",
           What, Ret, Ret ? 0 : GetLastError(), Status, Granted);
        PrivilegesLength = sizeof(Privileges);
        Ret = AccessCheck(Sd, Imp, MAXIMUM_ALLOWED, &Mapping, &Privileges, &PrivilegesLength, &Granted, &Status);
        ok(Ret && Status, "%s: AccessCheck MAXIMUM_ALLOWED: ret %d error %lu status %d granted 0x%lx\n",
           What, Ret, Ret ? 0 : GetLastError(), Status, Granted);
        CloseHandle(Imp);
    }
    CloseHandle(Token);
}

static PSID GetUserSid(PBYTE Buffer, DWORD Size)
{
    HANDLE Token;
    DWORD Length;
    BOOL Ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return NULL;
    Ret = GetTokenInformation(Token, TokenUser, Buffer, Size, &Length);
    CloseHandle(Token);
    return Ret ? ((PTOKEN_USER)Buffer)->User.Sid : NULL;
}

/* WriteBack: also write a DACL back (only for the test's own key) */
static void TestNamed(PCWSTR Name, SE_OBJECT_TYPE Type, BOOL WriteBack)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Dacl = NULL, NewAcl = NULL;
    PSID Owner = NULL, Group = NULL, UserSid;
    EXPLICIT_ACCESSW Access, *Entries = NULL;
    BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    WCHAR CurrentUser[] = L"CURRENT_USER";
    ULONG Count = 0;
    DWORD Error;
    CHAR What[300];

    StringCbPrintfA(What, sizeof(What), "%ls (type %d)", Name, Type);

    Error = GetNamedSecurityInfoW((PWSTR)Name, Type, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL, &Sd);
    ok(Error == ERROR_SUCCESS && Dacl != NULL, "%s: GetNamedSecurityInfo DACL: %lu, dacl %p\n", What, Error, Dacl);
    if (Sd)
        LocalFree(Sd);
    Sd = NULL;
    Dacl = NULL;

    Error = GetNamedSecurityInfoW((PWSTR)Name, Type,
                                  OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &Owner, &Group, &Dacl, NULL, &Sd);
    ok(Error == ERROR_SUCCESS && Dacl && Owner && Group,
       "%s: GetNamedSecurityInfo OWNER|GROUP|DACL: %lu, owner %p group %p dacl %p\n", What, Error, Owner, Group, Dacl);
    if (Error != ERROR_SUCCESS || !Dacl)
    {
        if (Sd)
            LocalFree(Sd);
        return;
    }
    CheckAccess(What, Sd);

    Error = GetExplicitEntriesFromAclW(Dacl, &Count, &Entries);
    ok(Error == ERROR_SUCCESS, "%s: GetExplicitEntriesFromAcl: %lu (%lu entries)\n", What, Error, Count);
    if (Entries)
        LocalFree(Entries);

    /* Give the user full control, as an installer's "permission grantor" does */
    BuildExplicitAccessWithNameW(&Access, CurrentUser, KEY_ALL_ACCESS, GRANT_ACCESS, CONTAINER_INHERIT_ACE);
    Error = SetEntriesInAclW(1, &Access, Dacl, &NewAcl);
    ok(Error == ERROR_SUCCESS, "%s: SetEntriesInAcl CURRENT_USER: %lu\n", What, Error);
    if (NewAcl)
        LocalFree(NewAcl);
    NewAcl = NULL;

    UserSid = GetUserSid(UserBuffer, sizeof(UserBuffer));
    ok(UserSid != NULL, "No user SID\n");
    if (UserSid)
    {
        ZeroMemory(&Access, sizeof(Access));
        Access.grfAccessPermissions = KEY_ALL_ACCESS;
        Access.grfAccessMode = GRANT_ACCESS;
        Access.grfInheritance = CONTAINER_INHERIT_ACE;
        BuildTrusteeWithSidW(&Access.Trustee, UserSid);
        Error = SetEntriesInAclW(1, &Access, Dacl, &NewAcl);
        ok(Error == ERROR_SUCCESS, "%s: SetEntriesInAcl user SID: %lu\n", What, Error);
    }

    if (WriteBack)
    {
        /* The unchanged DACL (what an undo does), then the new one */
        Error = SetNamedSecurityInfoW((PWSTR)Name, Type, DACL_SECURITY_INFORMATION, NULL, NULL, Dacl, NULL);
        ok(Error == ERROR_SUCCESS, "%s: SetNamedSecurityInfo (same DACL): %lu\n", What, Error);
        if (NewAcl)
        {
            Error = SetNamedSecurityInfoW((PWSTR)Name, Type, DACL_SECURITY_INFORMATION, NULL, NULL, NewAcl, NULL);
            ok(Error == ERROR_SUCCESS, "%s: SetNamedSecurityInfo (new DACL): %lu\n", What, Error);
        }
    }

    if (NewAcl)
        LocalFree(NewAcl);
    LocalFree(Sd);
}

static void TestByHandle(HKEY Root, PCWSTR SubKey, REGSAM View, REGSAM Access)
{
    HKEY Key;
    LONG Ret;
    DWORD Length = 0;
    PSECURITY_DESCRIPTOR Sd;
    const SECURITY_INFORMATION Info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    CHAR What[300];

    StringCbPrintfA(What, sizeof(What), "%ls (view 0x%lx)", SubKey, (ULONG)View);
    Ret = RegOpenKeyExW(Root, SubKey, 0, Access | View, &Key);
    ok(Ret == ERROR_SUCCESS, "%s: RegOpenKeyEx 0x%lx: %ld\n", What, (ULONG)Access, Ret);
    if (Ret)
        return;
    Ret = RegGetKeySecurity(Key, Info, NULL, &Length);
    ok(Ret == ERROR_INSUFFICIENT_BUFFER && Length != 0, "%s: RegGetKeySecurity size: %ld, length %lu\n", What, Ret, Length);
    Sd = LocalAlloc(LMEM_FIXED, Length ? Length : 1);
    Ret = RegGetKeySecurity(Key, Info, Sd, &Length);
    ok(Ret == ERROR_SUCCESS, "%s: RegGetKeySecurity: %ld\n", What, Ret);
    if (Ret == ERROR_SUCCESS)
        CheckAccess(What, Sd);
    LocalFree(Sd);
    RegCloseKey(Key);
}

/*
 * What Google's updater does (CreateClientStateMedium): open a key with
 * KEY_WRITE only (no WRITE_DAC), read its DACL with GetSecurityInfo, add an
 * inheritable ACE and write it back with SetSecurityInfo on the same handle.
 * That works on Windows when the caller may change the key's security.
 */
static void TestSetInfoThroughWriteHandle(void)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID Interactive = NULL;
    PSECURITY_DESCRIPTOR Sd = NULL, Sd2 = NULL;
    PACL Dacl = NULL, Dacl2 = NULL, NewAcl = NULL;
    ACL_SIZE_INFORMATION Info, Info2;
    DWORD Size, i, Err;
    PVOID Ace;
    HKEY Key;
    LONG Ret;

    Ret = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0, KEY_WRITE, NULL, &Key, NULL);
    ok(Ret == ERROR_SUCCESS, "RegCreateKeyEx(KEY_WRITE): %ld\n", Ret);
    if (Ret)
        return;

    Err = GetSecurityInfo(Key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL, &Sd);
    ok(Err == ERROR_SUCCESS, "GetSecurityInfo(KEY_WRITE handle): %lu\n", Err);
    if (Err || !Dacl)
        goto Cleanup;

    AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_INTERACTIVE_RID, 0, 0, 0, 0, 0, 0, 0, &Interactive);
    GetAclInformation(Dacl, &Info, sizeof(Info), AclSizeInformation);
    Size = Info.AclBytesInUse + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(Interactive);
    NewAcl = LocalAlloc(LMEM_FIXED, Size);
    InitializeAcl(NewAcl, Size, ACL_REVISION);
    for (i = 0; i < Info.AceCount; i++)
    {
        if (GetAce(Dacl, i, &Ace))
            AddAce(NewAcl, ACL_REVISION, MAXDWORD, Ace, ((PACE_HEADER)Ace)->AceSize);
    }
    ok(AddAccessAllowedAceEx(NewAcl, ACL_REVISION, CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE | OBJECT_INHERIT_ACE,
                             KEY_READ | KEY_SET_VALUE | KEY_CREATE_SUB_KEY, Interactive),
       "AddAccessAllowedAceEx failed: %lu\n", GetLastError());

    /* The handle itself has no WRITE_DAC */
    {
        SECURITY_DESCRIPTOR NewSd;

        InitializeSecurityDescriptor(&NewSd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&NewSd, TRUE, NewAcl, FALSE);
        Ret = RegSetKeySecurity(Key, DACL_SECURITY_INFORMATION, &NewSd);
        ok(Ret == ERROR_ACCESS_DENIED, "RegSetKeySecurity(KEY_WRITE handle): %ld\n", Ret);
    }

    /* ...but SetSecurityInfo succeeds, the caller may change the DACL */
    Err = SetSecurityInfo(Key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, NewAcl, NULL);
    ok(Err == ERROR_SUCCESS, "SetSecurityInfo(KEY_WRITE handle): %lu\n", Err);

    Err = GetSecurityInfo(Key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl2, NULL, &Sd2);
    ok(Err == ERROR_SUCCESS, "GetSecurityInfo again: %lu\n", Err);
    if (Err == ERROR_SUCCESS && Dacl2)
    {
        GetAclInformation(Dacl2, &Info2, sizeof(Info2), AclSizeInformation);
        ok(Info2.AceCount == Info.AceCount + 1, "ACE count %lu, expected %lu\n", Info2.AceCount, Info.AceCount + 1);
    }

Cleanup:
    if (Sd2) LocalFree(Sd2);
    if (NewAcl) LocalFree(NewAcl);
    if (Interactive) FreeSid(Interactive);
    if (Sd) LocalFree(Sd);
    RegCloseKey(Key);
}

START_TEST(RegKeySecurity)
{
    static const PCWSTR MachineNames[] =
    {
        L"MACHINE\\Software\\Classes\\CLSID",
        L"MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"CLASSES_ROOT\\CLSID",
    };
    HKEY Key;
    LONG Ret;
    ULONG i;

    /* HKLM: read the security and check it in memory only (opening with
     * WRITE_DAC changes nothing) */
    for (i = 0; i < _countof(MachineNames); i++)
    {
        TestNamed(MachineNames[i], SE_REGISTRY_KEY, FALSE);
        TestNamed(MachineNames[i], SE_REGISTRY_WOW64_64KEY, FALSE);
        TestNamed(MachineNames[i], SE_REGISTRY_WOW64_32KEY, FALSE);
    }
    TestByHandle(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", 0, READ_CONTROL | WRITE_DAC);
    TestByHandle(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", KEY_WOW64_64KEY, READ_CONTROL | WRITE_DAC);
    TestByHandle(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, READ_CONTROL | WRITE_DAC);

    /* The test's own key: also write the security back */
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
    Ret = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok(Ret == ERROR_SUCCESS, "RegCreateKeyEx: %ld\n", Ret);
    if (Ret)
        return;
    RegCloseKey(Key);
    TestNamed(L"CURRENT_USER\\" TEST_KEY, SE_REGISTRY_KEY, TRUE);
    TestByHandle(HKEY_CURRENT_USER, TEST_KEY, 0, READ_CONTROL | WRITE_DAC);
    TestSetInfoThroughWriteHandle();
    Ret = RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
    ok(Ret == ERROR_SUCCESS, "RegDeleteKey: %ld\n", Ret);
}
