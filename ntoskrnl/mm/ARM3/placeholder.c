/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Placeholders and the extended memory system calls of Windows 10
 *              1803: NtAllocateVirtualMemoryEx, NtMapViewOfSectionEx and
 *              NtUnmapViewOfSectionEx
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * A placeholder is a reserved range with no access (a private VAD marked with
 * MI_SET_PLACEHOLDER_VAD). It can be split (NtFreeVirtualMemory with
 * MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), joined again
 * (MEM_COALESCE_PLACEHOLDERS), and a part of the exact size replaced by a
 * view (NtMapViewOfSectionEx, MEM_REPLACE_PLACEHOLDER) or by private memory
 * (NtAllocateVirtualMemoryEx, MEM_REPLACE_PLACEHOLDER), without anything else
 * taking the range in between. Unmapping a view with MEM_PRESERVE_PLACEHOLDER
 * gives the placeholder back. The .NET runtime (5+) loads single-file bundles
 * this way, Cygwin/MSYS2 use the address requirements.
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* (Another thread may take a free range before we do: try that many) */
#define MI_FREE_RANGE_ATTEMPTS 64

typedef struct _MI_EXTENDED_PARAMETERS
{
    BOOLEAN HasRequirements;
    MEM_ADDRESS_REQUIREMENTS Requirements;
} MI_EXTENDED_PARAMETERS, *PMI_EXTENDED_PARAMETERS;

/* PLACEHOLDER VADS ***********************************************************/

PMMVAD
NTAPI
MiAllocatePlaceholderVad(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR StartingVpn,
    _In_ ULONG_PTR EndingVpn)
{
    PMMVAD Vad;

    if (!NT_SUCCESS(PsChargeProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG))))
        return NULL;

    Vad = ExAllocatePoolZero(NonPagedPool, sizeof(MMVAD_LONG), 'SdaV');
    if (!Vad)
    {
        PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));
        return NULL;
    }

    Vad->StartingVpn = StartingVpn;
    Vad->EndingVpn = EndingVpn;
    Vad->u.VadFlags.PrivateMemory = 1;
    Vad->u.VadFlags.Protection = MM_NOACCESS;
    Vad->ControlArea = NULL;
    MI_SET_PLACEHOLDER_VAD(Vad, TRUE);
    return Vad;
}

VOID
NTAPI
MiFreePlaceholderVad(
    _In_ PEPROCESS Process,
    _In_ PMMVAD Vad)
{
    PsReturnProcessNonPagedPoolQuota(Process, sizeof(MMVAD_LONG));
    ExFreePoolWithTag(Vad, 'SdaV');
}

/*
 * Takes the placeholder that covers exactly [StartingAddress, EndingAddress]
 * out of the VAD tree, so that a view can take its place. The address space
 * must be locked and the process attached. Returns NULL if there is none.
 */
PMMVAD
NTAPI
MiRemovePlaceholderVad(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR StartingAddress,
    _In_ ULONG_PTR EndingAddress)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PMMVAD Vad;

    ASSERT(Process == PsGetCurrentProcess());

    Vad = MiLocateVad(&Process->VadRoot, (PVOID)StartingAddress);
    if (!Vad || !MI_IS_PLACEHOLDER_VAD(Vad) ||
        (Vad->StartingVpn != (StartingAddress >> PAGE_SHIFT)) ||
        (Vad->EndingVpn != (EndingAddress >> PAGE_SHIFT)))
    {
        return NULL;
    }

    MiLockProcessWorkingSetUnsafe(Process, CurrentThread);
    MiRemoveNode((PMMADDRESS_NODE)Vad, &Process->VadRoot);
    MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);
    Process->VirtualSize -= (Vad->EndingVpn - Vad->StartingVpn + 1) << PAGE_SHIFT;
    return Vad;
}

/* Puts back a placeholder that MiRemovePlaceholderVad took out (the mapping failed) */
VOID
NTAPI
MiRestorePlaceholderVad(
    _In_ PEPROCESS Process,
    _In_ PMMVAD Vad)
{
    PETHREAD CurrentThread = PsGetCurrentThread();

    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, CurrentThread);
    MiInsertVad(Vad, &Process->VadRoot);
    MiUnlockProcessWorkingSetUnsafe(Process, CurrentThread);
    Process->VirtualSize += (Vad->EndingVpn - Vad->StartingVpn + 1) << PAGE_SHIFT;
}

