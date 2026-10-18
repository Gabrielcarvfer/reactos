/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite: the memory leak sanitizer (KLSAN)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Only for kernels built with ENABLE_KLSAN and booted with /KLSAN (skipped
 * otherwise): blocks that nothing points to are reported, blocks that a
 * global variable or another block points to (also into their middle) are
 * not.
 */

#include <kmt_test.h>
#include <reactos/klsan.h>

#define TAG_KLSAN_TEST 'tslK'

typedef struct _KLSAN_TEST_BLOCK
{
    struct _KLSAN_TEST_BLOCK *Child;
    ULONG_PTR Filler[6];
} KLSAN_TEST_BLOCK, *PKLSAN_TEST_BLOCK;

static PKLSAN_CONTROL_ROUTINE pKlsanControl;

/* Global variables: roots of the scan */
static PKLSAN_TEST_BLOCK volatile KeptBlock;
static PUCHAR volatile KeptInterior;

/* The leaked blocks are only remembered inverted (not a pointer to them) */
static volatile ULONG_PTR LeakedNonPaged, LeakedPaged, LeakedBig, LeakedChain;

static
DECLSPEC_NOINLINE
ULONG_PTR
AllocateHidden(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T Size)
{
    PULONG_PTR Block = ExAllocatePoolWithTag(PoolType, Size, TAG_KLSAN_TEST);
    ULONG_PTR Hidden;

    if (!Block)
        return 0;
    /* (no pointers in it) */
    RtlFillMemory(Block, Size, 0x5A);
    Hidden = ~(ULONG_PTR)Block;
    Block = NULL;
    return Hidden;
}

/* Two leaked blocks: the first one points to the second one */
static
DECLSPEC_NOINLINE
ULONG_PTR
AllocateHiddenChain(VOID)
{
    PKLSAN_TEST_BLOCK First, Second;
    ULONG_PTR Hidden;

    First = ExAllocatePoolWithTag(PagedPool, sizeof(*First), TAG_KLSAN_TEST);
    if (!First)
        return 0;
    Second = ExAllocatePoolWithTag(PagedPool, sizeof(*Second), TAG_KLSAN_TEST);
    if (!Second)
    {
        ExFreePoolWithTag(First, TAG_KLSAN_TEST);
        return 0;
    }
    RtlZeroMemory(First, sizeof(*First));
    RtlZeroMemory(Second, sizeof(*Second));
    First->Child = Second;
    Hidden = ~(ULONG_PTR)First;
    return Hidden;
}

/* The scan reads this thread's stack too: old copies of the pointers in
 * the stack below the current frames must go */
static
DECLSPEC_NOINLINE
VOID
ClearStack(VOID)
{
    volatile UCHAR Buffer[4000];

    RtlSecureZeroMemory((PVOID)Buffer, sizeof(Buffer));
}

static
ULONG
QueryBlock(
    _In_ PVOID Address)
{
    KLSAN_CONTROL Control;
    ULONG State = MAXULONG, Length = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Control, sizeof(Control));
    Control.Command = KLSAN_CMD_QUERY;
    Control.Address = Address;
    Status = pKlsanControl(&Control, &State, sizeof(State), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Length, (ULONG)sizeof(State));
    return State;
}

static
VOID
Scan(VOID)
{
    KLSAN_CONTROL Control;
    PCHAR Report;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Control, sizeof(Control));
    Control.Command = KLSAN_CMD_SCAN;
    Control.MinAgeSeconds = MAXULONG;   /* (any age) */
    Control.MinScans = 1;
    Control.MaxStacks = 10;
    Report = ExAllocatePoolWithTag(PagedPool, 64 * 1024, TAG_KLSAN_TEST);
    if (skip(Report != NULL, "No memory\n"))
        return;
    Status = pKlsanControl(&Control, Report, 64 * 1024, &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Length > 1 && Length <= 64 * 1024, "Length = %lu\n", Length);
    if (Length > 1)
    {
        ok(Report[Length - 1] == ANSI_NULL, "Report not terminated\n");
        ok(strstr(Report, "unreferenced blocks") != NULL, "Report: %.200s\n", Report);
        /* our leaks have the allocation stack: this test's function */
        ok(strstr(Report, "kmtest_drv") != NULL, "No kmtest_drv frame in the report\n");
    }
    ExFreePoolWithTag(Report, TAG_KLSAN_TEST);
}

