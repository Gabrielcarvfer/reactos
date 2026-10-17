/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for setting the security of files and folders as programs do
 *
 * - Chromium's base::win::GrantAccessToPath (network service sandbox, on its
 *   cache folder): GetNamedSecurityInfo, SetEntriesInAcl with an LPAC
 *   capability SID (10 subauthorities), SetNamedSecurityInfo.
 * - WiX Burn (Python's setup and other per-user bundles) resets the
 *   permissions of its cache with an empty, unprotected DACL.
 * - Inno Setup 6 creates its temporary folder with a security descriptor made
 *   from an SDDL string.
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

static WCHAR TestDir[MAX_PATH];
static BOOL PersistentAcls;

static void MakePath(PWSTR Path, PCWSTR Name)
{
    StringCchCopyW(Path, MAX_PATH, TestDir);
    StringCchCatW(Path, MAX_PATH, Name);
}

static BOOL HasAceFor(PCWSTR Path, PSID Sid)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Dacl = NULL;
    BOOL Found = FALSE;
    DWORD i;

    if (GetNamedSecurityInfoW((PWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL, &Sd))
        return FALSE;
    for (i = 0; Dacl && i < Dacl->AceCount; i++)
    {
        ACCESS_ALLOWED_ACE *Ace;
        if (GetAce(Dacl, i, (PVOID *)&Ace) && Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            EqualSid(&Ace->SidStart, Sid))
        {
            Found = TRUE;
        }
    }
    LocalFree(Sd);
    return Found;
}

/* Chromium's GrantAccessToPath */
static void GrantAccess(PCWSTR Path)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Dacl = NULL, NewAcl = NULL;
    PSID Sid = NULL;
    EXPLICIT_ACCESSW Access;
    DWORD Error;

    ok(ConvertStringSidToSidW(L"S-1-15-3-1024-1065365936-1281604716-3511738428-1654721687-432734479-3232135806-4053264122-3456934681", &Sid),
       "ConvertStringSidToSid (capability SID) failed: %lu\n", GetLastError());
    if (!Sid)
        return;
    Error = GetNamedSecurityInfoW((PWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &Dacl, NULL, &Sd);
    ok(Error == ERROR_SUCCESS, "%ls: GetNamedSecurityInfo: %lu\n", Path, Error);

    ZeroMemory(&Access, sizeof(Access));
    Access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | DELETE;
    Access.grfAccessMode = GRANT_ACCESS;
    Access.grfInheritance = CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE;
    BuildTrusteeWithSidW(&Access.Trustee, Sid);
    Error = SetEntriesInAclW(1, &Access, Dacl, &NewAcl);
    ok(Error == ERROR_SUCCESS, "%ls: SetEntriesInAcl: %lu\n", Path, Error);
    if (Error == ERROR_SUCCESS)
    {
        Error = SetNamedSecurityInfoW((PWSTR)Path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NewAcl, NULL);
        ok(Error == ERROR_SUCCESS, "%ls: SetNamedSecurityInfo: %lu\n", Path, Error);
        Error = SetNamedSecurityInfoW((PWSTR)Path, SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                      NULL, NULL, NewAcl, NULL);
        ok(Error == ERROR_SUCCESS, "%ls: SetNamedSecurityInfo (unprotected): %lu\n", Path, Error);
        /* FAT keeps no ACLs: Windows reports success for the set and returns
         * a default "Everyone: full access" descriptor when queried. */
        if (PersistentAcls)
            ok(HasAceFor(Path, Sid), "%ls: no ACE for the capability SID\n", Path);
        else
            skip("%ls: the file system keeps no ACLs\n", Path);
        LocalFree(NewAcl);
    }
    if (Sd)
        LocalFree(Sd);
    LocalFree(Sid);
}

static void TestGrantAccessToPath(void)
{
    WCHAR Empty[MAX_PATH], Full[MAX_PATH], Sub[MAX_PATH], File[MAX_PATH];
    HANDLE Handle;

    MakePath(Empty, L"acl1");
    MakePath(Full, L"acl2");
    MakePath(Sub, L"acl2\\Cache_Data");
    MakePath(File, L"acl2\\Cache_Data\\index");
    ok(CreateDirectoryW(Empty, NULL), "CreateDirectory %ls failed: %lu\n", Empty, GetLastError());
    ok(CreateDirectoryW(Full, NULL), "CreateDirectory %ls failed: %lu\n", Full, GetLastError());
    ok(CreateDirectoryW(Sub, NULL), "CreateDirectory %ls failed: %lu\n", Sub, GetLastError());
    Handle = CreateFileW(File, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "CreateFile %ls failed: %lu\n", File, GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Handle);

    GrantAccess(Empty);
    GrantAccess(Full);

    ok(DeleteFileW(File), "DeleteFile %ls failed: %lu\n", File, GetLastError());
    ok(RemoveDirectoryW(Sub), "RemoveDirectory %ls failed: %lu\n", Sub, GetLastError());
    ok(RemoveDirectoryW(Full), "RemoveDirectory %ls failed: %lu\n", Full, GetLastError());
    ok(RemoveDirectoryW(Empty), "RemoveDirectory %ls failed: %lu\n", Empty, GetLastError());
}

/* WiX Burn's empty DACL */
static void TestEmptyDacl(void)
{
    WCHAR File[MAX_PATH], Dir[MAX_PATH];
    ACL Acl;
    SECURITY_DESCRIPTOR Sd;
    HANDLE Handle;
    DWORD Error;

    MakePath(File, L"setsec.txt");
    MakePath(Dir, L"setsec-dir");
    Handle = CreateFileW(File, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "CreateFile %ls failed: %lu\n", File, GetLastError());
    if (Handle == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(Handle);

    ok(InitializeAcl(&Acl, sizeof(ACL), ACL_REVISION), "InitializeAcl failed: %lu\n", GetLastError());
    ok(IsValidAcl(&Acl), "The empty ACL is not valid\n");

    Error = SetNamedSecurityInfoW(File, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &Acl, NULL);
    ok(Error == ERROR_SUCCESS, "Empty DACL: %lu\n", Error);
    Error = SetNamedSecurityInfoW(File, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &Acl, NULL);
    ok(Error == ERROR_SUCCESS, "Empty unprotected DACL: %lu\n", Error);
    Error = SetNamedSecurityInfoW(File, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &Acl, NULL);
    ok(Error == ERROR_SUCCESS, "Empty protected DACL: %lu\n", Error);
    Error = SetNamedSecurityInfoW(File, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
    ok(Error == ERROR_SUCCESS, "NULL DACL: %lu\n", Error);

    InitializeSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&Sd, TRUE, &Acl, FALSE);
    ok(SetFileSecurityW(File, DACL_SECURITY_INFORMATION, &Sd), "SetFileSecurity (empty DACL) failed: %lu\n", GetLastError());

    /* Give access back so that the file can be deleted */
    Error = SetNamedSecurityInfoW(File, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
    ok(Error == ERROR_SUCCESS, "NULL DACL: %lu\n", Error);
    ok(DeleteFileW(File), "DeleteFile %ls failed: %lu\n", File, GetLastError());

    /* A folder */
    ok(CreateDirectoryW(Dir, NULL), "CreateDirectory %ls failed: %lu\n", Dir, GetLastError());
    Error = SetNamedSecurityInfoW(Dir, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL, NULL, &Acl, NULL);
    ok(Error == ERROR_SUCCESS, "Folder, empty unprotected DACL: %lu\n", Error);
    Error = SetNamedSecurityInfoW(Dir, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
    ok(Error == ERROR_SUCCESS, "Folder, NULL DACL: %lu\n", Error);
    ok(RemoveDirectoryW(Dir), "RemoveDirectory %ls failed: %lu\n", Dir, GetLastError());
}

/* Inno Setup's temporary folder */
static void TestSddlDirectory(void)
{
    static const struct
    {
        PCWSTR Sddl;
        BOOL SetsOwner;
    } Tests[] =
    {
        { L"D:(A;OICI;FA;;;BA)", FALSE },
        { L"D:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", FALSE },
        { L"O:BAD:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", TRUE },
        { L"O:BAG:BAD:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", TRUE },
        { L"O:SYD:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", TRUE },
        { L"D:PAI(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)", FALSE },
    };
    SECURITY_ATTRIBUTES Attributes;
    PSECURITY_DESCRIPTOR Sd;
    WCHAR Dir[MAX_PATH];
    BOOL Ret;
    DWORD Error;
    ULONG i;

    MakePath(Dir, L"sddl");
    for (i = 0; i < _countof(Tests); i++)
    {
        Sd = NULL;
        Ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(Tests[i].Sddl, SDDL_REVISION_1, &Sd, NULL);
        ok(Ret, "%ls: convert failed: %lu\n", Tests[i].Sddl, GetLastError());
        if (!Ret)
            continue;
        Attributes.nLength = sizeof(Attributes);
        Attributes.lpSecurityDescriptor = Sd;
        Attributes.bInheritHandle = FALSE;
        Ret = CreateDirectoryW(Dir, &Attributes);
        Error = Ret ? ERROR_SUCCESS : GetLastError();
        /* An owner the caller may not set is refused */
        if (Tests[i].SetsOwner)
            ok(Ret || Error == ERROR_INVALID_OWNER, "%ls: CreateDirectory failed: %lu\n", Tests[i].Sddl, Error);
        else
            ok(Ret, "%ls: CreateDirectory failed: %lu\n", Tests[i].Sddl, Error);
        if (Ret)
            ok(RemoveDirectoryW(Dir), "%ls: RemoveDirectory failed: %lu\n", Tests[i].Sddl, GetLastError());
        LocalFree(Sd);
    }
}

START_TEST(FileSecurity)
{
    WCHAR Root[MAX_PATH];
    DWORD Length, Flags = 0;

    Length = GetTempPathW(_countof(TestDir), TestDir);
    ok(Length != 0, "GetTempPath failed: %lu\n", GetLastError());
    if (!Length)
        return;
    StringCchCatW(TestDir, _countof(TestDir), L"advapi32_FileSecurity\\");
    RemoveDirectoryW(TestDir);
    if (!CreateDirectoryW(TestDir, NULL))
    {
        skip("Cannot create %ls: %lu\n", TestDir, GetLastError());
        return;
    }

    if (GetVolumePathNameW(TestDir, Root, _countof(Root)) &&
        GetVolumeInformationW(Root, NULL, 0, NULL, NULL, &Flags, NULL, 0))
    {
        PersistentAcls = (Flags & FILE_PERSISTENT_ACLS) != 0;
    }
    else
    {
        PersistentAcls = TRUE;
    }

    TestGrantAccessToPath();
    TestEmptyDacl();
    TestSddlDirectory();

    ok(RemoveDirectoryW(TestDir), "RemoveDirectory %ls failed: %lu\n", TestDir, GetLastError());
}
