/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for the SID aliases of SDDL strings (case)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * NVDA's helper starts its RPC server with the security descriptor
 * "D:(A;;GA;;;wd)(A;;GA;;;AC)": the aliases are case-insensitive.
 */

#include "precomp.h"
#include <sddl.h>

START_TEST(SddlAliases)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PSID Sid = NULL, World;
    SID_IDENTIFIER_AUTHORITY WorldAuthority = { SECURITY_WORLD_SID_AUTHORITY };
    ULONG Size = 0;
    BOOL Ret;

    AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World);

    Ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;wd)(A;;GA;;;AC)", SDDL_REVISION_1, &Sd, &Size);
    ok(Ret, "ConvertStringSecurityDescriptorToSecurityDescriptorW failed: %lu\n", GetLastError());
    ok(Sd != NULL && Size != 0, "Sd %p, size %lu\n", Sd, Size);
    if (Sd)
    {
        PACL Dacl = NULL;
        BOOL Present = FALSE, Defaulted;
        ACCESS_ALLOWED_ACE *Ace;

        ok(GetSecurityDescriptorDacl(Sd, &Present, &Dacl, &Defaulted) && Present && Dacl,
           "No DACL\n");
        if (Dacl)
        {
            ok(Dacl->AceCount == 2, "%u ACEs\n", Dacl->AceCount);
            if (GetAce(Dacl, 0, (LPVOID *)&Ace))
                ok(EqualSid(&Ace->SidStart, World), "The first ACE isn't for Everyone\n");
        }
        LocalFree(Sd);
    }

    Ret = ConvertStringSidToSidW(L"wd", &Sid);
    ok(Ret, "ConvertStringSidToSidW(wd) failed: %lu\n", GetLastError());
    if (Ret)
    {
        ok(EqualSid(Sid, World), "wd isn't Everyone\n");
        LocalFree(Sid);
    }
    Ret = ConvertStringSidToSidW(L"Ba", &Sid);
    ok(Ret, "ConvertStringSidToSidW(Ba) failed: %lu\n", GetLastError());
    if (Ret)
        LocalFree(Sid);

    FreeSid(World);
}
