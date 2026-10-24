/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for NtCreateLowBoxToken
 *
 * Chromium-based browsers (Edge 155, WebView2) start their renderers in
 * AppContainers with this; it was missing and their child processes never
 * got an IPC connection.
 */

#include "precomp.h"

/* (TOKEN_APPCONTAINER_INFORMATION) */
typedef struct { PSID TokenAppContainer; } TEST_APPCONTAINER_INFORMATION;

typedef NTSTATUS (NTAPI *PCREATELOWBOX)(PHANDLE, HANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PSID, ULONG, PSID_AND_ATTRIBUTES, ULONG, HANDLE *);

START_TEST(NtCreateLowBoxToken)
{
    static SID_IDENTIFIER_AUTHORITY AppPackage = { { 0, 0, 0, 0, 0, 15 } };
    static SID_IDENTIFIER_AUTHORITY World = { SECURITY_WORLD_SID_AUTHORITY };
    PCREATELOWBOX pNtCreateLowBoxToken;
    PSID Package = NULL, Everyone = NULL;
    HANDLE Token, LowBox = NULL;
    UCHAR Buffer[256];
    PTOKEN_MANDATORY_LABEL Label = (PTOKEN_MANDATORY_LABEL)Buffer;
    TOKEN_TYPE Type;
    ULONG IsAppContainer;
    ULONG Length;
    NTSTATUS Status;

    pNtCreateLowBoxToken = (PCREATELOWBOX)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");
    if (!pNtCreateLowBoxToken)
    {
        skip("NtCreateLowBoxToken is missing\n");
        return;
    }
    ok(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Token), "OpenProcessToken: %lu\n", GetLastError());
    AllocateAndInitializeSid(&AppPackage, 8, 2, 1, 2, 3, 4, 5, 6, 7, &Package);
    AllocateAndInitializeSid(&World, 1, 0, 0, 0, 0, 0, 0, 0, 0, &Everyone);

    /* Not a package SID */
    Status = pNtCreateLowBoxToken(&LowBox, Token, TOKEN_ALL_ACCESS, NULL, Everyone, 0, NULL, 0, NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "with S-1-1-0: 0x%lx\n", Status);

    Status = pNtCreateLowBoxToken(&LowBox, Token, TOKEN_ALL_ACCESS, NULL, Package, 0, NULL, 0, NULL);
    ok(Status == STATUS_SUCCESS, "NtCreateLowBoxToken: 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(GetTokenInformation(LowBox, TokenType, &Type, sizeof(Type), &Length) && Type == TokenPrimary,
           "token type %d (%lu)\n", Type, GetLastError());
        ok(GetTokenInformation(LowBox, TokenIntegrityLevel, Label, sizeof(Buffer), &Length),
           "TokenIntegrityLevel: %lu\n", GetLastError());
        ok(*GetSidSubAuthority(Label->Label.Sid, 0) == SECURITY_MANDATORY_LOW_RID,
           "integrity 0x%lx\n", *GetSidSubAuthority(Label->Label.Sid, 0));
        IsAppContainer = 0;
        ok(GetTokenInformation(LowBox, TokenIsAppContainer, &IsAppContainer, sizeof(IsAppContainer), &Length) &&
           IsAppContainer == 1, "TokenIsAppContainer %lu (%lu)\n", IsAppContainer, GetLastError());
        ok(GetTokenInformation(LowBox, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length) &&
           ((TEST_APPCONTAINER_INFORMATION *)Buffer)->TokenAppContainer &&
           EqualSid(((TEST_APPCONTAINER_INFORMATION *)Buffer)->TokenAppContainer, Package),
           "TokenAppContainerSid (%lu)\n", GetLastError());
        CloseHandle(LowBox);
    }
    /* An ordinary token is not an AppContainer */
    IsAppContainer = 1;
    ok(GetTokenInformation(Token, TokenIsAppContainer, &IsAppContainer, sizeof(IsAppContainer), &Length) &&
       IsAppContainer == 0, "TokenIsAppContainer %lu (%lu)\n", IsAppContainer, GetLastError());

    FreeSid(Package);
    FreeSid(Everyone);
    CloseHandle(Token);
}