START_TEST(ExKlsan)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"KlsanControl");
    KLSAN_CONTROL Control;
    KLSAN_STATS Stats;
    PKLSAN_TEST_BLOCK Child;
    ULONG Length;
    NTSTATUS Status;

    pKlsanControl = (PKLSAN_CONTROL_ROUTINE)MmGetSystemRoutineAddress(&Name);
    if (skip(pKlsanControl != NULL, "Not a KLSAN kernel (ENABLE_KLSAN)\n"))
        return;

    RtlZeroMemory(&Control, sizeof(Control));
    Control.Command = KLSAN_CMD_STATS;
    Status = pKlsanControl(&Control, &Stats, sizeof(Stats), &Length);
    if (skip(Status != STATUS_NOT_SUPPORTED, "KLSAN is off (boot option /KLSAN)\n"))
        return;
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Stats.TrackedBlocks > 1000, "TrackedBlocks = %I64u\n", Stats.TrackedBlocks);

    /* Bad requests */
    Control.Command = 0x1234;
    Status = pKlsanControl(&Control, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Control.Command = KLSAN_CMD_STATS;
    Status = pKlsanControl(&Control, &Stats, sizeof(Stats) - 1, &Length);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    /* Referenced: by a global, by another block, by a pointer into the middle */
    KeptBlock = ExAllocatePoolWithTag(NonPagedPool, sizeof(*KeptBlock), TAG_KLSAN_TEST);
    Child = ExAllocatePoolWithTag(PagedPool, sizeof(*Child), TAG_KLSAN_TEST);
    KeptInterior = ExAllocatePoolWithTag(NonPagedPool, 200, TAG_KLSAN_TEST);
    if (skip(KeptBlock && Child && KeptInterior, "No memory\n"))
        return;
    RtlZeroMemory(KeptBlock, sizeof(*KeptBlock));
    RtlZeroMemory(Child, sizeof(*Child));
    KeptBlock->Child = Child;
    KeptInterior += 100;
    Child = NULL;

    /* Leaked: nothing points to them */
    LeakedNonPaged = AllocateHidden(NonPagedPool, 100);
    LeakedPaged = AllocateHidden(PagedPool, 300);
    LeakedBig = AllocateHidden(NonPagedPool, 3 * PAGE_SIZE + 100);
    LeakedChain = AllocateHiddenChain();
    ok(LeakedNonPaged && LeakedPaged && LeakedBig && LeakedChain, "No memory\n");
    ClearStack();

    /* Not scanned yet */
    ok_eq_ulong(QueryBlock(KeptBlock), (ULONG)KLSAN_BLOCK_NEW);

    Scan();

    ok_eq_ulong(QueryBlock(KeptBlock), (ULONG)KLSAN_BLOCK_REFERENCED);
    ok_eq_ulong(QueryBlock(KeptBlock->Child), (ULONG)KLSAN_BLOCK_REFERENCED);
    ok_eq_ulong(QueryBlock(KeptInterior - 100), (ULONG)KLSAN_BLOCK_REFERENCED);
    /* (a pointer into a block finds it too) */
    ok_eq_ulong(QueryBlock(KeptInterior), (ULONG)KLSAN_BLOCK_REFERENCED);
    if (LeakedNonPaged)
        ok_eq_ulong(QueryBlock((PVOID)~LeakedNonPaged), (ULONG)KLSAN_BLOCK_UNREFERENCED);
    if (LeakedPaged)
        ok_eq_ulong(QueryBlock((PVOID)~LeakedPaged), (ULONG)KLSAN_BLOCK_UNREFERENCED);
    if (LeakedBig)
    {
        ok_eq_ulong(QueryBlock((PVOID)~LeakedBig), (ULONG)KLSAN_BLOCK_UNREFERENCED);
        /* (the last page of it) */
        ok_eq_ulong(QueryBlock((PUCHAR)~LeakedBig + 3 * PAGE_SIZE + 50), (ULONG)KLSAN_BLOCK_UNREFERENCED);
    }
    if (LeakedChain)
    {
        /* the first one is the leak, the second one is only referenced by it:
         * both are unreachable */
        ok_eq_ulong(QueryBlock((PVOID)~LeakedChain), (ULONG)KLSAN_BLOCK_UNREFERENCED);
        ok_eq_ulong(QueryBlock(((PKLSAN_TEST_BLOCK)~LeakedChain)->Child), (ULONG)KLSAN_BLOCK_UNREFERENCED);
    }
    /* Not a pool block */
    ok_eq_ulong(QueryBlock((PVOID)&KeptBlock), (ULONG)KLSAN_BLOCK_UNTRACKED);

    /* Clean up: freed blocks are forgotten */
    if (LeakedChain)
    {
        ExFreePoolWithTag(((PKLSAN_TEST_BLOCK)~LeakedChain)->Child, TAG_KLSAN_TEST);
        ExFreePoolWithTag((PVOID)~LeakedChain, TAG_KLSAN_TEST);
    }
    if (LeakedBig)
        ExFreePoolWithTag((PVOID)~LeakedBig, TAG_KLSAN_TEST);
    if (LeakedPaged)
        ExFreePoolWithTag((PVOID)~LeakedPaged, TAG_KLSAN_TEST);
    if (LeakedNonPaged)
    {
        ExFreePoolWithTag((PVOID)~LeakedNonPaged, TAG_KLSAN_TEST);
        ok_eq_ulong(QueryBlock((PVOID)~LeakedNonPaged), (ULONG)KLSAN_BLOCK_UNTRACKED);
    }
    ExFreePoolWithTag(KeptBlock->Child, TAG_KLSAN_TEST);
    ExFreePoolWithTag(KeptBlock, TAG_KLSAN_TEST);
    ExFreePoolWithTag(KeptInterior - 100, TAG_KLSAN_TEST);
    ok_eq_ulong(QueryBlock(KeptBlock), (ULONG)KLSAN_BLOCK_UNTRACKED);
    KeptBlock = NULL;
    KeptInterior = NULL;
    LeakedNonPaged = LeakedPaged = LeakedBig = LeakedChain = 0;
}
