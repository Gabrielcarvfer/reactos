/*
 * PROJECT:     ReactOS Kernel Base
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     VirtualAlloc2 and MapViewOfFile3 (only in KernelBase on Windows)
 */

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"

/* (NtAllocateVirtualMemoryEx and NtMapViewOfSectionEx come from winternl.h) */
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif

PVOID
WINAPI
VirtualAlloc2(
    _In_opt_ HANDLE Process,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    PVOID Base = BaseAddress;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemoryEx(Process ? Process : GetCurrentProcess(), &Base, &Size,
                                       AllocationType, PageProtection, ExtendedParameters, ParameterCount);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return NULL;
    }
    return Base;
}

PVOID
WINAPI
MapViewOfFile3(
    _In_ HANDLE FileMapping,
    _In_opt_ HANDLE Process,
    _In_opt_ PVOID BaseAddress,
    _In_ ULONG64 Offset,
    _In_ SIZE_T ViewSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER *ExtendedParameters,
    _In_ ULONG ParameterCount)
{
    LARGE_INTEGER SectionOffset;
    PVOID Base = BaseAddress;
    NTSTATUS Status;

    SectionOffset.QuadPart = Offset;
    Status = NtMapViewOfSectionEx(FileMapping, Process ? Process : GetCurrentProcess(), &Base, &SectionOffset,
                                  &ViewSize, AllocationType, PageProtection, ExtendedParameters, ParameterCount);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return NULL;
    }
    return Base;
}
