/*
 * WoW64 CPU support
 *
 * Copyright 2021 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/asm.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

#ifdef __REACTOS__
#undef GetCurrentProcess
#undef GetCurrentThread
#define GetCurrentProcess() NtCurrentProcess()
#define GetCurrentThread() NtCurrentThread()
#endif

#include "pshpack1.h"
struct thunk_32to64
{
    BYTE  ljmp;   /* jump far, absolute indirect */
    BYTE  modrm;  /* address=disp32, opcode=5 */
    DWORD op;
    DWORD addr;
    WORD  cs;
};
#ifdef __REACTOS__
/* The far jump only reaches 32-bit addresses, and wow64cpu.dll may be
 * anywhere: it lands on a 64-bit jump, placed with the 32-bit one below 2 GB */
struct thunk_opcodes
{
    struct thunk_32to64 syscall_thunk;
    BYTE  pad[2];
    struct
    {
        BYTE    jmp[6];  /* jmp qword ptr [rip] */
        ULONG64 target;
    } trampoline;
};
#else
struct thunk_opcodes
{
    struct thunk_32to64 syscall_thunk;
    struct
    {
        BYTE pushl;  /* pushl $dispatcher_high */
        DWORD dispatcher_high;
        BYTE pushl2;  /* pushl $dispatcher_low */
        DWORD dispatcher_low;
        struct thunk_32to64 t;
    } unix_thunk;
};
#endif
#include "poppack.h"

#ifdef __REACTOS__
static BYTE *code_buffer;
#else
static BYTE DECLSPEC_ALIGN(4096) code_buffer[0x1000];
#endif

static USHORT cs64_sel;
static USHORT ds64_sel;
static USHORT fs32_sel;

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH) LdrDisableThreadCalloutsForDll( inst );
    return TRUE;
}

/***********************************************************************
 *           fpux_to_fpu
 *
 * Build a standard i386 FPU context from an extended one.
 */
static void fpux_to_fpu( I386_FLOATING_SAVE_AREA *fpu, const XMM_SAVE_AREA32 *fpux )
{
    unsigned int i, tag, stack_top;

    fpu->ControlWord   = fpux->ControlWord;
    fpu->StatusWord    = fpux->StatusWord;
    fpu->ErrorOffset   = fpux->ErrorOffset;
    fpu->ErrorSelector = fpux->ErrorSelector | (fpux->ErrorOpcode << 16);
    fpu->DataOffset    = fpux->DataOffset;
    fpu->DataSelector  = fpux->DataSelector;
    fpu->Cr0NpxState   = fpux->StatusWord | 0xffff0000;

    stack_top = (fpux->StatusWord >> 11) & 7;
    fpu->TagWord = 0xffff0000;
    for (i = 0; i < 8; i++)
    {
        memcpy( &fpu->RegisterArea[10 * i], &fpux->FloatRegisters[i], 10 );
        if (!(fpux->TagWord & (1 << i))) tag = 3;  /* empty */
        else
        {
            const M128A *reg = &fpux->FloatRegisters[(i - stack_top) & 7];
            if ((reg->High & 0x7fff) == 0x7fff)  /* exponent all ones */
            {
                tag = 2;  /* special */
            }
            else if (!(reg->High & 0x7fff))  /* exponent all zeroes */
            {
                if (reg->Low) tag = 2;  /* special */
                else tag = 1;  /* zero */
            }
            else
            {
                if (reg->Low >> 63) tag = 0;  /* valid */
                else tag = 2;  /* special */
            }
        }
        fpu->TagWord |= tag << (2 * i);
    }
}

/**********************************************************************
 *           copy_context_64to32
 *
 * Copy a 64-bit context corresponding to an exception happening in 32-bit mode
 * into the corresponding 32-bit context.
 */
