/*
 * PROJECT:     ReactOS NT User-Mode Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The WoW64 CPU area: where the 32-bit registers of a thread of
 *              a 32-bit process are kept while it runs 64-bit code
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* INCLUDES *****************************************************************/

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

/* The TEB slot that points to the CPU area, and the area's header, as on
 * Windows. The 32-bit context follows the header. wow64.dll sets it up. */
#define WOW64_TLS_CPURESERVED 1
#define WOW64_CPURESERVED_FLAG_RESET_STATE 1
/* ReactOS: the floating point state in the CPU area (ExtendedRegisters) was
 * set and must be loaded into the FPU on the way back to 32-bit code
 * (wow64cpu). Otherwise the FPU itself holds the 32-bit state: 64-bit code
 * keeps the control words, and doesn't use the x87 unit. */
#define WOW64_CPURESERVED_FLAG_RESTORE_FPU 2

typedef struct _WOW64_CPURESERVED
{
    USHORT Flags;
    USHORT Machine;
} WOW64_CPURESERVED, *PWOW64_CPURESERVED;

/* The selectors of 32-bit user code on a 64-bit kernel: code, data and the
 * 32-bit TEB (KGDT64_R3_CMCODE, KGDT64_R3_DATA and KGDT64_R3_CMTEB, with
 * RPL 3). 32-bit code may give the selectors of the 32-bit kernel (1Bh,
 * 23h, 3Bh): like on Windows, these are always replaced. */
#define WOW64_CS32 0x20
#define WOW64_CS32_RPL3 0x23
#define WOW64_DS32_RPL3 0x2b
#define WOW64_FS32_RPL3 0x53

/* The x87 status word bits of pending exceptions (and "busy") */
#define WOW64_FSW_EXCEPTIONS 0x80ff

/* FUNCTIONS ****************************************************************/

static
VOID
RtlpWow64SaveFpu(
    _Out_ PXSAVE_FORMAT Area)
{
#if defined(__GNUC__)
    __asm__ __volatile__("fxsave %0" : "=m" (*Area));
#else
    _fxsave(Area);
#endif
}

/* FXSAVE image -> i386 FNSAVE layout (FloatSave) */
static
VOID
RtlpWow64FxToFloatSave(
    _Out_ PWOW64_FLOATING_SAVE_AREA Fpu,
    _In_ const XSAVE_FORMAT *Fx)
{
    ULONG i, Tag, Top;

    Fpu->ControlWord = 0xffff0000 | Fx->ControlWord;
    Fpu->StatusWord = 0xffff0000 | Fx->StatusWord;
    Fpu->ErrorOffset = Fx->ErrorOffset;
    Fpu->ErrorSelector = Fx->ErrorSelector | ((ULONG)Fx->ErrorOpcode << 16);
    Fpu->DataOffset = Fx->DataOffset;
    Fpu->DataSelector = 0xffff0000 | Fx->DataSelector;
    Fpu->Cr0NpxState = 0;

    /* The abridged tag word has a bit per physical register; FNSAVE has two,
     * computed from the register's contents. FloatRegisters are in ST order. */
    Top = (Fx->StatusWord >> 11) & 7;
    Fpu->TagWord = 0xffff0000;
    for (i = 0; i < 8; i++)
    {
        const M128A *Reg = &Fx->FloatRegisters[(i - Top) & 7];
        USHORT Exponent = (USHORT)Reg->High & 0x7fff;

        if (!(Fx->TagWord & (1 << i))) Tag = 3;                 /* empty */
        else if (Exponent == 0x7fff) Tag = 2;                    /* special */
        else if (!Exponent) Tag = Reg->Low ? 2 : 1;              /* denormal / zero */
        else Tag = (Reg->Low >> 63) ? 0 : 2;                     /* valid / unnormal */
        Fpu->TagWord |= Tag << (2 * i);

        RtlCopyMemory(&Fpu->RegisterArea[10 * i], &Fx->FloatRegisters[i], 10);
    }
}