/* EXTENDED PARAMETERS ********************************************************/

static
NTSTATUS
MiCaptureExtendedParameters(
    _In_reads_opt_(Count) PMEM_EXTENDED_PARAMETER UserParameters,
    _In_ ULONG Count,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PMI_EXTENDED_PARAMETERS Parameters)
{
    MEM_EXTENDED_PARAMETER Captured[MemExtendedParameterMax];
    PMEM_ADDRESS_REQUIREMENTS Requirements;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    RtlZeroMemory(Parameters, sizeof(*Parameters));
    if (!Count)
        return STATUS_SUCCESS;
    if (!UserParameters || (Count > MemExtendedParameterMax))
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(UserParameters, Count * sizeof(MEM_EXTENDED_PARAMETER), sizeof(ULONG));
        RtlCopyMemory(Captured, UserParameters, Count * sizeof(MEM_EXTENDED_PARAMETER));

        for (i = 0; i < Count; i++)
        {
            switch (Captured[i].Type)
            {
                case MemExtendedParameterAddressRequirements:
                    Requirements = Captured[i].Pointer;
                    if (!Requirements || Parameters->HasRequirements)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        _SEH2_LEAVE;
                    }
                    if (PreviousMode != KernelMode)
                        ProbeForRead(Requirements, sizeof(*Requirements), sizeof(ULONG));
                    Parameters->Requirements = *Requirements;
                    Parameters->HasRequirements = TRUE;
                    break;

                case MemExtendedParameterNumaNode:
                    /* One node: any node is this one */
                    break;

                case MemExtendedParameterAttributeFlags:
                    if (Captured[i].ULong64)
                    {
                        DPRINT1("Memory attribute flags %I64x not supported\n", Captured[i].ULong64);
                        Status = STATUS_NOT_SUPPORTED;
                        _SEH2_LEAVE;
                    }
                    break;

                case MemExtendedParameterInvalidType:
                    Status = STATUS_INVALID_PARAMETER;
                    _SEH2_LEAVE;

                default:
                    DPRINT1("Extended memory parameter %u not supported\n", (ULONG)Captured[i].Type);
                    Status = STATUS_NOT_SUPPORTED;
                    _SEH2_LEAVE;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* The range MEM_ADDRESS_REQUIREMENTS allow, checked as Windows does */
static
NTSTATUS
MiCheckAddressRequirements(
    _In_ PMEM_ADDRESS_REQUIREMENTS Requirements,
    _Out_ PULONG_PTR Lowest,
    _Out_ PULONG_PTR Highest,
    _Out_ PULONG_PTR Alignment)
{
    ULONG_PTR Low = (ULONG_PTR)Requirements->LowestStartingAddress;
    ULONG_PTR High = (ULONG_PTR)Requirements->HighestEndingAddress;
    ULONG_PTR Align = Requirements->Alignment;

    if (!Low)
        Low = (ULONG_PTR)MM_LOWEST_USER_ADDRESS;
    if (!High || (High > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS))
        High = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
    if (!Align)
        Align = MM_VIRTMEM_GRANULARITY;

    if ((Align & (Align - 1)) || (Align < MM_VIRTMEM_GRANULARITY) ||
        (Low & (MM_VIRTMEM_GRANULARITY - 1)) ||
        ((High + 1) & (PAGE_SIZE - 1)) || (Low >= High))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Lowest = Low;
    *Highest = High;
    *Alignment = Align;
    return STATUS_SUCCESS;
}

/* A free range of Size bytes in [Lowest, Highest] (the caller then takes it) */
static
NTSTATUS
MiFindFreeRangeInRange(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Size,
    _In_ ULONG_PTR Lowest,
    _In_ ULONG_PTR Highest,
    _In_ ULONG_PTR Alignment,
    _In_ BOOLEAN TopDown,
    _Out_ PULONG_PTR Base)
{
    PMMADDRESS_NODE Parent;
    TABLE_SEARCH_RESULT Result;
    ULONG_PTR Start = 0;
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;
    NTSTATUS Status;

    if (Size > Highest - Lowest + 1)
        return STATUS_NO_MEMORY;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    KeAcquireGuardedMutex(&Process->AddressCreationLock);
    if (Process->VmDeleted)
    {
        Status = STATUS_PROCESS_IS_TERMINATING;
    }
    else
    {
        if (TopDown)
        {
            Result = MiFindEmptyAddressRangeDownTree(Size, Highest, Alignment,
                                                     &Process->VadRoot, &Start, &Parent);
            Status = ((Result != TableFoundNode) && (Start >= Lowest)) ?
                     STATUS_SUCCESS : STATUS_NO_MEMORY;
        }
        else
        {
            Result = MiFindEmptyAddressRangeInTreeEx(Size, Alignment, Lowest,
                                                     &Process->VadRoot, &Parent, &Start);
            Status = ((Result != TableFoundNode) && (Start + Size - 1 <= Highest)) ?
                     STATUS_SUCCESS : STATUS_NO_MEMORY;
        }
    }
    KeReleaseGuardedMutex(&Process->AddressCreationLock);

    if (Attached) KeUnstackDetachProcess(&ApcState);
    *Base = Start;
    return Status;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtAllocateVirtualMemoryEx(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ExtendedParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ExtendedParameterCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MI_EXTENDED_PARAMETERS Parameters;
    ULONG_PTR Lowest, Highest, Alignment, Candidate;
    PVOID Base;
    SIZE_T Size;
    PEPROCESS Process;
    NTSTATUS Status;
    ULONG Attempt;
    PAGED_CODE();

    Status = MiCaptureExtendedParameters(ExtendedParameters, ExtendedParameterCount,
                                         PreviousMode, &Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Parameters.HasRequirements)
    {
        return MiAllocateVirtualMemory(ProcessHandle, BaseAddress, 0, RegionSize,
                                       AllocationType, PageProtection, TRUE);
    }

    /* With requirements, the system picks the address of a new reservation */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWritePointer(BaseAddress);
            ProbeForWriteSize_t(RegionSize);
        }
        Base = *BaseAddress;
        Size = *RegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Base || !Size || !(AllocationType & MEM_RESERVE) ||
        (AllocationType & MEM_REPLACE_PLACEHOLDER))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Size = ROUND_TO_PAGES(Size);

    Status = MiCheckAddressRequirements(&Parameters.Requirements, &Lowest, &Highest, &Alignment);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType,
                                       PreviousMode, (PVOID*)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Attempt = 0; Attempt < MI_FREE_RANGE_ATTEMPTS; Attempt++)
    {
        Status = MiFindFreeRangeInRange(Process, Size, Lowest, Highest, Alignment,
                                        (AllocationType & MEM_TOP_DOWN) != 0, &Candidate);
        if (!NT_SUCCESS(Status))
            break;

        _SEH2_TRY
        {
            *BaseAddress = (PVOID)Candidate;
            *RegionSize = Size;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            break;

        Status = MiAllocateVirtualMemory(ProcessHandle, BaseAddress, 0, RegionSize,
                                         AllocationType & ~MEM_TOP_DOWN, PageProtection, TRUE);
        if (Status != STATUS_CONFLICTING_ADDRESSES)
            break;
    }

    ObDereferenceObject(Process);

    if (!NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            *BaseAddress = NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
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
    _Inout_updates_opt_(ExtendedParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ExtendedParameterCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MI_EXTENDED_PARAMETERS Parameters;
    ULONG_PTR Lowest, Highest, Alignment, Candidate;
    LARGE_INTEGER Offset;
    PSECTION Section;
    PEPROCESS Process;
    PVOID Base;
    SIZE_T Size;
    NTSTATUS Status;
    ULONG Attempt;
    PAGED_CODE();

    Status = MiCaptureExtendedParameters(ExtendedParameters, ExtendedParameterCount,
                                         PreviousMode, &Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    /* (Views made this way are not inherited by child processes) */
    if (!Parameters.HasRequirements)
    {
        return MiMapViewOfSectionCommon(SectionHandle, ProcessHandle, BaseAddress, 0, 0,
                                        SectionOffset, ViewSize, ViewUnmap,
                                        AllocationType, PageProtection, TRUE);
    }

    Offset.QuadPart = 0;
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWritePointer(BaseAddress);
            ProbeForWriteSize_t(ViewSize);
            if (SectionOffset) ProbeForWriteLargeInteger(SectionOffset);
        }
        Base = *BaseAddress;
        Size = *ViewSize;
        if (SectionOffset) Offset = *SectionOffset;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Base || (AllocationType & MEM_REPLACE_PLACEHOLDER))
        return STATUS_INVALID_PARAMETER;

    Status = MiCheckAddressRequirements(&Parameters.Requirements, &Lowest, &Highest, &Alignment);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The size of the range to look for: the view, or the section from the offset */
    if (!Size)
    {
        Status = ObReferenceObjectByHandle(SectionHandle, SECTION_QUERY, MmSectionObjectType,
                                           PreviousMode, (PVOID*)&Section, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Offset.QuadPart < Section->SizeOfSection.QuadPart)
            Size = (SIZE_T)(Section->SizeOfSection.QuadPart - Offset.QuadPart);
        ObDereferenceObject(Section);
        if (!Size)
            return STATUS_INVALID_VIEW_SIZE;
    }
    /* (A view starts on 64 KB of its section) */
    Size = ROUND_TO_PAGES(Size + (Offset.LowPart & (MM_VIRTMEM_GRANULARITY - 1)));

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType,
                                       PreviousMode, (PVOID*)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Attempt = 0; Attempt < MI_FREE_RANGE_ATTEMPTS; Attempt++)
    {
        Status = MiFindFreeRangeInRange(Process, Size, Lowest, Highest, Alignment,
                                        (AllocationType & MEM_TOP_DOWN) != 0, &Candidate);
        if (!NT_SUCCESS(Status))
            break;

        _SEH2_TRY
        {
            *BaseAddress = (PVOID)Candidate;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            break;

        Status = MiMapViewOfSectionCommon(SectionHandle, ProcessHandle, BaseAddress, 0, 0,
                                          SectionOffset, ViewSize, ViewUnmap,
                                          AllocationType & ~MEM_TOP_DOWN, PageProtection, TRUE);
        if (Status != STATUS_CONFLICTING_ADDRESSES)
            break;
    }

    ObDereferenceObject(Process);

    if (!NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            *BaseAddress = NULL;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    return Status;
}

NTSTATUS
NTAPI
NtUnmapViewOfSectionEx(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    PMMVAD Vad, Placeholder = NULL;
    BOOLEAN Attached = FALSE;
    KAPC_STATE ApcState;
    NTSTATUS Status;
    PAGED_CODE();

    if (Flags & ~(MEM_UNMAP_WITH_TRANSIENT_BOOST | MEM_PRESERVE_PLACEHOLDER))
        return STATUS_INVALID_PARAMETER_3;

    if (!(Flags & MEM_PRESERVE_PLACEHOLDER))
        return NtUnmapViewOfSection(ProcessHandle, BaseAddress);

    if ((PreviousMode == UserMode) && (BaseAddress > MM_HIGHEST_USER_ADDRESS))
        return STATUS_NOT_MAPPED_VIEW;

    Status = ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType,
                                       PreviousMode, (PVOID*)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    /* Unmap the view and put a placeholder in its place, under one lock */
    MmLockAddressSpace(&Process->Vm);
    if (Process->VmDeleted)
    {
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto Quit;
    }

    Vad = MiLocateVad(&Process->VadRoot, BaseAddress);
    if (!Vad || Vad->u.VadFlags.PrivateMemory || (Vad->u.VadFlags.VadType == VadImageMap))
    {
        Status = STATUS_NOT_MAPPED_VIEW;
        goto Quit;
    }

    Placeholder = MiAllocatePlaceholderVad(Process, Vad->StartingVpn, Vad->EndingVpn);
    if (!Placeholder)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Quit;
    }

    Status = MiUnmapViewOfSection(Process, BaseAddress, 1);
    if (NT_SUCCESS(Status))
    {
        MiRestorePlaceholderVad(Process, Placeholder);
        Placeholder = NULL;
    }

Quit:
    MmUnlockAddressSpace(&Process->Vm);
    if (Placeholder) MiFreePlaceholderVad(Process, Placeholder);
    if (Attached) KeUnstackDetachProcess(&ApcState);
    ObDereferenceObject(Process);
    return Status;
}

/* EOF */
