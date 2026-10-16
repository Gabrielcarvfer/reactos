/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for CreateRestrictedToken and the token calls of Chromium's sandbox
 *
 * Chromium's sandbox (RestrictedToken::CreateRestricted) builds the token of a
 * sandboxed process: groups made deny-only, restricting SIDs (the logon SID,
 * Restricted), a default DACL edited with SetEntriesInAcl, all privileges
 * removed and a lower integrity level. A failing step gives VS Code's
 * "GPU process launch failed, error code 49"
 * (SBOX_ERROR_CANNOT_CREATE_RESTRICTED_TOKEN).
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

#define TokenSecurityAttributesClass ((TOKEN_INFORMATION_CLASS)39)

static PSID Sid(PCWSTR String)
{
    PSID Result = NULL;
    ok(ConvertStringSidToSidW(String, &Result), "ConvertStringSidToSid(%ls) failed: %lu\n", String, GetLastError());
    return Result;
}

static void CloseTokenHandle(HANDLE *Handle)
{
    if (*Handle)
        CloseHandle(*Handle);
    *Handle = NULL;
}

/* tokentest: the basic calls */
static void TestBasic(HANDLE Token)
{
    HANDLE Restricted = NULL, Dup = NULL;
    SID_AND_ATTRIBUTES RestrictSids[2] = {{0}}, Deny[1] = {{0}};
    PSID Everyone = Sid(L"S-1-1-0"), Users = Sid(L"S-1-5-32-545"), Admins = Sid(L"S-1-5-32-544");
    PSID Low = Sid(L"S-1-16-4096");
    BYTE Buffer[4096];
    DWORD Length;
    TOKEN_MANDATORY_LABEL Label;
    BOOL Ret;

    Ret = DuplicateTokenEx(Token, TOKEN_ALL_ACCESS, NULL, SecurityAnonymous, TokenPrimary, &Dup);
    ok(Ret, "DuplicateTokenEx (primary) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Dup);

    RestrictSids[0].Sid = Everyone;
    RestrictSids[1].Sid = Users;
    Deny[0].Sid = Admins;
    Ret = CreateRestrictedToken(Token, DISABLE_MAX_PRIVILEGE, 1, Deny, 0, NULL, 2, RestrictSids, &Restricted);
    ok(Ret, "CreateRestrictedToken (DISABLE_MAX_PRIVILEGE) failed: %lu\n", GetLastError());
    if (!Ret)
    {
        skip("No restricted token\n");
        goto Cleanup;
    }

    Ret = GetTokenInformation(Restricted, TokenDefaultDacl, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation (TokenDefaultDacl) failed: %lu\n", GetLastError());
    if (Ret)
    {
        Ret = SetTokenInformation(Restricted, TokenDefaultDacl, Buffer, Length);
        ok(Ret, "SetTokenInformation (TokenDefaultDacl) failed: %lu\n", GetLastError());
    }
    Ret = GetTokenInformation(Restricted, TokenGroups, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation (TokenGroups) failed: %lu\n", GetLastError());
    Ret = GetTokenInformation(Restricted, TokenUser, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation (TokenUser) failed: %lu\n", GetLastError());
    Ret = GetTokenInformation(Restricted, TokenIntegrityLevel, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation (TokenIntegrityLevel) failed: %lu\n", GetLastError());

    Label.Label.Sid = Low;
    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    Ret = SetTokenInformation(Restricted, TokenIntegrityLevel, &Label, sizeof(Label) + GetLengthSid(Low));
    ok(Ret, "SetTokenInformation (TokenIntegrityLevel low) failed: %lu\n", GetLastError());

    Ret = GetTokenInformation(Restricted, TokenSecurityAttributesClass, Buffer, sizeof(Buffer), &Length);
    ok(Ret, "GetTokenInformation (TokenSecurityAttributes) failed: %lu\n", GetLastError());

    Ret = AdjustTokenPrivileges(Restricted, TRUE, NULL, 0, NULL, NULL);
    ok(Ret, "AdjustTokenPrivileges (disable all) failed: %lu\n", GetLastError());

    Ret = DuplicateTokenEx(Restricted, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Dup);
    ok(Ret, "DuplicateTokenEx (impersonation) failed: %lu\n", GetLastError());

Cleanup:
    CloseTokenHandle(&Dup);
    CloseTokenHandle(&Restricted);
    LocalFree(Everyone);
    LocalFree(Users);
    LocalFree(Admins);
    LocalFree(Low);
}

/* tokentest2: the full sequence */
static void TestFull(HANDLE Token)
{
    static BYTE GroupsBuffer[8192], UserBuffer[512], DaclBuffer[4096], PrivBuffer[4096];
    PTOKEN_GROUPS Groups = (PTOKEN_GROUPS)GroupsBuffer;
    PTOKEN_USER User = (PTOKEN_USER)UserBuffer;
    PTOKEN_DEFAULT_DACL DefaultDacl = (PTOKEN_DEFAULT_DACL)DaclBuffer;
    PTOKEN_PRIVILEGES Privileges = (PTOKEN_PRIVILEGES)PrivBuffer;
    SID_AND_ATTRIBUTES Deny[64], RestrictSids[8], DenyAll[66], RestrictAll[66], One[1];
    DWORD Length, DenyCount = 0, RestrictCount = 0, DenyAllCount = 0, RestrictAllCount = 0, i, Error;
    PSID RestrictedSid = Sid(L"S-1-5-12"), Everyone = Sid(L"S-1-1-0"), Users = Sid(L"S-1-5-32-545");
    PSID Untrusted = Sid(L"S-1-16-0"), Random = Sid(L"S-1-5-21-1111-2222-3333-4444"), NullSid = Sid(L"S-1-0-0");
    PSID Logon = NULL;
    HANDLE Restricted = NULL, Other = NULL;
    EXPLICIT_ACCESSW Access[3];
    PACL NewDacl = NULL;
    TOKEN_MANDATORY_LABEL Label;
    WCHAR Empty[] = L"";
    BOOL Ret;

    Ret = GetTokenInformation(Token, TokenGroups, GroupsBuffer, sizeof(GroupsBuffer), &Length);
    ok(Ret, "GetTokenInformation (TokenGroups) failed: %lu\n", GetLastError());
    if (!Ret)
        goto Cleanup;
    Ret = GetTokenInformation(Token, TokenUser, UserBuffer, sizeof(UserBuffer), &Length);
    ok(Ret, "GetTokenInformation (TokenUser) failed: %lu\n", GetLastError());
    if (!Ret)
        goto Cleanup;

    for (i = 0; i < Groups->GroupCount && DenyCount < _countof(Deny); i++)
    {
        if (Groups->Groups[i].Attributes & SE_GROUP_LOGON_ID)
            Logon = Groups->Groups[i].Sid;
        /* Chromium skips the integrity label and its exceptions */
        if (Groups->Groups[i].Attributes & SE_GROUP_INTEGRITY)
            continue;
        if (EqualSid(Groups->Groups[i].Sid, Everyone) || EqualSid(Groups->Groups[i].Sid, Users))
            continue;
        Deny[DenyCount].Sid = Groups->Groups[i].Sid;
        Deny[DenyCount++].Attributes = 0;
    }
    trace("%lu groups, %lu deny-only, logon SID %s\n", Groups->GroupCount, DenyCount, Logon ? "found" : "none");

    /* Chromium's stricter levels also make the user SID deny-only, and add a
     * random SID to the restricting ones */
    One[0].Sid = User->User.Sid;
    One[0].Attributes = 0;
    Ret = CreateRestrictedToken(Token, 0, 1, One, 0, NULL, 0, NULL, &Other);
    ok(Ret, "CreateRestrictedToken (user SID deny-only) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Other);
    One[0].Sid = Random;
    Ret = CreateRestrictedToken(Token, 0, 0, NULL, 0, NULL, 1, One, &Other);
    ok(Ret, "CreateRestrictedToken (random restricting SID) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Other);
    Ret = CreateRestrictedToken(Token, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &Other);
    ok(Ret, "CreateRestrictedToken (no lists, DISABLE_MAX_PRIVILEGE) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Other);

    /* USER_LOCKDOWN: the user and all groups deny-only, only S-1-0-0 restricting */
    DenyAll[DenyAllCount].Sid = User->User.Sid;
    DenyAll[DenyAllCount++].Attributes = 0;
    for (i = 0; i < Groups->GroupCount && RestrictAllCount < _countof(RestrictAll) - 1; i++)
    {
        if (Groups->Groups[i].Attributes & SE_GROUP_INTEGRITY)
            continue;
        DenyAll[DenyAllCount].Sid = Groups->Groups[i].Sid;
        DenyAll[DenyAllCount++].Attributes = 0;
        RestrictAll[RestrictAllCount].Sid = Groups->Groups[i].Sid;
        RestrictAll[RestrictAllCount++].Attributes = 0;
    }
    One[0].Sid = NullSid;
    One[0].Attributes = 0;
    Ret = CreateRestrictedToken(Token, DISABLE_MAX_PRIVILEGE, DenyAllCount, DenyAll, 0, NULL, 1, One, &Other);
    ok(Ret, "CreateRestrictedToken (USER_LOCKDOWN) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Other);
    /* USER_RESTRICTED_SAME_ACCESS: all groups and the user restricting */
    RestrictAll[RestrictAllCount].Sid = User->User.Sid;
    RestrictAll[RestrictAllCount++].Attributes = 0;
    Ret = CreateRestrictedToken(Token, 0, 0, NULL, 0, NULL, RestrictAllCount, RestrictAll, &Other);
    ok(Ret, "CreateRestrictedToken (all groups restricting) failed: %lu\n", GetLastError());
    CloseTokenHandle(&Other);

    RestrictSids[RestrictCount++].Sid = Users;
    RestrictSids[RestrictCount++].Sid = Everyone;
    RestrictSids[RestrictCount++].Sid = RestrictedSid;
    RestrictSids[RestrictCount++].Sid = User->User.Sid;
    if (Logon)
        RestrictSids[RestrictCount++].Sid = Logon;
    for (i = 0; i < RestrictCount; i++)
        RestrictSids[i].Attributes = 0;

    Ret = CreateRestrictedToken(Token, DISABLE_MAX_PRIVILEGE, DenyCount, Deny, 0, NULL,
                                RestrictCount, RestrictSids, &Restricted);
    ok(Ret, "CreateRestrictedToken (full) failed: %lu\n", GetLastError());
    if (!Ret)
    {
        skip("No restricted token\n");
        goto Cleanup;
    }

    /* Chromium probes the sizes: the call without a buffer fails and gives the size */
    {
        static const struct { TOKEN_INFORMATION_CLASS Class; PCSTR Name; } Classes[] =
        {
            { TokenUser, "TokenUser" },
            { TokenGroups, "TokenGroups" },
            { TokenPrivileges, "TokenPrivileges" },
            { TokenDefaultDacl, "TokenDefaultDacl" },
            { TokenIntegrityLevel, "TokenIntegrityLevel" },
            { TokenStatistics, "TokenStatistics" },
            { TokenRestrictedSids, "TokenRestrictedSids" },
        };
        for (i = 0; i < _countof(Classes); i++)
        {
            DWORD Size = 0;
            SetLastError(0xdeadbeef);
            Ret = GetTokenInformation(Restricted, Classes[i].Class, NULL, 0, &Size);
            Error = GetLastError();
            ok(!Ret && Size != 0 && Error == ERROR_INSUFFICIENT_BUFFER,
               "Size probe %s: ret %d, size %lu, error %lu\n", Classes[i].Name, Ret, Size, Error);
        }
    }

    /* RemoveAllPrivileges: every privilege with SE_PRIVILEGE_REMOVED */
    Ret = CreateRestrictedToken(Token, 0, 0, NULL, 0, NULL, 0, NULL, &Other);
    ok(Ret, "CreateRestrictedToken (copy) failed: %lu\n", GetLastError());
    if (Ret && GetTokenInformation(Other, TokenPrivileges, PrivBuffer, sizeof(PrivBuffer), &Length))
    {
        trace("%lu privileges before\n", Privileges->PrivilegeCount);
        for (i = 0; i < Privileges->PrivilegeCount; i++)
            Privileges->Privileges[i].Attributes = SE_PRIVILEGE_REMOVED;
        Ret = AdjustTokenPrivileges(Other, FALSE, Privileges, Length, NULL, NULL);
        ok(Ret, "AdjustTokenPrivileges (SE_PRIVILEGE_REMOVED) failed: %lu\n", GetLastError());
        Ret = GetTokenInformation(Other, TokenPrivileges, PrivBuffer, sizeof(PrivBuffer), &Length);
        ok(Ret, "GetTokenInformation (TokenPrivileges) failed: %lu\n", GetLastError());
        if (Ret)
            ok(Privileges->PrivilegeCount == 0, "%lu privileges left after removing all\n", Privileges->PrivilegeCount);
    }
    CloseTokenHandle(&Other);

    /* The default DACL: grant Restricted and the user, revoke the logon SID */
    Ret = GetTokenInformation(Restricted, TokenDefaultDacl, DaclBuffer, sizeof(DaclBuffer), &Length);
    ok(Ret, "GetTokenInformation (TokenDefaultDacl) failed: %lu\n", GetLastError());
    if (Ret)
    {
        BuildExplicitAccessWithNameW(&Access[0], Empty, GENERIC_ALL, GRANT_ACCESS, 0);
        Access[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        Access[0].Trustee.ptstrName = (LPWSTR)RestrictedSid;
        BuildExplicitAccessWithNameW(&Access[1], Empty, GENERIC_ALL, GRANT_ACCESS, 0);
        Access[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        Access[1].Trustee.ptstrName = (LPWSTR)User->User.Sid;
        BuildExplicitAccessWithNameW(&Access[2], Empty, 0, REVOKE_ACCESS, 0);
        Access[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        Access[2].Trustee.ptstrName = (LPWSTR)(Logon ? Logon : Everyone);
        Error = SetEntriesInAclW(3, Access, DefaultDacl->DefaultDacl, &NewDacl);
        ok(Error == ERROR_SUCCESS, "SetEntriesInAcl (grant, grant, revoke): %lu\n", Error);
        if (NewDacl)
        {
            TOKEN_DEFAULT_DACL New = { NewDacl };
            Ret = SetTokenInformation(Restricted, TokenDefaultDacl, &New, sizeof(New));
            ok(Ret, "SetTokenInformation (new TokenDefaultDacl) failed: %lu\n", GetLastError());
            LocalFree(NewDacl);
        }
    }

    /* Untrusted integrity */
    Label.Label.Sid = Untrusted;
    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    Ret = SetTokenInformation(Restricted, TokenIntegrityLevel, &Label, sizeof(Label) + GetLengthSid(Untrusted));
    ok(Ret, "SetTokenInformation (TokenIntegrityLevel untrusted) failed: %lu\n", GetLastError());
    Ret = GetTokenInformation(Restricted, TokenIntegrityLevel, DaclBuffer, sizeof(DaclBuffer), &Length);
    ok(Ret, "GetTokenInformation (TokenIntegrityLevel) failed: %lu\n", GetLastError());
    if (Ret)
    {
        PSID Level = ((PTOKEN_MANDATORY_LABEL)DaclBuffer)->Label.Sid;
        DWORD Rid = *GetSidSubAuthority(Level, *GetSidSubAuthorityCount(Level) - 1);
        ok(Rid == SECURITY_MANDATORY_UNTRUSTED_RID, "Integrity RID is 0x%lx\n", Rid);
    }

Cleanup:
    CloseTokenHandle(&Restricted);
    LocalFree(RestrictedSid);
    LocalFree(Everyone);
    LocalFree(Users);
    LocalFree(Untrusted);
    LocalFree(Random);
    LocalFree(NullSid);
}

START_TEST(RestrictedToken)
{
    HANDLE Token;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Token))
    {
        ok(0, "OpenProcessToken failed: %lu\n", GetLastError());
        return;
    }
    TestBasic(Token);
    TestFull(Token);
    CloseHandle(Token);
}
