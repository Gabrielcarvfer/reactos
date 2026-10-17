/*
 * PROJECT:     ReactOS NT User Mode Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NtAllocateVirtualMemoryEx and NtMapViewOfSectionEx (Windows 10 1803+)
 *
 * The kernel has no "Ex" services yet, so these are done in user mode on top
 * of NtAllocateVirtualMemory and NtMapViewOfSection. The one extended
 * parameter programs use, MemExtendedParameterAddressRequirements (put the
 * block between two addresses, with an alignment), is handled by looking for
 * a free range with NtQueryVirtualMemory and asking for it; another thread
 * may take it first, then the next free range is tried.
 * Users: Cygwin/MSYS2 (Git for Windows' bash) allocate thread stacks and mmap
 * regions that way when Windows reports build 17134 or later.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef NTSTATUS (*PRTLP_TRY_ADDRESS)(PVOID Context, ULONG_PTR Address);

static
NTSTATUS
RtlpGetAddressRequirements(
    _In_reads_opt_(Count) PMEM_EXTENDED_PARAMETER Parameters,
    _In_ ULONG Count,
    _Out_ PMEM_ADDRESS_REQUIREMENTS Requirements,
    _Out_ PBOOLEAN HasRequirements)
{
    ULONG i;

    RtlZeroMemory(Requirements, sizeof(*Requirements));
    *HasRequirements = FALSE;
    if (Count && !Parameters)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < Count; i++)
    {
        switch (Parameters[i].Type)
        {
            case MemExtendedParameterAddressRequirements:
                if (!Parameters[i].Pointer || *HasRequirements)
                    return STATUS_INVALID_PARAMETER;
                *Requirements = *(PMEM_ADDRESS_REQUIREMENTS)Parameters[i].Pointer;
                *HasRequirements = TRUE;
                break;

            case MemExtendedParameterNumaNode:
                /* One node: any node is this one */
                break;

            default:
                DPRINT1("Extended memory parameter %u not supported\n", (ULONG)Parameters[i].Type);
                return STATUS_NOT_SUPPORTED;
        }
    }
    return STATUS_SUCCESS;
}

/* Finds a free range of Size bytes inside the requirements and calls Try with
 * its address, until Try gets it (another thread may get there first) */
static
NTSTATUS
RtlpFindFreeRange(
    _In_ HANDLE ProcessHandle,
    _In_ SIZE_T Size,
    _In_ PMEM_ADDRESS_REQUIREMENTS Requirements,
    _In_ BOOLEAN TopDown,
    _In_ PRTLP_TRY_ADDRESS Try,
    _In_ PVOID Context)
{
    SYSTEM_BASIC_INFORMATION BasicInfo;
    MEMORY_BASIC_INFORMATION Info;
    ULONG_PTR Low, High, Align, Address, Next;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(SystemBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Low = (ULONG_PTR)Requirements->LowestStartingAddress;
    High = (ULONG_PTR)Requirements->HighestEndingAddress;
    Align = Requirements->Alignment;
    if (!Low)
        Low = BasicInfo.MinimumUserModeAddress;
    if (!High || High > BasicInfo.MaximumUserModeAddress)
        High = BasicInfo.MaximumUserModeAddress;
    if (!Align)
        Align = BasicInfo.AllocationGranularity;

    /* Windows wants a power of two, at least the allocation granularity, and
       the low address on that granularity, the high one just before it */
    if ((Align & (Align - 1)) || Align < BasicInfo.AllocationGranularity ||
        (Low & (BasicInfo.AllocationGranularity - 1)) ||
        ((High + 1) & (BasicInfo.PageSize - 1)) || Low >= High || !Size)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Size > High - Low + 1)
        return STATUS_NO_MEMORY;

    if (TopDown)
    {
        Address = ALIGN_DOWN_BY(High + 1 - Size, Align);
        while (Address >= Low)
        {
            Status = NtQueryVirtualMemory(ProcessHandle, (PVOID)Address, MemoryBasicInformation,
                                          &Info, sizeof(Info), NULL);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Info.State == MEM_FREE && Info.RegionSize >= Size)
            {
                Status = Try(Context, Address);
                if (Status != STATUS_CONFLICTING_ADDRESSES)
                    return Status;
                Next = Address - Align;
            }
            else if (Info.State == MEM_FREE)
            {
                /* Too small: below the block that ends it */
                Next = (ULONG_PTR)Info.BaseAddress + Info.RegionSize;
                Next = (Next < Size) ? 0 : ALIGN_DOWN_BY(Next - Size, Align);
            }
            else
            {
                /* In use: below its allocation */
                Next = (ULONG_PTR)Info.AllocationBase;
                Next = (Next < Size) ? 0 : ALIGN_DOWN_BY(Next - Size, Align);
            }
            if (Next >= Address)
                Next = Address - Align;
            if (Next > Address) /* wrapped around */
                break;
            Address = Next;
        }
    }
    else
    {
        Address = ALIGN_UP_BY(Low, Align);
        while (Address >= Low && Address + Size - 1 <= High && Address + Size > Address)
        {
            Status = NtQueryVirtualMemory(ProcessHandle, (PVOID)Address, MemoryBasicInformation,
                                          &Info, sizeof(Info), NULL);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Info.State == MEM_FREE && Info.RegionSize >= Size)
            {
                Status = Try(Context, Address);
                if (Status != STATUS_CONFLICTING_ADDRESSES)
                    return Status;
                Next = Address + Align;
            }
            else
            {
                /* Above this region */
                Next = ALIGN_UP_BY((ULONG_PTR)Info.BaseAddress + Info.RegionSize, Align);
            }
            if (Next <= Address)
                break;
            Address = Next;
        }
    }
    return STATUS_NO_MEMORY;
}

