/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for CheckTokenMembership and the "is the user an administrator" queries
 *
 * Installers ask whether the user is an administrator with
 * CheckTokenMembership, IsUserAnAdmin, the token's groups, TokenElevation and
 * TokenIntegrityLevel; the answers must agree. CheckTokenMembership once
 * damaged the heap of 32-bit programs under WoW64, so the process heap is
 * validated after each call.
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_IsUserAnAdmin)(void);

START_TEST(CheckTokenMembership)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = { SECURITY_NT_AUTHORITY };
    PSID Admins = NULL;
    BOOL Member, Ret, InGroups = FALSE;
    HANDLE Token, Dup = NULL;
    DWORD Length = 0, i;
    PTOKEN_GROUPS Groups;
    TOKEN_ELEVATION Elevation;
    BYTE Label[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    HMODULE Shell32;

    ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt at start\n");
    Ret = AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                   0, 0, 0, 0, 0, 0, &Admins);
    ok(Ret, "AllocateAndInitializeSid failed: %lu\n", GetLastError());
    if (!Ret)
        return;

    Ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &Token);
    ok(Ret, "OpenProcessToken failed: %lu\n", GetLastError());
    if (!Ret)
    {
        FreeSid(Admins);
        return;
    }

    /* The enabled Administrators group in the token */
    GetTokenInformation(Token, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    Ret = Groups && GetTokenInformation(Token, TokenGroups, Groups, Length, &Length);
    ok(Ret, "GetTokenInformation (TokenGroups) failed: %lu\n", GetLastError());
    for (i = 0; Ret && i < Groups->GroupCount; i++)
    {
        if (EqualSid(Groups->Groups[i].Sid, Admins) &&
            (Groups->Groups[i].Attributes & SE_GROUP_ENABLED) &&
            !(Groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY))
        {
            InGroups = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, Groups);

    Member = -1;
    Ret = CheckTokenMembership(NULL, Admins, &Member);
    ok(Ret, "CheckTokenMembership (NULL) failed: %lu\n", GetLastError());
    ok(Member == InGroups, "CheckTokenMembership says %d, the token's groups %d\n", Member, InGroups);
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt after CheckTokenMembership\n");

    /* A duplicated impersonation token, several times (the WoW64 heap damage) */
    Ret = DuplicateTokenEx(Token, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, SecurityImpersonation, TokenImpersonation, &Dup);
    ok(Ret, "DuplicateTokenEx failed: %lu\n", GetLastError());
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt after DuplicateTokenEx\n");
    for (i = 0; Dup && i < 3; i++)
    {
        Member = -1;
        Ret = CheckTokenMembership(Dup, Admins, &Member);
        ok(Ret, "CheckTokenMembership (dup) failed: %lu\n", GetLastError());
        ok(Member == InGroups, "CheckTokenMembership (dup) says %d, the token's groups %d\n", Member, InGroups);
        ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt after CheckTokenMembership (dup), pass %lu\n", i);
        Member = -1;
        Ret = CheckTokenMembership(NULL, Admins, &Member);
        ok(Ret && Member == InGroups, "CheckTokenMembership (NULL): ret %d, member %d\n", Ret, Member);
        ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt after CheckTokenMembership (NULL), pass %lu\n", i);
    }
    if (Dup)
        CloseHandle(Dup);

    /* shell32's answer */
    Shell32 = LoadLibraryW(L"shell32.dll");
    if (Shell32)
    {
        PFN_IsUserAnAdmin pIsUserAnAdmin = (PFN_IsUserAnAdmin)GetProcAddress(Shell32, "IsUserAnAdmin");
        ok(pIsUserAnAdmin != NULL, "No IsUserAnAdmin\n");
        if (pIsUserAnAdmin)
            ok(!pIsUserAnAdmin() == !InGroups, "IsUserAnAdmin disagrees with the token's groups (%d)\n", InGroups);
        FreeLibrary(Shell32);
    }
    else
    {
        skip("No shell32\n");
    }

    /* Elevation and integrity */
    Ret = GetTokenInformation(Token, TokenElevation, &Elevation, sizeof(Elevation), &Length);
    ok(Ret, "GetTokenInformation (TokenElevation) failed: %lu\n", GetLastError());
    if (Ret && InGroups)
        ok(Elevation.TokenIsElevated != 0, "An administrator's token is not elevated\n");
    Ret = GetTokenInformation(Token, TokenIntegrityLevel, Label, sizeof(Label), &Length);
    ok(Ret, "GetTokenInformation (TokenIntegrityLevel) failed: %lu\n", GetLastError());
    if (Ret && InGroups)
    {
        PSID Level = ((PTOKEN_MANDATORY_LABEL)Label)->Label.Sid;
        DWORD Rid = *GetSidSubAuthority(Level, *GetSidSubAuthorityCount(Level) - 1);
        ok(Rid >= SECURITY_MANDATORY_HIGH_RID, "An administrator's integrity RID is 0x%lx\n", Rid);
    }

    CloseHandle(Token);
    FreeSid(Admins);
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "Heap corrupt at the end\n");
}
