/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test the x87 and SSE state in exception contexts, NtContinue and Get/SetThreadContext
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * An exception context holds the FPU state of the faulting code, and resuming
 * (EXCEPTION_CONTINUE_EXECUTION, NtContinue) loads the FPU state of the
 * possibly changed context, whatever the handler did to the FPU meanwhile.
 * 64-bit ReactOS left the x87 part of the context empty and ignored it, and
 * WoW64 never loaded the FPU state of a 32-bit context: AnyDesk (32-bit,
 * packed) changes the x87 control word in its exception handlers and then
 * ran with unmasked floating point exceptions. Its 32-bit build is the
 * interesting one. The expected values are those of Windows 10.
 */

#if defined(__GNUC__) && (defined(_M_IX86) || defined(_M_AMD64))

static USHORT GetCw(void) { USHORT Cw; __asm__ __volatile__("fnstcw %0" : "=m" (Cw)); return Cw; }
static USHORT GetSw(void) { USHORT Sw; __asm__ __volatile__("fnstsw %0" : "=m" (Sw)); return Sw; }
static void SetCw(USHORT Cw) { __asm__ __volatile__("fldcw %0" : : "m" (Cw)); }
static ULONG GetCsr(void) { ULONG Csr; __asm__ __volatile__("stmxcsr %0" : "=m" (Csr)); return Csr; }
static void SetCsr(ULONG Csr) { __asm__ __volatile__("ldmxcsr %0" : : "m" (Csr)); }

/* The FXSAVE image in a context */
static PUCHAR FxArea(PCONTEXT Context)
{
#ifdef _M_AMD64
    return (PUCHAR)&Context->FltSave;
#else
    return Context->ExtendedRegisters;
#endif
}
static USHORT CtxCw(PCONTEXT Context) { return *(PUSHORT)(FxArea(Context) + 0); }
static USHORT CtxSw(PCONTEXT Context) { return *(PUSHORT)(FxArea(Context) + 2); }
static ULONG CtxCsr(PCONTEXT Context) { return *(PULONG)(FxArea(Context) + 24); }

static void CtxSetCw(PCONTEXT Context, USHORT Cw)
{
    *(PUSHORT)(FxArea(Context) + 0) = Cw;
#ifdef _M_IX86
    Context->FloatSave.ControlWord = (Context->FloatSave.ControlWord & 0xffff0000) | Cw;
#endif
}

static void CtxClearSw(PCONTEXT Context)
{
    *(PUSHORT)(FxArea(Context) + 2) &= ~0x80ff;
#ifdef _M_IX86
    Context->FloatSave.StatusWord &= ~0x80ff;
#endif
}

static void CtxSetCsr(PCONTEXT Context, ULONG Csr)
{
    *(PULONG)(FxArea(Context) + 24) = Csr;
#ifdef _M_AMD64
    Context->MxCsr = Csr;
#endif
}

static void CtxSkip(PCONTEXT Context, ULONG Bytes)
{
#ifdef _M_AMD64
    Context->Rip += Bytes;
#else
    Context->Eip += Bytes;
#endif
}

/* What the handler does */
static enum { H_SKIP, H_CLOBBER, H_SETCONTEXT, H_CLEARFP, H_NTCONTINUE } Mode;
static volatile NTSTATUS SeenCode;
static volatile LONG SeenCount;
static volatile USHORT SeenCw, SeenSw;
static volatile ULONG SeenCsr;
static USHORT NewCw, RestoreCw;
static ULONG NewCsr, RestoreCsr;