static void copy_context_64to32( I386_CONTEXT *ctx32, DWORD flags, AMD64_CONTEXT *ctx64 )
{
    ctx32->ContextFlags = flags;
    flags &= ~CONTEXT_i386;
    if (flags & CONTEXT_I386_INTEGER)
    {
        ctx32->Eax = ctx64->Rax;
        ctx32->Ebx = ctx64->Rbx;
        ctx32->Ecx = ctx64->Rcx;
        ctx32->Edx = ctx64->Rdx;
        ctx32->Esi = ctx64->Rsi;
        ctx32->Edi = ctx64->Rdi;
    }
    if (flags & CONTEXT_I386_CONTROL)
    {
        ctx32->Esp    = ctx64->Rsp;
        ctx32->Ebp    = ctx64->Rbp;
        ctx32->Eip    = ctx64->Rip;
        ctx32->EFlags = ctx64->EFlags;
        ctx32->SegCs  = ctx64->SegCs;
        ctx32->SegSs  = ds64_sel;
    }
    if (flags & CONTEXT_I386_SEGMENTS)
    {
        ctx32->SegDs = ds64_sel;
        ctx32->SegEs = ds64_sel;
        ctx32->SegFs = fs32_sel;
        ctx32->SegGs = ds64_sel;
    }
    if (flags & CONTEXT_I386_DEBUG_REGISTERS)
    {
        ctx32->Dr0 = ctx64->Dr0;
        ctx32->Dr1 = ctx64->Dr1;
        ctx32->Dr2 = ctx64->Dr2;
        ctx32->Dr3 = ctx64->Dr3;
        ctx32->Dr6 = ctx64->Dr6;
        ctx32->Dr7 = ctx64->Dr7;
    }
    if (flags & CONTEXT_I386_FLOATING_POINT)
    {
        fpux_to_fpu( &ctx32->FloatSave, &ctx64->FltSave );
    }
    if (flags & CONTEXT_I386_EXTENDED_REGISTERS)
    {
        *(XSAVE_FORMAT *)ctx32->ExtendedRegisters = ctx64->FltSave;
    }
    /* FIXME: xstate */
}


#ifdef __REACTOS__
/* 32-bit code overwrites the non-volatile 64-bit registers (rbx, rbp, rsi,
 * rdi, xmm6, xmm7). BTCpuSimulate saves them, and its unwind information
 * (also used for syscall_32to64, which runs in its frame) says where: so
 * unwinding from 32-bit code, after an exception, gives back the 64-bit ones,
 * and cpu_simulate finds its frame. */
#define SIMULATE_PROLOG \
    "pushq %rbp\n\t" __ASM_SEH(".seh_pushreg %rbp\n\t") \
    "pushq %rbx\n\t" __ASM_SEH(".seh_pushreg %rbx\n\t") \
    "pushq %rsi\n\t" __ASM_SEH(".seh_pushreg %rsi\n\t") \
    "pushq %rdi\n\t" __ASM_SEH(".seh_pushreg %rdi\n\t") \
    "pushq %r12\n\t" __ASM_SEH(".seh_pushreg %r12\n\t") \
    "pushq %r13\n\t" __ASM_SEH(".seh_pushreg %r13\n\t") \
    "pushq %r14\n\t" __ASM_SEH(".seh_pushreg %r14\n\t") \
    "pushq %r15\n\t" __ASM_SEH(".seh_pushreg %r15\n\t") \
    "subq $0x58,%rsp\n\t" __ASM_SEH(".seh_stackalloc 0x58\n\t") \
    "movdqa %xmm6,0x30(%rsp)\n\t" __ASM_SEH(".seh_savexmm %xmm6,0x30\n\t") \
    "movdqa %xmm7,0x40(%rsp)\n\t" __ASM_SEH(".seh_savexmm %xmm7,0x40\n\t") \
    __ASM_SEH(".seh_endprologue\n\t")
#define SIMULATE_PROLOG_SEH \
    __ASM_SEH(".seh_pushreg %rbp\n\t") \
    __ASM_SEH(".seh_pushreg %rbx\n\t") \
    __ASM_SEH(".seh_pushreg %rsi\n\t") \
    __ASM_SEH(".seh_pushreg %rdi\n\t") \
    __ASM_SEH(".seh_pushreg %r12\n\t") \
    __ASM_SEH(".seh_pushreg %r13\n\t") \
    __ASM_SEH(".seh_pushreg %r14\n\t") \
    __ASM_SEH(".seh_pushreg %r15\n\t") \
    __ASM_SEH(".seh_stackalloc 0x58\n\t") \
    __ASM_SEH(".seh_savexmm %xmm6,0x30\n\t") \
    __ASM_SEH(".seh_savexmm %xmm7,0x40\n\t") \
    __ASM_SEH(".seh_endprologue\n\t")
