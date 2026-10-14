/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for NtWriteVirtualMemory
 *
 * Writing to memory that is not there must fail cleanly. On x64 ReactOS, a
 * small write to a bad address took the whole machine down: GCC moved the
 * probe of the target out of the range of the kernel's PSEH try block, so the
 * exception found no handler.
 */

#include "precomp.h"

static void
Test_WriteBad(PVOID Address, SIZE_T Size)
{
    PVOID Buffer;
    SIZE_T Written = 0xdeadbeef;
    NTSTATUS Status;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    ok(Buffer != NULL, "HeapAlloc failed\n");
    if (!Buffer)
        return;

    Status = NtWriteVirtualMemory(NtCurrentProcess(), Address, Buffer, Size, &Written);
    ok(Status == STATUS_PARTIAL_COPY || Status == STATUS_ACCESS_VIOLATION,
       "%p, size %Iu: status 0x%lx, expected STATUS_PARTIAL_COPY or STATUS_ACCESS_VIOLATION\n",
       Address, Size, Status);
    ok(Written == 0, "%p, size %Iu: wrote %Iu bytes, expected 0\n", Address, Size, Written);
    HeapFree(GetProcessHeap(), 0, Buffer);
}

static void
Test_WriteUncommitted(SIZE_T Size)
{
    PVOID Reserved = NULL;
    SIZE_T Length = 4 * 64 * 1024;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Reserved, 0, &Length, MEM_RESERVE, PAGE_READWRITE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Test_WriteBad(Reserved, Size);
    Length = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &Reserved, &Length, MEM_RELEASE);
}

static void
Test_WriteValid(void)
{
    static const char Source[] = "NtWriteVirtualMemory";
    char Target[sizeof(Source)] = { 0 };
    SIZE_T Written = 0;
    NTSTATUS Status;

    Status = NtWriteVirtualMemory(NtCurrentProcess(), Target, (PVOID)Source, sizeof(Source), &Written);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Written == sizeof(Source), "wrote %Iu bytes\n", Written);
    ok(!strcmp(Target, Source), "wrong data '%s'\n", Target);
}

START_TEST(NtWriteVirtualMemory)
{
    Test_WriteValid();
    /* Small writes use a pool buffer, large ones lock the pages with an MDL */
    Test_WriteBad((PVOID)0x10, 4);
    Test_WriteBad((PVOID)0x10, 4 * 64 * 1024);
    Test_WriteUncommitted(64);
    Test_WriteUncommitted(4 * 64 * 1024);
}
