/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for mandatory integrity labels (Vista+)
 *
 * - A SACL that only holds a mandatory label needs no SeSecurityPrivilege
 *   (Chromium's crash reporter labels its pipe "untrusted").
 * - LABEL_SECURITY_INFORMATION on a process token returns its label
 *   (Chromium reads it at startup).
 */

#include "precomp.h"

#ifndef LABEL_SECURITY_INFORMATION
#define LABEL_SECURITY_INFORMATION 0x00000010
#endif
#ifndef SYSTEM_MANDATORY_LABEL_ACE_TYPE
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#endif

static void
Test_PipeWithLabel(void)
{
    /* S:(ML;;;;;S-1-16-0): untrusted mandatory label */
    BYTE Buffer[64] = { 0 };
    SECURITY_DESCRIPTOR_RELATIVE *Sd = (SECURITY_DESCRIPTOR_RELATIVE *)Buffer;
    ACL *Sacl = (ACL *)(Sd + 1);
    ACE_HEADER *Ace = (ACE_HEADER *)(Sacl + 1);
    PSID Sid = (PBYTE)(Ace + 1) + sizeof(ACCESS_MASK);
    SID_IDENTIFIER_AUTHORITY LabelAuthority = { { 0, 0, 0, 0, 0, 16 } };
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), Buffer, FALSE };
    HANDLE Pipe;

    Sd->Revision = SECURITY_DESCRIPTOR_REVISION;
    Sd->Control = SE_SELF_RELATIVE | SE_SACL_PRESENT;
    Sd->Sacl = sizeof(*Sd);
    InitializeSid(Sid, &LabelAuthority, 1);
    *GetSidSubAuthority(Sid, 0) = 0;
    Ace->AceType = SYSTEM_MANDATORY_LABEL_ACE_TYPE;
    Ace->AceSize = (WORD)(sizeof(ACE_HEADER) + sizeof(ACCESS_MASK) + GetLengthSid(Sid));
    Sacl->AclRevision = ACL_REVISION;
    Sacl->AclSize = (WORD)(sizeof(ACL) + Ace->AceSize);
    Sacl->AceCount = 1;

    Pipe = CreateNamedPipeW(L"\\\\.\\pipe\\rostest_label", PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 512, 512, 0, &Attributes);
    ok(Pipe != INVALID_HANDLE_VALUE, "CreateNamedPipe with a label failed: %lu\n", GetLastError());
    if (Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe);
}

static void
Test_TokenLabel(void)
{
    HANDLE Token;
    BYTE Buffer[512];
    DWORD Needed;
    BOOL Present, Defaulted;
    PACL Sacl = NULL;
    ACE_HEADER *Ace;

    if (!OpenProcessToken(GetCurrentProcess(), READ_CONTROL, &Token))
    {
        skip("OpenProcessToken failed: %lu\n", GetLastError());
        return;
    }
    ok(GetKernelObjectSecurity(Token, LABEL_SECURITY_INFORMATION, Buffer, sizeof(Buffer), &Needed),
       "GetKernelObjectSecurity failed: %lu\n", GetLastError());
    ok(GetSecurityDescriptorSacl(Buffer, &Present, &Sacl, &Defaulted), "No SACL info\n");
    ok(Present && Sacl != NULL, "No SACL with the label\n");
    if (Present && Sacl && Sacl->AceCount)
    {
        ok(GetAce(Sacl, 0, (PVOID *)&Ace), "GetAce failed\n");
        ok(Ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "ACE type %u\n", Ace->AceType);
    }
    CloseHandle(Token);
}

START_TEST(SecurityLabel)
{
    if (LOBYTE(LOWORD(GetVersion())) < 6)
    {
        skip("Integrity labels are Vista+\n");
        return;
    }
    Test_PipeWithLabel();
    Test_TokenLabel();
}
