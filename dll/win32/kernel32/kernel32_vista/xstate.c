/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The extended processor state functions of a CONTEXT (Windows 7 SP1)
 *
 * ReactOS does not save the extended state (AVX...) of threads: only the
 * legacy features, x87 (0) and SSE (1), which every CONTEXT holds, are
 * enabled. Programs check GetEnabledXStateFeatures before they ask for more
 * (the .NET runtime among others imports these functions).
 */

#include <k32_vista.h>

#define NDEBUG
#include <debug.h>

#define XSTATE_LEGACY_FEATURES 3 /* XSTATE_MASK_LEGACY: x87 and SSE */

#ifdef _M_AMD64
#define CONTEXT_ARCH CONTEXT_AMD64
#define CONTEXT_LEGACY_FLOAT CONTEXT_FLOATING_POINT
#define CONTEXT_ALIGNMENT 16
#else
#define CONTEXT_ARCH CONTEXT_i386
#define CONTEXT_LEGACY_FLOAT CONTEXT_EXTENDED_REGISTERS
#define CONTEXT_ALIGNMENT 4
#endif

#ifndef CONTEXT_XSTATE
#define CONTEXT_XSTATE (CONTEXT_ARCH | 0x40)
#endif

DWORD64
WINAPI
GetEnabledXStateFeatures(VOID)
{
    return XSTATE_LEGACY_FEATURES;
}

static
BOOL
IsContextFlagSet(DWORD Flags, DWORD Flag)
{
    return (Flags & Flag) == Flag;
}

