/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that SetEntriesInAcl keeps and merges the existing ACEs
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

/*
 * SetEntriesInAcl dropped every ACE of the old ACL: AnyDesk adds the rights
 * of its processes one at a time (GetSecurityInfo, SetEntriesInAcl,
 * SetSecurityInfo) and ended up with only the last one, so its frontend
 * could not open its own service process. The expected ACLs are those of
 * Windows 10.
 */
static PACL Apply(PACL Old, ACCESS_MODE Mode, DWORD Mask, DWORD Inheritance, WELL_KNOWN_SID_TYPE Who)
{
    EXPLICIT_ACCESSW Entry;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    DWORD SidSize = sizeof(Sid), Error;
    PACL New = NULL;

    ok(CreateWellKnownSid(Who, NULL, Sid, &SidSize), "CreateWellKnownSid failed: %lu\n", GetLastError());
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.grfAccessPermissions = Mask;
    Entry.grfAccessMode = Mode;
    Entry.grfInheritance = Inheritance;
    Entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Entry.Trustee.ptstrName = (LPWSTR)Sid;
    Error = SetEntriesInAclW(1, &Entry, Old, &New);
    ok(Error == ERROR_SUCCESS, "SetEntriesInAclW returned %lu\n", Error);
    return New;
}

static void Check(PACL Acl, PCWSTR Expected, int Line)
{
    SECURITY_DESCRIPTOR Sd;
    LPWSTR String = NULL;

    InitializeSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&Sd, TRUE, Acl, FALSE);
    ConvertSecurityDescriptorToStringSecurityDescriptorW(&Sd, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &String, NULL);
    ok_(__FILE__, Line)(String && !wcscmp(String, Expected), "Got %S, expected %S\n", String, Expected);
    LocalFree(String);
}

START_TEST(SetEntriesInAcl)
{
    PSECURITY_DESCRIPTOR Sd;
    PACL Acl, Next;
    BOOL Present, Defaulted;

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;0x1;;;AU)",
                                                            SDDL_REVISION_1, &Sd, NULL),
       "ConvertStringSecurityDescriptorToSecurityDescriptorW failed: %lu\n", GetLastError());
    GetSecurityDescriptorDacl(Sd, &Present, &Acl, &Defaulted);

    /* SET_ACCESS: a new allow ACE first, the old ACEs stay */
    Next = Apply(Acl, SET_ACCESS, SYNCHRONIZE, NO_INHERITANCE, WinWorldSid);
    Check(Next, L"D:(A;;0x100000;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    /* GRANT_ACCESS adds to the trustee's allowed rights, in place */
    Acl = Next;
    Next = Apply(Acl, GRANT_ACCESS, PROCESS_QUERY_INFORMATION, NO_INHERITANCE, WinWorldSid);
    LocalFree(Acl);
    Check(Next, L"D:(A;;0x100400;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    Acl = Next;
    Next = Apply(Acl, GRANT_ACCESS, PROCESS_TERMINATE, NO_INHERITANCE, WinBuiltinAdministratorsSid);
    LocalFree(Acl);
    Check(Next, L"D:(A;;0x100400;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    /* A different inheritance is another ACE */
    Acl = Next;
    Next = Apply(Acl, GRANT_ACCESS, PROCESS_TERMINATE, SUB_CONTAINERS_AND_OBJECTS_INHERIT, WinWorldSid);
    LocalFree(Acl);
    Check(Next, L"D:(A;OICI;CC;;;WD)(A;;0x100400;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    /* DENY_ACCESS goes first */
    Acl = Next;
    Next = Apply(Acl, DENY_ACCESS, PROCESS_VM_WRITE, NO_INHERITANCE, WinWorldSid);
    LocalFree(Acl);
    Check(Next, L"D:(D;;WP;;;WD)(A;OICI;CC;;;WD)(A;;0x100400;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    /* REVOKE_ACCESS removes the trustee's allow ACEs, not its deny ACEs */
    Acl = Next;
    Next = Apply(Acl, REVOKE_ACCESS, 0, NO_INHERITANCE, WinWorldSid);
    LocalFree(Acl);
    Check(Next, L"D:(D;;WP;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    /* SET_ACCESS replaces the trustee's allow and deny ACEs */
    Acl = Next;
    Next = Apply(Acl, SET_ACCESS, SYNCHRONIZE, NO_INHERITANCE, WinWorldSid);
    LocalFree(Acl);
    Check(Next, L"D:(A;;0x100000;;;WD)(A;;0x1fffff;;;SY)(A;;0x1fffff;;;BA)(A;ID;CC;;;AU)", __LINE__);

    LocalFree(Next);
    LocalFree(Sd);
}