#else
#define SIMULATE_PROLOG \
    "subq $0x28,%rsp\n" \
    __ASM_SEH(".seh_stackalloc 0x28\n\t") \
    __ASM_SEH(".seh_endprologue\n\t") \
    __ASM_CFI(".cfi_adjust_cfa_offset 0x28\n\t")
#define SIMULATE_PROLOG_SEH \
    __ASM_SEH(".seh_stackalloc 0x28\n\t") \
    __ASM_SEH(".seh_endprologue\n\t") \
    __ASM_CFI(".cfi_adjust_cfa_offset 0x28\n\t")
#endif

/**********************************************************************
 *           syscall_32to64
 *
 * Execute a 64-bit syscall from 32-bit code, then return to 32-bit.
 */
extern void WINAPI syscall_32to64(void);
__ASM_GLOBAL_FUNC( syscall_32to64,
                   /* cf. BTCpuSimulate prolog */
                   SIMULATE_PROLOG_SEH
                   "xchgq %r14,%rsp\n\t"
                   "movl %edi,0x9c(%r13)\n\t"   /* context->Edi */
                   "movl %esi,0xa0(%r13)\n\t"   /* context->Esi */
                   "movl %ebx,0xa4(%r13)\n\t"   /* context->Ebx */
                   "movl %ebp,0xb4(%r13)\n\t"   /* context->Ebp */
                   "movl (%r14),%edx\n\t"
                   "movl %edx,0xb8(%r13)\n\t"   /* context->Eip */
                   "pushfq\n\t"
                   "popq %rdx\n\t"
                   "movl %edx,0xc0(%r13)\n\t"   /* context->EFlags */
                   "leaq 4(%r14),%rdx\n\t"
                   "movl %edx,0xc4(%r13)\n\t"   /* context->Esp */
                   "movq %rax,%rcx\n\t"         /* syscall number */
                   "leaq 8(%r14),%rdx\n\t"      /* parameters */
                   "call " __ASM_NAME("Wow64SystemServiceEx") "\n\t"
                   /* The call's status, also when the context was set during the
                      call (RESET_STATE), as on Windows: an interrupted wait must
                      return STATUS_USER_APC, not the Eax of the context */
                   "movl %eax,0xb0(%r13)\n\t"   /* context->Eax */

                   "syscall_32to64_return:\n\t"
#ifdef __REACTOS__
                   /* A context set with its floating point state (exception
                      dispatch, NtContinue, SetThreadContext): load it. The
                      64-bit side's xmm8-15 stay as they are. */
                   "btrl $1,-4(%r13)\n\t"       /* cpu->Flags & WOW64_CPURESERVED_FLAG_RESTORE_FPU */
                   "jnc 2f\n\t"
                   "movups %xmm8,0x1ec(%r13)\n\t" /* context->ExtendedRegisters + 0x120 */
                   "movups %xmm9,0x1fc(%r13)\n\t"
                   "movups %xmm10,0x20c(%r13)\n\t"
                   "movups %xmm11,0x21c(%r13)\n\t"
                   "movups %xmm12,0x22c(%r13)\n\t"
                   "movups %xmm13,0x23c(%r13)\n\t"
                   "movups %xmm14,0x24c(%r13)\n\t"
                   "movups %xmm15,0x25c(%r13)\n\t"
                   "fxrstor 0xcc(%r13)\n"         /* context->ExtendedRegisters */
                   "2:\n\t"
#endif
                   "movl 0x9c(%r13),%edi\n\t"   /* context->Edi */
                   "movl 0xa0(%r13),%esi\n\t"   /* context->Esi */
                   "movl 0xa4(%r13),%ebx\n\t"   /* context->Ebx */
                   "movl 0xb4(%r13),%ebp\n\t"   /* context->Ebp */
                   "btrl $0,-4(%r13)\n\t"       /* cpu->Flags & WOW64_CPURESERVED_FLAG_RESET_STATE */
                   "jc .Lsyscall_32to64_return\n\t"
                   "movl 0xb8(%r13),%edx\n\t"   /* context->Eip */
                   "movl %edx,(%rsp)\n\t"
                   "movl 0xbc(%r13),%edx\n\t"   /* context->SegCs */
                   "movl %edx,4(%rsp)\n\t"
                   "movl 0xc4(%r13),%r14d\n\t"  /* context->Esp */
                   "xchgq %r14,%rsp\n\t"
                   "ljmp *(%r14)\n"
                   ".Lsyscall_32to64_return:\n\t"
                   "movq %rsp,%r14\n\t"
                   "movl 0xa8(%r13),%edx\n\t"   /* context->Edx */
                   "movl 0xac(%r13),%ecx\n\t"   /* context->Ecx */
                   "movl 0xc8(%r13),%eax\n\t"   /* context->SegSs */
                   "movq %rax,0x20(%rsp)\n\t"
                   "mov %ax,%ds\n\t"
                   "mov %ax,%es\n\t"
                   "mov 0x90(%r13),%fs\n\t"     /* context->SegFs */
                   "movl 0xc4(%r13),%eax\n\t"   /* context->Esp */
                   "movq %rax,0x18(%rsp)\n\t"
                   "movl 0xc0(%r13),%eax\n\t"   /* context->EFlags */
                   "movq %rax,0x10(%rsp)\n\t"
                   "movl 0xbc(%r13),%eax\n\t"   /* context->SegCs */
                   "movq %rax,0x8(%rsp)\n\t"
                   "movl 0xb8(%r13),%eax\n\t"   /* context->Eip */
                   "movq %rax,(%rsp)\n\t"
                   "movl 0xb0(%r13),%eax\n\t"   /* context->Eax */
                   "iretq" )