typedef struct _RTLP_ALLOC_CONTEXT
{
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    PSIZE_T RegionSize;
    ULONG AllocationType;
    ULONG Protect;
} RTLP_ALLOC_CONTEXT, *PRTLP_ALLOC_CONTEXT;

static
NTSTATUS
RtlpTryAllocate(PVOID Context, ULONG_PTR Address)
{
    PRTLP_ALLOC_CONTEXT Alloc = Context;
    PVOID Base = (PVOID)Address;
    SIZE_T Size = *Alloc->RegionSize;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemory(Alloc->ProcessHandle, &Base, 0, &Size,
                                     Alloc->AllocationType, Alloc->Protect);
    if (NT_SUCCESS(Status))
    {
        *Alloc->BaseAddress = Base;
        *Alloc->RegionSize = Size;
    }
    return Status;
}

NTSTATUS
NTAPI
NtAllocateVirtualMemoryEx(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    MEM_ADDRESS_REQUIREMENTS Requirements;
    RTLP_ALLOC_CONTEXT Alloc;
    BOOLEAN HasRequirements;
    SIZE_T Size;
    NTSTATUS Status;

    Status = RtlpGetAddressRequirements(ExtendedParameters, ParameterCount, &Requirements, &HasRequirements);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!HasRequirements)
        return NtAllocateVirtualMemory(ProcessHandle, BaseAddress, 0, RegionSize, AllocationType, PageProtection);

    /* With requirements, the system picks the address of a new reservation */
    if (*BaseAddress || !(AllocationType & MEM_RESERVE))
        return STATUS_INVALID_PARAMETER;

    Size = ALIGN_UP_BY(*RegionSize, PAGE_SIZE);
    Alloc.ProcessHandle = ProcessHandle;
    Alloc.BaseAddress = BaseAddress;
    Alloc.RegionSize = &Size;
    Alloc.AllocationType = AllocationType & ~MEM_TOP_DOWN;
    Alloc.Protect = PageProtection;
    Status = RtlpFindFreeRange(ProcessHandle, Size, &Requirements, (AllocationType & MEM_TOP_DOWN) != 0,
                               RtlpTryAllocate, &Alloc);
    if (NT_SUCCESS(Status))
        *RegionSize = Size;
    return Status;
}

typedef struct _RTLP_MAP_CONTEXT
{
    HANDLE SectionHandle;
    HANDLE ProcessHandle;
    PVOID *BaseAddress;
    PLARGE_INTEGER SectionOffset;
    PSIZE_T ViewSize;
    ULONG AllocationType;
    ULONG Protect;
} RTLP_MAP_CONTEXT, *PRTLP_MAP_CONTEXT;

static
NTSTATUS
RtlpTryMap(PVOID Context, ULONG_PTR Address)
{
    PRTLP_MAP_CONTEXT Map = Context;
    LARGE_INTEGER Offset, *POffset = NULL;
    PVOID Base = (PVOID)Address;
    SIZE_T Size = *Map->ViewSize;
    NTSTATUS Status;

    if (Map->SectionOffset)
    {
        Offset = *Map->SectionOffset;
        POffset = &Offset;
    }
    Status = NtMapViewOfSection(Map->SectionHandle, Map->ProcessHandle, &Base, 0, 0, POffset, &Size,
                                ViewUnmap, Map->AllocationType, Map->Protect);
    if (NT_SUCCESS(Status))
    {
        *Map->BaseAddress = Base;
        *Map->ViewSize = Size;
        if (Map->SectionOffset)
            *Map->SectionOffset = Offset;
    }
    return Status;
}

NTSTATUS
NTAPI
NtMapViewOfSectionEx(
    _In_ HANDLE SectionHandle,
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_opt_ PLARGE_INTEGER SectionOffset,
    _Inout_ PSIZE_T ViewSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    MEM_ADDRESS_REQUIREMENTS Requirements;
    SECTION_BASIC_INFORMATION SectionInfo;
    RTLP_MAP_CONTEXT Map;
    BOOLEAN HasRequirements;
    SIZE_T Size;
    NTSTATUS Status;

    Status = RtlpGetAddressRequirements(ExtendedParameters, ParameterCount, &Requirements, &HasRequirements);
    if (!NT_SUCCESS(Status))
        return Status;
    /* (Views made this way are not inherited by child processes) */
    if (!HasRequirements)
    {
        return NtMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, 0, 0, SectionOffset,
                                  ViewSize, ViewUnmap, AllocationType, PageProtection);
    }
    if (*BaseAddress)
        return STATUS_INVALID_PARAMETER;

    /* The size of the range to look for: the view, or the section from the offset */
    Size = *ViewSize;
    if (!Size)
    {
        Status = NtQuerySection(SectionHandle, SectionBasicInformation, &SectionInfo, sizeof(SectionInfo), NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        Size = (SIZE_T)(SectionInfo.Size.QuadPart - (SectionOffset ? SectionOffset->QuadPart : 0));
    }
    Size = ALIGN_UP_BY(Size, PAGE_SIZE);

    Map.SectionHandle = SectionHandle;
    Map.ProcessHandle = ProcessHandle;
    Map.BaseAddress = BaseAddress;
    Map.SectionOffset = SectionOffset;
    Map.ViewSize = ViewSize;
    Map.AllocationType = AllocationType & ~MEM_TOP_DOWN;
    Map.Protect = PageProtection;
    return RtlpFindFreeRange(ProcessHandle, Size, &Requirements, (AllocationType & MEM_TOP_DOWN) != 0,
                             RtlpTryMap, &Map);
}