static LONG NTAPI Handler(PEXCEPTION_POINTERS Pointers)
{
    PCONTEXT Context = Pointers->ContextRecord;
    NTSTATUS Code = Pointers->ExceptionRecord->ExceptionCode;

    if (Code != STATUS_ILLEGAL_INSTRUCTION && Code != STATUS_ACCESS_VIOLATION &&
        Code != STATUS_FLOAT_DIVIDE_BY_ZERO && Code != STATUS_FLOAT_INEXACT_RESULT &&
        Code != STATUS_FLOAT_MULTIPLE_FAULTS && Code != STATUS_FLOAT_MULTIPLE_TRAPS)
        return EXCEPTION_CONTINUE_SEARCH;

    SeenCode = Code;
    SeenCount++;
    SeenCw = CtxCw(Context);
    SeenSw = CtxSw(Context);
    SeenCsr = CtxCsr(Context);
    if (SeenCount > 5)
    {
        /* Don't loop forever */
        CtxSetCw(Context, RestoreCw);
        CtxClearSw(Context);
        CtxSetCsr(Context, RestoreCsr);
        if (Code == STATUS_ILLEGAL_INSTRUCTION || Code == STATUS_ACCESS_VIOLATION)
            CtxSkip(Context, 2);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    switch (Mode)
    {
    case H_SKIP:
        break;
    case H_CLOBBER:
        /* The handler's own FPU use must not leak into the resumed code */
        SetCw(0x0360);
        SetCsr(0x0000);
        break;
    case H_SETCONTEXT:
        CtxSetCw(Context, NewCw);
        CtxSetCsr(Context, NewCsr);
        break;
    case H_CLEARFP:
        /* Re-executed with the exceptions masked (#MF and #XM are faults) */
        CtxSetCw(Context, RestoreCw);
        CtxClearSw(Context);
        CtxSetCsr(Context, RestoreCsr);
        return EXCEPTION_CONTINUE_EXECUTION;
    case H_NTCONTINUE:
        CtxSetCw(Context, NewCw);
        CtxSetCsr(Context, NewCsr);
        CtxSkip(Context, 2);
        SetCw(0x0360);
        NtContinue(Context, FALSE);
        ok(FALSE, "NtContinue returned\n");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    CtxSkip(Context, 2);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void __attribute__((noinline)) Ud2(void)
{
    __asm__ __volatile__("ud2" ::: "memory");
}

static void __attribute__((noinline)) ReadNull(void)
{
    PVOID volatile Null = NULL;
    __asm__ __volatile__("movl (%0), %%eax" : : "c" (Null) : "eax", "memory"); /* 8b 01: 2 bytes */
}

/* 1.0f / 3.0f with SSE (an inexact result) */
static void __attribute__((noinline, target("sse"))) SseDivide(void)
{
    volatile float One = 1.0f, Three = 3.0f, Result;
    __asm__ __volatile__("movss %1, %%xmm0\n\tdivss %2, %%xmm0\n\tmovss %%xmm0, %0"
                         : "=m" (Result) : "m" (One), "m" (Three) : "xmm0", "memory");
}

static volatile USHORT SpinCw;
static volatile ULONG SpinCsr;
static volatile LONG SpinReady, SpinStop, SpinBusy;

static ULONG NTAPI Spin(PVOID Parameter)
{
    SetCw(0x0e7f);
    SetCsr(0x5f80);
    SpinReady = 1;
    if (SpinBusy)
    {
        /* In user mode */
        while (!SpinStop);
    }
    else
    {
        /* Mostly in a system call */
        while (!SpinStop) Sleep(1);
    }
    SpinCw = GetCw();
    SpinCsr = GetCsr();
    return 0;
}

static HANDLE StartSpin(BOOL Busy)
{
    HANDLE Thread;

    SpinReady = SpinStop = 0;
    SpinBusy = Busy;
    Thread = CreateThread(NULL, 0, Spin, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    while (!SpinReady) Sleep(1);
    Sleep(20);
    SuspendThread(Thread);
    return Thread;
}

static void StopSpin(HANDLE Thread)
{
    ResumeThread(Thread);
    SpinStop = 1;
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
}

START_TEST(FpuContext)
{
    USHORT Cw0 = GetCw();
    ULONG Csr0 = GetCsr();
    PVOID Veh;
    HANDLE Thread;
    CONTEXT Context;
    ULONG i;

    RestoreCw = Cw0;
    RestoreCsr = Csr0;
    Veh = AddVectoredExceptionHandler(1, Handler);
    ok(Veh != NULL, "AddVectoredExceptionHandler failed\n");

    /* The exception context shows the FPU of the faulting code */
    Mode = H_SKIP;
    SeenCount = 0;
    SetCw(0x0a7f);
    SetCsr(0x3f80);
    Ud2();
    ok(SeenCw == 0x0a7f, "ud2: context FCW %04x\n", SeenCw);
    ok(SeenCsr == 0x3f80, "ud2: context MXCSR %08lx\n", SeenCsr);
    ReadNull();
    ok(SeenCw == 0x0a7f, "AV: context FCW %04x\n", SeenCw);
    ok(SeenCsr == 0x3f80, "AV: context MXCSR %08lx\n", SeenCsr);
    ok(GetCw() == 0x0a7f, "FCW after resuming: %04x\n", GetCw());
    ok(GetCsr() == 0x3f80, "MXCSR after resuming: %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);

    /* The handler's own FPU changes don't leak */
    Mode = H_CLOBBER;
    SeenCount = 0;
    for (i = 0; i < 3; i++) Ud2();
    ok(GetCw() == Cw0, "ud2, handler changed the FPU: FCW %04x\n", GetCw());
    ok(GetCsr() == Csr0, "ud2, handler changed the FPU: MXCSR %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);
    SeenCount = 0;
    ReadNull();
    ok(GetCw() == Cw0, "AV, handler changed the FPU: FCW %04x\n", GetCw());
    ok(GetCsr() == Csr0, "AV, handler changed the FPU: MXCSR %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);

    /* A context changed by the handler is applied */
    Mode = H_SETCONTEXT;
    SeenCount = 0;
    NewCw = 0x067f;
    NewCsr = 0x5f80;
    Ud2();
    ok(GetCw() == 0x067f, "Changed context: FCW %04x\n", GetCw());
    ok(GetCsr() == 0x5f80, "Changed context: MXCSR %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);

    /* NtContinue from the handler */
    Mode = H_NTCONTINUE;
    SeenCount = 0;
    NewCw = 0x0e7f;
    NewCsr = 0x3f80;
    Ud2();
    ok(GetCw() == 0x0e7f, "NtContinue: FCW %04x\n", GetCw());
    ok(GetCsr() == 0x3f80, "NtContinue: MXCSR %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);

    /* Unmasked x87 divide by zero, handled by masking it in the context */
    Mode = H_CLEARFP;
    SeenCount = 0;
    SeenCode = 0;
    {
        volatile double One = 1.0, Zero = 0.0, Result;
        SetCw(Cw0 & ~0x0004);
        __asm__ __volatile__("fldl %1\n\tfdivl %2\n\tfstpl %0\n\tfwait"
                             : "=m" (Result) : "m" (One), "m" (Zero) : "memory");
    }
    ok(SeenCount == 1, "x87: %ld exceptions\n", SeenCount);
    ok(SeenCode == STATUS_FLOAT_DIVIDE_BY_ZERO, "x87: code 0x%lx\n", SeenCode);
    ok((SeenSw & 0x8084) == 0x8084, "x87: context FSW %04x\n", SeenSw);
    ok(GetCw() == Cw0, "x87: FCW after resuming %04x\n", GetCw());
    ok(!(GetSw() & 0x80bf), "x87: FSW after resuming %04x\n", GetSw());
    __asm__ __volatile__("fnclex");
    SetCw(Cw0);
    SetCsr(Csr0);

    /* Unmasked SSE inexact result, handled by masking it in the context */
    SeenCount = 0;
    SeenCode = 0;
    SetCsr(Csr0 & ~0x1000);
    SseDivide();
    ok(SeenCount == 1, "SSE: %ld exceptions\n", SeenCount);
#ifdef _M_AMD64
    ok(SeenCode == STATUS_FLOAT_INEXACT_RESULT, "SSE: code 0x%lx\n", SeenCode);
#else
    ok(SeenCode == STATUS_FLOAT_MULTIPLE_FAULTS, "SSE: code 0x%lx\n", SeenCode);
#endif
    ok((GetCsr() & ~0x3f) == Csr0, "SSE: MXCSR after resuming %08lx\n", GetCsr());
    SetCw(Cw0);
    SetCsr(Csr0);

    RemoveVectoredExceptionHandler(Veh);

    /* Get/SetThreadContext on a thread suspended in user mode */
    Thread = StartSpin(TRUE);
    RtlZeroMemory(&Context, sizeof(Context));
#ifdef _M_AMD64
    Context.ContextFlags = CONTEXT_FULL;
#else
    Context.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT | CONTEXT_EXTENDED_REGISTERS;
#endif
    ok(GetThreadContext(Thread, &Context), "GetThreadContext failed: %lu\n", GetLastError());
    ok(CtxCw(&Context) == 0x0e7f, "GetThreadContext: FCW %04x\n", CtxCw(&Context));
    ok(CtxCsr(&Context) == 0x5f80, "GetThreadContext: MXCSR %08lx\n", CtxCsr(&Context));
#ifdef _M_IX86
    ok((USHORT)Context.FloatSave.ControlWord == 0x0e7f, "GetThreadContext: FloatSave.ControlWord %08lx\n", Context.FloatSave.ControlWord);
#endif
    CtxSetCw(&Context, 0x067f);
    CtxSetCsr(&Context, 0x3f80);
    ok(SetThreadContext(Thread, &Context), "SetThreadContext failed: %lu\n", GetLastError());
    StopSpin(Thread);
    ok(SpinCw == 0x067f, "SetThreadContext: the thread has FCW %04x\n", SpinCw);
    ok(SpinCsr == 0x3f80, "SetThreadContext: the thread has MXCSR %08lx\n", SpinCsr);

    /* CONTEXT_CONTROL leaves the FPU alone (thread in a system call) */
    Thread = StartSpin(FALSE);
    Context.ContextFlags = CONTEXT_CONTROL;
    ok(GetThreadContext(Thread, &Context), "GetThreadContext failed: %lu\n", GetLastError());
    ok(SetThreadContext(Thread, &Context), "SetThreadContext failed: %lu\n", GetLastError());
    StopSpin(Thread);
    ok(SpinCw == 0x0e7f, "SetThreadContext(CONTEXT_CONTROL): the thread has FCW %04x\n", SpinCw);
    ok(SpinCsr == 0x5f80, "SetThreadContext(CONTEXT_CONTROL): the thread has MXCSR %08lx\n", SpinCsr);
}

#else

START_TEST(FpuContext)
{
    skip("Needs GCC inline assembly on x86/x64\n");
}

#endif