#ifndef __REACTOS__
/**********************************************************************
 *           unix_call_32to64
 *
 * Execute a 64-bit Unix call from 32-bit code, then return to 32-bit.
 */
extern void WINAPI unix_call_32to64(void);
__ASM_GLOBAL_FUNC( unix_call_32to64,
                   /* cf. BTCpuSimulate prolog */
                   SIMULATE_PROLOG_SEH
                   "xchgq %r14,%rsp\n\t"
                   "movl %edi,0x9c(%r13)\n\t"   /* context->Edi */
                   "movl %esi,0xa0(%r13)\n\t"   /* context->Esi */
                   "movl %ebx,0xa4(%r13)\n\t"   /* context->Ebx */
                   "movl %ebp,0xb4(%r13)\n\t"   /* context->Ebp */
                   "movl 8(%r14),%edx\n\t"
                   "movl %edx,0xb8(%r13)\n\t"   /* context->Eip */
                   "leaq 28(%r14),%rdx\n\t"
                   "movl %edx,0xc4(%r13)\n\t"   /* context->Esp */
                   "movq 12(%r14),%rcx\n\t"     /* handle */
                   "movl 20(%r14),%edx\n\t"     /* code */
                   "movl 24(%r14),%r8d\n\t"     /* args */
                   "callq *(%r14)\n\t"
                   "btrl $0,-4(%r13)\n\t"       /* cpu->Flags & WOW64_CPURESERVED_FLAG_RESET_STATE */
                   "jc .Lsyscall_32to64_return\n\t"
                   "movl 0xb8(%r13),%edx\n\t"   /* context->Eip */
                   "movl %edx,(%rsp)\n\t"
                   "movl 0xbc(%r13),%edx\n\t"   /* context->SegCs */
                   "movl %edx,4(%rsp)\n\t"
                   "movl 0xc4(%r13),%r14d\n\t"  /* context->Esp */
                   "xchgq %r14,%rsp\n\t"
                   "ljmp *(%r14)" )


#endif

/**********************************************************************
 *           BTCpuSimulate  (wow64cpu.@)
 */
