/*
 * PROJECT:     ReactOS NT User Mode Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AddressSanitizer runtime for user mode: the shadow memory, the
 *              checks called by instrumented code, the reports
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Modules built with -fsanitize=kernel-address (see ASAN_MODULES in
 * sdk/cmake) call __asan_loadN_noabort/__asan_storeN_noabort(address)
 * before each memory access. When ASan is on for the program (AsanFlags in
 * its Image File Execution Options), they look at the shadow byte of the
 * address and report bad accesses; the heap gives each block red zones and
 * keeps freed blocks poisoned (sdk/lib/rtl/asan.c).
 *
 * The shadow area (1/8 of the user address space) is only reserved: its pages
 * are committed, zeroed (meaning "may be used"), when first touched, by the
 * vectored exception handler below.
 */

#include <ntdll.h>
#include <reactos/rtlasan.h>

#define NDEBUG
#include <debug.h>

static SIZE_T RtlpAsanShadowSize;

/* SHADOW MEMORY ************************************************************/

static LONG NTAPI
RtlpAsanShadowFault(PEXCEPTION_POINTERS ExceptionInfo)
{
    PEXCEPTION_RECORD Record = ExceptionInfo->ExceptionRecord;
    ULONG_PTR Address;
    PVOID Base;
    SIZE_T Size = 0x10000;

    if (Record->ExceptionCode != STATUS_ACCESS_VIOLATION || Record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;

    Address = Record->ExceptionInformation[1];
    if (Address < RtlpAsanShadowBase || Address >= RtlpAsanShadowBase + RtlpAsanShadowSize)
        return EXCEPTION_CONTINUE_SEARCH;

    /* Commit (zeroed: "may be used") the 64 KB around it */
    Base = (PVOID)(Address & ~(ULONG_PTR)(Size - 1));
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_COMMIT, PAGE_READWRITE)))
        return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* REPORTS ******************************************************************/

static VOID
RtlpAsanPrintAddress(PCSTR Prefix, PVOID Address)
{
    PLDR_DATA_TABLE_ENTRY Entry;

    if (NT_SUCCESS(LdrFindEntryForAddress(Address, &Entry)))
        DbgPrint("%s%p %wZ+0x%Ix\n", Prefix, Address, &Entry->BaseDllName, (ULONG_PTR)Address - (ULONG_PTR)Entry->DllBase);
    else
        DbgPrint("%s%p\n", Prefix, Address);
}

static VOID
RtlpAsanPrintStack(PVOID *Stack, ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count && Stack[i]; i++)
        RtlpAsanPrintAddress("      ", Stack[i]);
}

/* The heap block that Address belongs to (or is next to), from the shadow */
static PASAN_BLOCK_HEADER
RtlpAsanFindBlock(ULONG_PTR Address)
{
    ULONG_PTR Granule = Address & ~(ULONG_PTR)(ASAN_GRANULE - 1);
    ULONG Steps;

    if (*RtlpAsanShadow(Granule) == ASAN_HEAP_LEFT_REDZONE)
    {
        /* before a block: its data starts after the left red zone */
        for (Steps = 0; Steps < ASAN_LEFT_REDZONE / ASAN_GRANULE + 1 &&
             *RtlpAsanShadow(Granule) == ASAN_HEAP_LEFT_REDZONE; Steps++)
        {
            Granule += ASAN_GRANULE;
        }
        return RtlpAsanGetBlock((PVOID)Granule);
    }

    /* in a block or after it: back to the end of its left red zone */
    for (Steps = 0; Steps < 0x100000 && Granule >= ASAN_GRANULE; Steps++)
    {
        if (*RtlpAsanShadow(Granule - ASAN_GRANULE) == ASAN_HEAP_LEFT_REDZONE)
            return RtlpAsanGetBlock((PVOID)Granule);
        Granule -= ASAN_GRANULE;
    }
    return NULL;
}

static VOID NTAPI
RtlpAsanDoReport(ULONG_PTR Address, SIZE_T Size, BOOLEAN IsWrite, PVOID Pc, PCSTR What)
{
    static LONG InReport;
    static PVOID Reported[256];
    PASAN_BLOCK_HEADER Header;
    PVOID Stack[16];
    ULONG Count, i;
    CHAR Shadow;

    /* One report for each place in the code */
    for (i = 0; i < ARRAYSIZE(Reported) && Reported[i]; i++)
    {
        if (Reported[i] == Pc)
            return;
    }
    if (InterlockedExchange(&InReport, 1))
        return;
    if (i < ARRAYSIZE(Reported))
        Reported[i] = Pc;

    Shadow = *RtlpAsanShadow(Address);
    if (!What)
    {
        What = Shadow == ASAN_HEAP_LEFT_REDZONE ? "heap-buffer-underflow" :
               Shadow == ASAN_HEAP_FREED ? "heap-use-after-free" :
               (Shadow == ASAN_HEAP_RIGHT_REDZONE || (Shadow > 0 && Shadow < ASAN_GRANULE)) ? "heap-buffer-overflow" :
               "bad-access";
    }

    DbgPrint("=================================================================\n");
    DbgPrint("ASAN: %s: %s of %Iu bytes at %p (process %p, thread %p)\n", What,
             IsWrite ? "write" : "read", Size, (PVOID)Address,
             NtCurrentTeb()->ClientId.UniqueProcess, NtCurrentTeb()->ClientId.UniqueThread);
    Count = RtlCaptureStackBackTrace(1, ARRAYSIZE(Stack), Stack, NULL);
    RtlpAsanPrintAddress("    at  ", Pc);
    RtlpAsanPrintStack(Stack, Count);

    Header = RtlpAsanFindBlock(Address);
    if (Header)
    {
        ULONG_PTR User = (ULONG_PTR)(Header + 1);
        LONG_PTR Offset = (LONG_PTR)(Address - User);

        DbgPrint("  The address is %Id bytes %s the %Iu-byte block at %p, allocated by:\n",
                 Offset < 0 ? -Offset : (Offset >= (LONG_PTR)Header->UserSize ? Offset - (LONG_PTR)Header->UserSize : Offset),
                 Offset < 0 ? "before" : (Offset >= (LONG_PTR)Header->UserSize ? "after" : "inside"),
                 Header->UserSize, (PVOID)User);
        RtlpAsanPrintStack(Header->AllocStack, ASAN_STACK_DEPTH);
        if (Header->State == ASAN_BLOCK_FREED)
        {
            DbgPrint("  and freed by:\n");
            RtlpAsanPrintStack(Header->FreeStack, ASAN_STACK_DEPTH);
        }
    }
    DbgPrint("=================================================================\n");

    InterlockedExchange(&InReport, 0);
    if (RtlpAsanFlags & ASAN_FLAG_BREAK)
        DbgBreakPoint();
}