/* i386 FNSAVE layout (FloatSave) -> the x87 part of an FXSAVE image */
static
VOID
RtlpWow64FloatSaveToFx(
    _Inout_ PXSAVE_FORMAT Fx,
    _In_ const WOW64_FLOATING_SAVE_AREA *Fpu)
{
    ULONG i;

    Fx->ControlWord = (USHORT)Fpu->ControlWord;
    Fx->StatusWord = (USHORT)Fpu->StatusWord;
    Fx->ErrorOffset = Fpu->ErrorOffset;
    Fx->ErrorSelector = (USHORT)Fpu->ErrorSelector;
    Fx->ErrorOpcode = (USHORT)(Fpu->ErrorSelector >> 16) & 0x7ff;
    Fx->DataOffset = Fpu->DataOffset;
    Fx->DataSelector = (USHORT)Fpu->DataSelector;
    Fx->TagWord = 0;
    for (i = 0; i < 8; i++)
    {
        if (((Fpu->TagWord >> (2 * i)) & 3) != 3) Fx->TagWord |= 1 << i;
        RtlZeroMemory(&Fx->FloatRegisters[i], sizeof(Fx->FloatRegisters[i]));
        RtlCopyMemory(&Fx->FloatRegisters[i], &Fpu->RegisterArea[10 * i], 10);
    }
}

/* The FXSAVE image a WoW64 context sets: its ExtendedRegisters, then its
 * FloatSave on top (as the i386 kernel does), over Base for what the context
 * doesn't give. Only the part that 32-bit code sees (up to xmm7). */
static
VOID
RtlpWow64ContextToFx(
    _Out_ PXSAVE_FORMAT Fx,
    _In_ const XSAVE_FORMAT *Base,
    _In_ PWOW64_CONTEXT Context)
{
    ULONG Flags = Context->ContextFlags;
    XSAVE_FORMAT DECLSPEC_ALIGN(16) Live;

    *Fx = *Base;
    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
    {
        RtlCopyMemory(Fx, Context->ExtendedRegisters, FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters[8]));
    }
    if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
    {
        RtlpWow64FloatSaveToFx(Fx, &Context->FloatSave);
    }

    /* Reserved MXCSR bits would make FXRSTOR fault */
    RtlpWow64SaveFpu(&Live);
    Fx->MxCsr &= Live.MxCsr_Mask ? Live.MxCsr_Mask : 0xffbf;
    Fx->MxCsr_Mask = Live.MxCsr_Mask;
}

static
VOID
RtlpWow64FxToContext(
    _Inout_ PWOW64_CONTEXT Context,
    _In_ const XSAVE_FORMAT *Fx)
{
    ULONG Flags = Context->ContextFlags;

    if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
        RtlpWow64FxToFloatSave(&Context->FloatSave, Fx);
    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
        RtlCopyMemory(Context->ExtendedRegisters, Fx, sizeof(Context->ExtendedRegisters));
}