__ASM_GLOBAL_FUNC( BTCpuSimulate,
                   SIMULATE_PROLOG
                    "movq %gs:0x30,%r12\n\t"
                    "movq 0x1488(%r12),%rcx\n\t" /* NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED] */
                    "leaq 4(%rcx),%r13\n"        /* cpu->Context */
                    "jmp syscall_32to64_return\n" )


/**********************************************************************
 *           BTCpuProcessInit  (wow64cpu.@)
 */
#ifdef __REACTOS__
NTSTATUS WINAPI BTCpuProcessInit(void)
{
    struct thunk_opcodes *thunk;
    SIZE_T size = 0x1000;
    ULONG old_prot;
    CONTEXT context;
    WOW64INFO *wow64info = NtCurrentTeb()->TlsSlots[WOW64_TLS_WOW64INFO];
    NTSTATUS status;

    wow64info->CpuFlags |= WOW64_CPUFLAGS_MSFT64;

    RtlCaptureContext( &context );
    cs64_sel = context.SegCs;
    ds64_sel = context.SegDs;
    fs32_sel = 0x53;  /* KGDT64_R3_CMTEB | RPL_MASK */

    status = NtAllocateVirtualMemory( GetCurrentProcess(), (void **)&code_buffer, 0x7fffffff, &size,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (status) return status;
    thunk = (struct thunk_opcodes *)code_buffer;

    thunk->syscall_thunk.ljmp  = 0xff;
    thunk->syscall_thunk.modrm = 0x2d;
    thunk->syscall_thunk.op    = PtrToUlong( &thunk->syscall_thunk.addr );
    thunk->syscall_thunk.addr  = PtrToUlong( &thunk->trampoline );
    thunk->syscall_thunk.cs    = cs64_sel;

    thunk->trampoline.jmp[0] = 0xff;
    thunk->trampoline.jmp[1] = 0x25;
    thunk->trampoline.jmp[2] = thunk->trampoline.jmp[3] = 0;
    thunk->trampoline.jmp[4] = thunk->trampoline.jmp[5] = 0;
    thunk->trampoline.target = (ULONG64)syscall_32to64;

    return NtProtectVirtualMemory( GetCurrentProcess(), (void **)&thunk, &size, PAGE_EXECUTE_READ, &old_prot );
}
#else
NTSTATUS WINAPI BTCpuProcessInit(void)
{
    struct thunk_opcodes *thunk = (struct thunk_opcodes *)code_buffer;
    SIZE_T size = sizeof(*thunk);
    ULONG old_prot;
    CONTEXT context;
    HMODULE module;
    UNICODE_STRING str = RTL_CONSTANT_STRING( L"ntdll.dll" );
    void **p__wine_unix_call_dispatcher;
    WOW64INFO *wow64info = NtCurrentTeb()->TlsSlots[WOW64_TLS_WOW64INFO];

    if ((ULONG_PTR)syscall_32to64 >> 32)
    {
        ERR( "wow64cpu loaded above 4G, disabling\n" );
        return STATUS_INVALID_ADDRESS;
    }

    wow64info->CpuFlags |= WOW64_CPUFLAGS_MSFT64;

    LdrGetDllHandle( NULL, 0, &str, &module );
    p__wine_unix_call_dispatcher = RtlFindExportedRoutineByName( module, "__wine_unix_call_dispatcher" );

    RtlCaptureContext( &context );
    cs64_sel = context.SegCs;
    ds64_sel = context.SegDs;
    fs32_sel = context.SegFs;

    thunk->syscall_thunk.ljmp  = 0xff;
    thunk->syscall_thunk.modrm = 0x2d;
    thunk->syscall_thunk.op    = PtrToUlong( &thunk->syscall_thunk.addr );
    thunk->syscall_thunk.addr  = PtrToUlong( syscall_32to64 );
    thunk->syscall_thunk.cs    = cs64_sel;

    thunk->unix_thunk.pushl   = 0x68;
    thunk->unix_thunk.dispatcher_high = (ULONG_PTR)*p__wine_unix_call_dispatcher >> 32;
    thunk->unix_thunk.pushl2  = 0x68;
    thunk->unix_thunk.dispatcher_low = (ULONG_PTR)*p__wine_unix_call_dispatcher;
    thunk->unix_thunk.t.ljmp  = 0xff;
    thunk->unix_thunk.t.modrm = 0x2d;
    thunk->unix_thunk.t.op    = PtrToUlong( &thunk->unix_thunk.t.addr );
    thunk->unix_thunk.t.addr  = PtrToUlong( unix_call_32to64 );
    thunk->unix_thunk.t.cs    = cs64_sel;

    NtProtectVirtualMemory( GetCurrentProcess(), (void **)&thunk, &size, PAGE_EXECUTE_READ, &old_prot );
    return STATUS_SUCCESS;
}
#endif


/**********************************************************************
 *           BTCpuGetBopCode  (wow64cpu.@)
 */
void * WINAPI BTCpuGetBopCode(void)
{
    struct thunk_opcodes *thunk = (struct thunk_opcodes *)code_buffer;

    return &thunk->syscall_thunk;
}


#ifndef __REACTOS__
/**********************************************************************
 *           __wine_get_unix_opcode  (wow64cpu.@)
 */
void * WINAPI __wine_get_unix_opcode(void)
{
    struct thunk_opcodes *thunk = (struct thunk_opcodes *)code_buffer;

    return &thunk->unix_thunk;
}


#endif

/**********************************************************************
 *           BTCpuIsProcessorFeaturePresent  (wow64cpu.@)
 */
BOOLEAN WINAPI BTCpuIsProcessorFeaturePresent( UINT feature )
{
    /* assume CPU features are the same for 32- and 64-bit */
#ifdef __REACTOS__
    /* KUSER_SHARED_DATA->ProcessorFeatures */
    return feature < 64 && ((const BOOLEAN *)0x7ffe0274)[feature];
#else
    return RtlIsProcessorFeaturePresent( feature );
#endif
}


/**********************************************************************
 *           BTCpuGetContext  (wow64cpu.@)
 */
NTSTATUS WINAPI BTCpuGetContext( HANDLE thread, HANDLE process, void *unknown, I386_CONTEXT *ctx )
{
    return RtlWow64GetThreadContext( thread, ctx );
}


/**********************************************************************
 *           BTCpuSetContext  (wow64cpu.@)
 */
NTSTATUS WINAPI BTCpuSetContext( HANDLE thread, HANDLE process, void *unknown, I386_CONTEXT *ctx )
{
    return RtlWow64SetThreadContext( thread, ctx );
}


/**********************************************************************
 *           BTCpuResetToConsistentState  (wow64cpu.@)
 */
NTSTATUS WINAPI BTCpuResetToConsistentState( EXCEPTION_POINTERS *ptrs )
{
    CONTEXT *context = ptrs->ContextRecord;
    I386_CONTEXT wow_context;
    struct machine_frame
    {
        ULONG64 rip;
        ULONG64 cs;
        ULONG64 eflags;
        ULONG64 rsp;
        ULONG64 ss;
    } *machine_frame;

    if (context->SegCs == cs64_sel) return STATUS_SUCCESS;  /* exception in 64-bit code, nothing to do */

    copy_context_64to32( &wow_context, CONTEXT_I386_ALL, (AMD64_CONTEXT *)context );
    wow_context.EFlags &= ~(0x100|0x40000);
    BTCpuSetContext( GetCurrentThread(), GetCurrentProcess(), NULL, &wow_context );

    /* fixup context to pretend that we jumped to 64-bit mode */
    context->Rip = (ULONG64)syscall_32to64;
    context->SegCs = cs64_sel;
    context->Rsp = context->R14;
    /* fixup machine frame */
    machine_frame = (struct machine_frame *)(((ULONG_PTR)(ptrs->ExceptionRecord + 1) + 15) & ~15);
    machine_frame->rip = context->Rip;
    machine_frame->rsp = context->Rsp;
    return STATUS_SUCCESS;
}


/**********************************************************************
 *           BTCpuTurboThunkControl  (wow64cpu.@)
 */
NTSTATUS WINAPI BTCpuTurboThunkControl( ULONG enable )
{
    if (enable) return STATUS_NOT_SUPPORTED;
    /* we don't have turbo thunks yet */
    return STATUS_SUCCESS;
}