/* THE CHECKS (called by instrumented code) *********************************/

static __inline BOOLEAN
RtlpAsanIsBad(ULONG_PTR Address, SIZE_T Size)
{
    ULONG_PTR Last = Address + Size - 1, Granule;

    if (!RtlpAsanEnabled || !Size || Last >= RtlpAsanShadowLimit || Last < Address)
        return FALSE;

    for (Granule = Address & ~(ULONG_PTR)(ASAN_GRANULE - 1); Granule <= Last; Granule += ASAN_GRANULE)
    {
        CHAR Shadow = *RtlpAsanShadow(Granule);

        if (Shadow == 0)
            continue;
        /* a red zone, freed memory... or a granule used only in part */
        if (Shadow < 0 || (LONG)(min(Last, Granule + ASAN_GRANULE - 1) - Granule) >= Shadow)
            return TRUE;
    }
    return FALSE;
}

#define ASAN_CHECK(Size, IsWrite, Name) \
    VOID __cdecl Name(ULONG_PTR Address) \
    { \
        if (RtlpAsanIsBad(Address, Size)) \
            RtlpAsanDoReport(Address, Size, IsWrite, _ReturnAddress(), NULL); \
    }

ASAN_CHECK(1, FALSE, __asan_load1_noabort)
ASAN_CHECK(2, FALSE, __asan_load2_noabort)
ASAN_CHECK(4, FALSE, __asan_load4_noabort)
ASAN_CHECK(8, FALSE, __asan_load8_noabort)
ASAN_CHECK(16, FALSE, __asan_load16_noabort)
ASAN_CHECK(1, TRUE, __asan_store1_noabort)
ASAN_CHECK(2, TRUE, __asan_store2_noabort)
ASAN_CHECK(4, TRUE, __asan_store4_noabort)
ASAN_CHECK(8, TRUE, __asan_store8_noabort)
ASAN_CHECK(16, TRUE, __asan_store16_noabort)

VOID __cdecl __asan_loadN_noabort(ULONG_PTR Address, SIZE_T Size)
{
    if (RtlpAsanIsBad(Address, Size))
        RtlpAsanDoReport(Address, Size, FALSE, _ReturnAddress(), NULL);
}

VOID __cdecl __asan_storeN_noabort(ULONG_PTR Address, SIZE_T Size)
{
    if (RtlpAsanIsBad(Address, Size))
        RtlpAsanDoReport(Address, Size, TRUE, _ReturnAddress(), NULL);
}

/* (before calls that do not return; nothing to do without stack checks) */
VOID __cdecl __asan_handle_no_return(VOID)
{
}

/* INITIALIZATION ***********************************************************/

/* At process start (LdrpInitializeProcess), once the process heap exists */
VOID
NTAPI
RtlpAsanInitialize(ULONG Flags)
{
    SYSTEM_BASIC_INFORMATION BasicInfo;
    ULONG_PTR Limit;
    PVOID Base = NULL;
    NTSTATUS Status;

    if (!(Flags & ASAN_FLAG_ENABLE) || RtlpAsanEnabled)
        return;

    /* All the user address space has a shadow */
    Status = NtQuerySystemInformation(SystemBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return;
    Limit = BasicInfo.MaximumUserModeAddress & ~(ULONG_PTR)(ASAN_GRANULE - 1);
#ifndef _WIN64
    /* (a 32-bit process of 64-bit ReactOS has its memory below 2 GB) */
    Limit = min(Limit, 0x80000000);
#endif

    RtlpAsanShadowSize = Limit >> ASAN_SHADOW_SCALE;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &RtlpAsanShadowSize, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ASAN: no room for the shadow memory (%Iu bytes, status 0x%lx)\n", RtlpAsanShadowSize, Status);
        return;
    }
    if (!RtlAddVectoredExceptionHandler(TRUE, RtlpAsanShadowFault))
        return;

    RtlpAsanShadowBase = (ULONG_PTR)Base;
    RtlpAsanShadowLimit = Limit;
    RtlpAsanFlags = Flags;
    RtlpAsanReport = RtlpAsanDoReport;
    RtlpAsanEnabled = TRUE;
    DbgPrint("ASAN: on (flags 0x%lx), shadow at %p\n", Flags, Base);
}
