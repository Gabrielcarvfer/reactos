/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for NtReadVirtualMemory
 *
 * Reading memory that is not committed must fail cleanly. On x64 ReactOS, a
 * large read (which locks the source pages with an MDL) used to take the
 * whole machine down: the failure is raised as an exception inside the
 * kernel, and the PSEH exception filter ran on a wrong frame (GCC x64).
 */

#include "precomp.h"

static void
Test_ReadUncommitted(SIZE_T Size)
{
    PVOID Reserved = NULL, Buffer;
    SIZE_T Length = 4 * 64 * 1024, Read = 0xdeadbeef;
    NTSTATUS Status;

    /* Reserved but not committed: reading it faults */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Reserved, 0, &Length, MEM_RESERVE, PAGE_READWRITE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Buffer != NULL, "HeapAlloc failed\n");
    if (Buffer)
    {
        Status = NtReadVirtualMemory(NtCurrentProcess(), Reserved, Buffer, Size, &Read);
        ok(Status == STATUS_PARTIAL_COPY || Status == STATUS_ACCESS_VIOLATION,
           "Size %Iu: status 0x%lx, expected STATUS_PARTIAL_COPY or STATUS_ACCESS_VIOLATION\n",
           Size, Status);
        ok(Read == 0, "Size %Iu: read %Iu bytes, expected 0\n", Size, Read);
        HeapFree(GetProcessHeap(), 0, Buffer);
    }

    Length = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &Reserved, &Length, MEM_RELEASE);
}

static void
Test_ReadPartiallyCommitted(void)
{
    PVOID Reserved = NULL, Committed;
    SIZE_T Length = 64 * 1024, Read = 0;
    static UCHAR Buffer[2 * 4096];
    NTSTATUS Status;

    /* One committed page followed by a reserved one: a read across both
     * gets the first page at most */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Reserved, 0, &Length, MEM_RESERVE, PAGE_READWRITE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Committed = Reserved;
    Length = PAGE_SIZE;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Committed, 0, &Length, MEM_COMMIT, PAGE_READWRITE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        memset(Committed, 0x5a, PAGE_SIZE);
        Status = NtReadVirtualMemory(NtCurrentProcess(), Reserved, Buffer, sizeof(Buffer), &Read);
        ok(Status == STATUS_PARTIAL_COPY || Status == STATUS_ACCESS_VIOLATION,
           "status 0x%lx, expected STATUS_PARTIAL_COPY or STATUS_ACCESS_VIOLATION\n", Status);
        ok(Read <= PAGE_SIZE, "read %Iu bytes, expected at most one page\n", Read);
        if (Read)
            ok(Buffer[0] == 0x5a, "wrong data 0x%x\n", Buffer[0]);
    }

    Length = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &Reserved, &Length, MEM_RELEASE);
}

static void
Test_ReadValid(void)
{
    static const char Source[] = "NtReadVirtualMemory";
    char Buffer[sizeof(Source)] = { 0 };
    SIZE_T Read = 0;
    NTSTATUS Status;

    Status = NtReadVirtualMemory(NtCurrentProcess(), (PVOID)Source, Buffer, sizeof(Buffer), &Read);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Read == sizeof(Source), "read %Iu bytes\n", Read);
    ok(!strcmp(Buffer, Source), "wrong data '%s'\n", Buffer);
}

START_TEST(NtReadVirtualMemory)
{
    Test_ReadValid();
    /* Small reads use a pool buffer, large ones lock the pages with an MDL */
    Test_ReadUncommitted(64);
    Test_ReadUncommitted(57344);
    Test_ReadUncommitted(4 * 64 * 1024);
    Test_ReadPartiallyCommitted();
}
