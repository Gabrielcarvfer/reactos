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

/* FUNCTIONS ****************************************************************/

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
 * CPU area but in its kernel frame, which the 64-bit context shows */
static
BOOLEAN
RtlpWow64GetLiveContext(
    _In_ HANDLE Thread,
    _Out_ PCONTEXT Context64)
{
    if (Thread == NtCurrentThread()) return FALSE;

    Context64->ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS | CONTEXT_FLOATING_POINT;
    if (!NT_SUCCESS(NtGetContextThread(Thread, Context64))) return FALSE;
    return (Context64->SegCs & ~3) == WOW64_CS32;
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
    NTSTATUS Status;

    if (RtlpWow64GetLiveContext(Thread, &Context64))
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
        if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
        {
            RtlCopyMemory(Context->ExtendedRegisters,
                          &Context64.FltSave,
                          sizeof(Context->ExtendedRegisters));
        }
        return STATUS_SUCCESS;
    }

    Status = RtlpWow64GetCpuArea(Thread, &Cpu);
    if (!NT_SUCCESS(Status)) return Status;

    *Context = *(PWOW64_CONTEXT)(Cpu + 1);
    Context->ContextFlags = Flags;
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
    NTSTATUS Status;

    if (RtlpWow64GetLiveContext(Thread, &Context64))
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
        if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
        {
            RtlCopyMemory(&Context64.FltSave,
                          Context->ExtendedRegisters,
                          sizeof(Context->ExtendedRegisters));
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
    if ((Flags & WOW64_CONTEXT_FLOATING_POINT) == WOW64_CONTEXT_FLOATING_POINT)
        CpuContext->FloatSave = Context->FloatSave;
    if ((Flags & WOW64_CONTEXT_DEBUG_REGISTERS) == WOW64_CONTEXT_DEBUG_REGISTERS)
    {
        CpuContext->Dr0 = Context->Dr0;
        CpuContext->Dr1 = Context->Dr1;
        CpuContext->Dr2 = Context->Dr2;
        CpuContext->Dr3 = Context->Dr3;
        CpuContext->Dr6 = Context->Dr6;
        CpuContext->Dr7 = Context->Dr7;
    }
    if ((Flags & WOW64_CONTEXT_EXTENDED_REGISTERS) == WOW64_CONTEXT_EXTENDED_REGISTERS)
    {
        RtlCopyMemory(CpuContext->ExtendedRegisters,
                      Context->ExtendedRegisters,
                      sizeof(CpuContext->ExtendedRegisters));
    }

    /* Return to 32-bit code with all the registers, not just the ones a
     * system call keeps */
    Cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
    return STATUS_SUCCESS;
}
