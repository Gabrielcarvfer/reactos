/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for BuildSecurityDescriptorW
 *
 * It returns an error code. The old stub returned FALSE, i.e. ERROR_SUCCESS,
 * with no descriptor: callers (Chromium's crash reporter) used garbage.
 */

#include "precomp.h"
#include <aclapi.h>
#include <sddl.h>

START_TEST(BuildSecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Base, NewSd = NULL;
    EXPLICIT_ACCESSW Access;
    WCHAR User[] = L"CURRENT_USER";
    ULONG Size = 0;
    BOOL Present, Defaulted;
    PACL Dacl;
    DWORD Error;

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)", SDDL_REVISION_1, &Base, NULL),
       "Convert failed: %lu\n", GetLastError());

    BuildExplicitAccessWithNameW(&Access, User, GENERIC_ALL, GRANT_ACCESS, NO_INHERITANCE);
    Error = BuildSecurityDescriptorW(NULL, NULL, 1, &Access, 0, NULL, Base, &Size, &NewSd);
    ok(Error == ERROR_SUCCESS, "BuildSecurityDescriptorW: %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        ok(NewSd != NULL && Size != 0, "No descriptor (%p, %lu)\n", NewSd, Size);
        ok(IsValidSecurityDescriptor(NewSd), "Invalid descriptor\n");
        ok(GetSecurityDescriptorDacl(NewSd, &Present, &Dacl, &Defaulted) && Present && Dacl,
           "No DACL\n");
        if (Present && Dacl)
        {
            /* The current user (SYSTEM itself in the LiveCD) has an allow ACE */
            BYTE UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
            HANDLE Token;
            DWORD Length, i;
            BOOL Found = FALSE;

            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token);
            GetTokenInformation(Token, TokenUser, UserBuffer, sizeof(UserBuffer), &Length);
            CloseHandle(Token);
            for (i = 0; i < Dacl->AceCount; i++)
            {
                ACCESS_ALLOWED_ACE *Ace;
                if (GetAce(Dacl, i, (PVOID *)&Ace) && Ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    EqualSid(&Ace->SidStart, ((PTOKEN_USER)UserBuffer)->User.Sid))
                {
                    Found = TRUE;
                }
            }
            ok(Found, "No allow ACE for the current user in %u ACEs\n", Dacl->AceCount);
        }
        LocalFree(NewSd);
    }
    LocalFree(Base);
}