BOOL
WINAPI
InitializeContext2(
    _Out_writes_bytes_opt_(*ContextLength) PVOID Buffer,
    _In_ DWORD ContextFlags,
    _Out_ PCONTEXT *Context,
    _Inout_ PDWORD ContextLength,
    _In_ ULONG64 XStateCompactionMask)
{
    DWORD Needed = sizeof(CONTEXT) + CONTEXT_ALIGNMENT - 1;
    PCONTEXT NewContext;

    UNREFERENCED_PARAMETER(XStateCompactionMask);

    /* No extended state to put after the context */
    if (IsContextFlagSet(ContextFlags, CONTEXT_XSTATE))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    if (!Buffer || *ContextLength < Needed)
    {
        *ContextLength = Needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    NewContext = (PCONTEXT)(((ULONG_PTR)Buffer + CONTEXT_ALIGNMENT - 1) & ~(ULONG_PTR)(CONTEXT_ALIGNMENT - 1));
    RtlZeroMemory(NewContext, sizeof(*NewContext));
    NewContext->ContextFlags = ContextFlags;
    *Context = NewContext;
    *ContextLength = Needed;
    return TRUE;
}

BOOL
WINAPI
InitializeContext(
    _Out_writes_bytes_opt_(*ContextLength) PVOID Buffer,
    _In_ DWORD ContextFlags,
    _Out_ PCONTEXT *Context,
    _Inout_ PDWORD ContextLength)
{
    return InitializeContext2(Buffer, ContextFlags, Context, ContextLength, ~0ULL);
}

/* Copies the parts of the context that ContextFlags names */
BOOL
WINAPI
CopyContext(
    _Inout_ PCONTEXT Destination,
    _In_ DWORD ContextFlags,
    _In_ PCONTEXT Source)
{
    DWORD Flags = ContextFlags & Source->ContextFlags;

    if (IsContextFlagSet(ContextFlags, CONTEXT_XSTATE) &&
        IsContextFlagSet(Source->ContextFlags, CONTEXT_XSTATE))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

#ifdef _M_AMD64
    if (IsContextFlagSet(Flags, CONTEXT_CONTROL))
    {
        Destination->SegCs = Source->SegCs;
        Destination->SegSs = Source->SegSs;
        Destination->EFlags = Source->EFlags;
        Destination->Rsp = Source->Rsp;
        Destination->Rip = Source->Rip;
    }
    if (IsContextFlagSet(Flags, CONTEXT_INTEGER))
    {
        Destination->Rax = Source->Rax;
        Destination->Rcx = Source->Rcx;
        Destination->Rdx = Source->Rdx;
        Destination->Rbx = Source->Rbx;
        Destination->Rbp = Source->Rbp;
        Destination->Rsi = Source->Rsi;
        Destination->Rdi = Source->Rdi;
        Destination->R8 = Source->R8;
        Destination->R9 = Source->R9;
        Destination->R10 = Source->R10;
        Destination->R11 = Source->R11;
        Destination->R12 = Source->R12;
        Destination->R13 = Source->R13;
        Destination->R14 = Source->R14;
        Destination->R15 = Source->R15;
    }
    if (IsContextFlagSet(Flags, CONTEXT_SEGMENTS))
    {
        Destination->SegDs = Source->SegDs;
        Destination->SegEs = Source->SegEs;
        Destination->SegFs = Source->SegFs;
        Destination->SegGs = Source->SegGs;
    }
    if (IsContextFlagSet(Flags, CONTEXT_FLOATING_POINT))
    {
        Destination->MxCsr = Source->MxCsr;
        Destination->FltSave = Source->FltSave;
    }
    if (IsContextFlagSet(Flags, CONTEXT_DEBUG_REGISTERS))
    {
        Destination->Dr0 = Source->Dr0;
        Destination->Dr1 = Source->Dr1;
        Destination->Dr2 = Source->Dr2;
        Destination->Dr3 = Source->Dr3;
        Destination->Dr6 = Source->Dr6;
        Destination->Dr7 = Source->Dr7;
    }
#else
    if (IsContextFlagSet(Flags, CONTEXT_CONTROL))
    {
        Destination->Ebp = Source->Ebp;
        Destination->Eip = Source->Eip;
        Destination->SegCs = Source->SegCs;
        Destination->EFlags = Source->EFlags;
        Destination->Esp = Source->Esp;
        Destination->SegSs = Source->SegSs;
    }
    if (IsContextFlagSet(Flags, CONTEXT_INTEGER))
    {
        Destination->Edi = Source->Edi;
        Destination->Esi = Source->Esi;
        Destination->Ebx = Source->Ebx;
        Destination->Edx = Source->Edx;
        Destination->Ecx = Source->Ecx;
        Destination->Eax = Source->Eax;
    }
    if (IsContextFlagSet(Flags, CONTEXT_SEGMENTS))
    {
        Destination->SegGs = Source->SegGs;
        Destination->SegFs = Source->SegFs;
        Destination->SegEs = Source->SegEs;
        Destination->SegDs = Source->SegDs;
    }
    if (IsContextFlagSet(Flags, CONTEXT_FLOATING_POINT))
        Destination->FloatSave = Source->FloatSave;
    if (IsContextFlagSet(Flags, CONTEXT_DEBUG_REGISTERS))
    {
        Destination->Dr0 = Source->Dr0;
        Destination->Dr1 = Source->Dr1;
        Destination->Dr2 = Source->Dr2;
        Destination->Dr3 = Source->Dr3;
        Destination->Dr6 = Source->Dr6;
        Destination->Dr7 = Source->Dr7;
    }
    if (IsContextFlagSet(Flags, CONTEXT_EXTENDED_REGISTERS))
        RtlCopyMemory(Destination->ExtendedRegisters, Source->ExtendedRegisters, sizeof(Source->ExtendedRegisters));
#endif

    Destination->ContextFlags |= Flags;
    return TRUE;
}

/* x87 (0) and SSE (1) are in the CONTEXT itself */
PVOID
WINAPI
LocateXStateFeature(
    _In_ PCONTEXT Context,
    _In_ DWORD FeatureId,
    _Out_opt_ PDWORD Length)
{
    if (!IsContextFlagSet(Context->ContextFlags, CONTEXT_ARCH) || FeatureId >= 2)
        return NULL;

#ifdef _M_AMD64
    if (FeatureId == 1)
    {
        if (Length) *Length = sizeof(M128A) * 16;
        return &Context->FltSave.XmmRegisters;
    }
    if (Length) *Length = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    return &Context->FltSave;
#else
    if (FeatureId == 1)
    {
        if (Length) *Length = sizeof(M128A) * 8;
        return (PUCHAR)&Context->ExtendedRegisters + FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    }
    if (Length) *Length = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    return &Context->ExtendedRegisters;
#endif
}

BOOL
WINAPI
SetXStateFeaturesMask(
    _Inout_ PCONTEXT Context,
    _In_ DWORD64 FeatureMask)
{
    if (!IsContextFlagSet(Context->ContextFlags, CONTEXT_ARCH))
        return FALSE;

    if (FeatureMask & XSTATE_LEGACY_FEATURES)
        Context->ContextFlags |= CONTEXT_LEGACY_FLOAT;

    /* Nothing beyond the legacy features */
    return !(FeatureMask & ~(DWORD64)XSTATE_LEGACY_FEATURES);
}

BOOL
WINAPI
GetXStateFeaturesMask(
    _In_ PCONTEXT Context,
    _Out_ PDWORD64 FeatureMask)
{
    if (!IsContextFlagSet(Context->ContextFlags, CONTEXT_ARCH))
        return FALSE;

    *FeatureMask = IsContextFlagSet(Context->ContextFlags, CONTEXT_LEGACY_FLOAT) ? XSTATE_LEGACY_FEATURES : 0;
    return TRUE;
}
