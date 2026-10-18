/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for ApiSetQueryApiSetPresence
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * WinUI 3 (Microsoft.Internal.FrameworkUdk.dll, used by PowerToys) imports it
 * from api-ms-win-core-apiquery-l1-1-0 (ntdll): the DLL did not load.
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PFN_PRESENCE)(PCUNICODE_STRING, PBOOLEAN);

static void Check(PFN_PRESENCE pPresence, PCWSTR Name, BOOLEAN Expected)
{
    UNICODE_STRING String;
    BOOLEAN Present = !Expected;
    NTSTATUS Status;

    RtlInitUnicodeString(&String, Name);
    Status = pPresence(&String, &Present);
    ok(Status == STATUS_SUCCESS, "%ls: 0x%lx\n", Name, Status);
    ok(Present == Expected, "%ls: present %d\n", Name, Present);
}

START_TEST(ApiSetQueryApiSetPresence)
{
    PFN_PRESENCE pPresence = (PFN_PRESENCE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "ApiSetQueryApiSetPresence");

    if (!pPresence)
    {
        skip("No ApiSetQueryApiSetPresence (before Windows 8.1)\n");
        return;
    }

    Check(pPresence, L"api-ms-win-core-synch-l1-2-0", TRUE);
    Check(pPresence, L"api-ms-win-core-synch-l1-2-0.dll", TRUE);
    Check(pPresence, L"api-ms-win-core-featurestaging-l1-1-0", TRUE);
    Check(pPresence, L"api-ms-win-reactos-nonexistent-l1-1-0", FALSE);
    Check(pPresence, L"kernel32", FALSE);
}