#define WOW64_CONTEXT_HAS_FPU(Flags) \
    ((((Flags) & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT) || \
     (((Flags) & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS))

NTSTATUS
NTAPI
RtlWow64GetCurrentCpuArea(
    _Out_opt_ PUSHORT Machine,
    _Out_opt_ PVOID *Context,
    _Out_opt_ PVOID *ContextEx)
{
    PWOW64_CPURESERVED Cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];

    if (!Cpu) return STATUS_INVALID_PARAMETER;
    if (Machine) *Machine = Cpu->Machine;
    if (Context) *Context = Cpu + 1;
    if (ContextEx) *ContextEx = (PWOW64_CONTEXT)(Cpu + 1) + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpWow64GetCpuArea(
    _In_ HANDLE Thread,
    _Out_ PWOW64_CPURESERVED *Cpu)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status;

    if (Thread == NtCurrentThread())
    {
        *Cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
        return *Cpu ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }

    Status = NtQueryInformationThread(Thread,
                                      ThreadBasicInformation,
                                      &Info,
                                      sizeof(Info),
                                      NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* FIXME: threads of other processes */
    if (Info.ClientId.UniqueProcess != NtCurrentTeb()->ClientId.UniqueProcess)
        return STATUS_NOT_IMPLEMENTED;

    *Cpu = ((PTEB)Info.TebBaseAddress)->TlsSlots[WOW64_TLS_CPURESERVED];
    return *Cpu ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/* A thread stopped while it ran 32-bit code: its registers are not in the
 * CPU area but in its kernel frame, which the 64-bit context shows. For
 * another thread, *Valid says whether Context64 was read (its FltSave is the
 * thread's FPU, which holds the 32-bit state also while it runs 64-bit code). */
static
BOOLEAN
RtlpWow64GetLiveContext(
    _In_ HANDLE Thread,
    _Out_ PCONTEXT Context64,
    _Out_ PBOOLEAN Valid)
{
    *Valid = FALSE;
    if (Thread == NtCurrentThread()) return FALSE;

    Context64->ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS | CONTEXT_FLOATING_POINT;
    if (!NT_SUCCESS(NtGetContextThread(Thread, Context64))) return FALSE;
    *Valid = TRUE;
    return (Context64->SegCs & ~3) == WOW64_CS32;
}

/* The FPU state of a thread that runs (or is stopped in) 64-bit code */
static
VOID
RtlpWow64GetFpu(
    _Out_ PXSAVE_FORMAT Fx,
    _In_ PWOW64_CPURESERVED Cpu,
    _In_ PCONTEXT Context64,
    _In_ BOOLEAN Valid64)
{
    if (Cpu->Flags & WOW64_CPURESERVED_FLAG_RESTORE_FPU)
    {
        /* Set, not loaded yet */
        RtlCopyMemory(Fx, ((PWOW64_CONTEXT)(Cpu + 1))->ExtendedRegisters, sizeof(*Fx));
    }
    else if (Valid64)
    {
        *Fx = Context64->FltSave;
    }
    else
    {
        RtlpWow64SaveFpu(Fx);
    }
}

NTSTATUS
NTAPI
RtlWow64GetThreadContext(
    _In_ HANDLE Thread,
    _Inout_ PWOW64_CONTEXT Context)
{
    ULONG Flags = Context->ContextFlags;
    PWOW64_CPURESERVED Cpu;
    CONTEXT Context64;
    XSAVE_FORMAT DECLSPEC_ALIGN(16) Fx;
    BOOLEAN Valid64;
    NTSTATUS Status;

    if (RtlpWow64GetLiveContext(Thread, &Context64, &Valid64))
    {
        if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
        {
            Context->Eax = (ULONG)Context64.Rax;
            Context->Ebx = (ULONG)Context64.Rbx;
            Context->Ecx = (ULONG)Context64.Rcx;
            Context->Edx = (ULONG)Context64.Rdx;
            Context->Esi = (ULONG)Context64.Rsi;
            Context->Edi = (ULONG)Context64.Rdi;
        }
        if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
        {
            Context->Ebp = (ULONG)Context64.Rbp;
            Context->Esp = (ULONG)Context64.Rsp;
            Context->Eip = (ULONG)Context64.Rip;
            Context->EFlags = Context64.EFlags;
            Context->SegCs = Context64.SegCs;
            Context->SegSs = Context64.SegSs;
        }
        if ((Flags & WOW64_CONTEXT_SEGMENTS) == WOW64_CONTEXT_SEGMENTS)
        {
            Context->SegDs = Context64.SegDs;
            Context->SegEs = Context64.SegEs;
            Context->SegFs = Context64.SegFs;
            Context->SegGs = Context64.SegGs;
        }
        RtlpWow64FxToContext(Context, &Context64.FltSave);
        return STATUS_SUCCESS;
    }

    Status = RtlpWow64GetCpuArea(Thread, &Cpu);
    if (!NT_SUCCESS(Status)) return Status;

    *Context = *(PWOW64_CONTEXT)(Cpu + 1);
    Context->ContextFlags = Flags;
    if (WOW64_CONTEXT_HAS_FPU(Flags))
    {
        RtlpWow64GetFpu(&Fx, Cpu, &Context64, Valid64);
        RtlpWow64FxToContext(Context, &Fx);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlWow64SetThreadContext(
    _In_ HANDLE Thread,
    _In_ PWOW64_CONTEXT Context)
{
    ULONG Flags = Context->ContextFlags;
    PWOW64_CPURESERVED Cpu;
    PWOW64_CONTEXT CpuContext;
    CONTEXT Context64;
    XSAVE_FORMAT DECLSPEC_ALIGN(16) Fx, Base;
    BOOLEAN Valid64;
    NTSTATUS Status;

    if (RtlpWow64GetLiveContext(Thread, &Context64, &Valid64))
    {
        if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
        {
            Context64.Rax = Context->Eax;
            Context64.Rbx = Context->Ebx;
            Context64.Rcx = Context->Ecx;
            Context64.Rdx = Context->Edx;
            Context64.Rsi = Context->Esi;
            Context64.Rdi = Context->Edi;
        }
        if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
        {
            Context64.Rbp = Context->Ebp;
            Context64.Rsp = Context->Esp;
            Context64.Rip = Context->Eip;
            Context64.EFlags = Context->EFlags;
        }
        if (WOW64_CONTEXT_HAS_FPU(Flags))
        {
            /* (xmm8-15 belong to 64-bit code: they stay) */
            Base = Context64.FltSave;
            RtlpWow64ContextToFx(&Context64.FltSave, &Base, Context);
            Context64.MxCsr = Context64.FltSave.MxCsr;
        }
        return NtSetContextThread(Thread, &Context64);
    }

    Status = RtlpWow64GetCpuArea(Thread, &Cpu);
    if (!NT_SUCCESS(Status)) return Status;
    CpuContext = (PWOW64_CONTEXT)(Cpu + 1);

    if ((Flags & WOW64_CONTEXT_INTEGER) == WOW64_CONTEXT_INTEGER)
    {
        CpuContext->Eax = Context->Eax;
        CpuContext->Ebx = Context->Ebx;
        CpuContext->Ecx = Context->Ecx;
        CpuContext->Edx = Context->Edx;
        CpuContext->Esi = Context->Esi;
        CpuContext->Edi = Context->Edi;
    }
    if ((Flags & WOW64_CONTEXT_CONTROL) == WOW64_CONTEXT_CONTROL)
    {
        CpuContext->Ebp = Context->Ebp;
        CpuContext->Esp = Context->Esp;
        CpuContext->Eip = Context->Eip;
        CpuContext->EFlags = Context->EFlags;
    }
    CpuContext->SegCs = WOW64_CS32_RPL3;
    CpuContext->SegSs = WOW64_DS32_RPL3;
    CpuContext->SegDs = WOW64_DS32_RPL3;
    CpuContext->SegEs = WOW64_DS32_RPL3;
    CpuContext->SegFs = WOW64_FS32_RPL3;
    CpuContext->SegGs = WOW64_DS32_RPL3;
    if ((Flags & WOW64_CONTEXT_DEBUG_REGISTERS) == WOW64_CONTEXT_DEBUG_REGISTERS)
    {
        CpuContext->Dr0 = Context->Dr0;
        CpuContext->Dr1 = Context->Dr1;
        CpuContext->Dr2 = Context->Dr2;
        CpuContext->Dr3 = Context->Dr3;
        CpuContext->Dr6 = Context->Dr6;
        CpuContext->Dr7 = Context->Dr7;
    }
    if (WOW64_CONTEXT_HAS_FPU(Flags))
    {
        /* Loaded into the FPU on the way back to 32-bit code (wow64cpu) */
        RtlpWow64GetFpu(&Base, Cpu, &Context64, Valid64);
        RtlpWow64ContextToFx(&Fx, &Base, Context);
        RtlCopyMemory(CpuContext->ExtendedRegisters, &Fx, sizeof(CpuContext->ExtendedRegisters));
        RtlpWow64FxToFloatSave(&CpuContext->FloatSave, &Fx);
        Cpu->Flags |= WOW64_CPURESERVED_FLAG_RESTORE_FPU;
    }

    /* Return to 32-bit code with all the registers, not just the ones a
     * system call keeps */
    Cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
    return STATUS_SUCCESS;
}
