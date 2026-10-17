/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test setting the context of a suspended thread
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * A thread is suspended, gets a new instruction pointer (and maybe new values
 * in every general register) with SetThreadContext, and is resumed. It must
 * come out at the new address with all the registers as set. Cygwin/MSYS2
 * deliver signals this way (Git for Windows' bash). 64-bit ReactOS left system
 * calls with sysretq, which reloads only RIP, RSP and EFLAGS, and WOW64 put the
 * call's status in EAX.
 */

#if defined(__GNUC__)

#ifdef _WIN64
#define NREG 16
static const char *Names[NREG] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
static const BOOLEAN NonVolatile[NREG] = { 0,0,0,1, 0,1,1,1, 0,0,0,0, 1,1,1,1 };
ULONG64 SetCtxSaved[NREG];
HANDLE SetCtxEvent;
volatile LONG SetCtxWaitReturned;
void SetCtxLanding(void);
void SetCtxBusyLoop(void);
void SetCtxWaitWithRegs(void);
__asm__(
    /* Store every register, end the thread */
    ".globl SetCtxLanding\n"
    "SetCtxLanding:\n"
    "  movq %rax, SetCtxSaved+0(%rip)\n  movq %rcx, SetCtxSaved+8(%rip)\n"
    "  movq %rdx, SetCtxSaved+16(%rip)\n movq %rbx, SetCtxSaved+24(%rip)\n"
    "  movq %rsp, SetCtxSaved+32(%rip)\n movq %rbp, SetCtxSaved+40(%rip)\n"
    "  movq %rsi, SetCtxSaved+48(%rip)\n movq %rdi, SetCtxSaved+56(%rip)\n"
    "  movq %r8, SetCtxSaved+64(%rip)\n  movq %r9, SetCtxSaved+72(%rip)\n"
    "  movq %r10, SetCtxSaved+80(%rip)\n movq %r11, SetCtxSaved+88(%rip)\n"
    "  movq %r12, SetCtxSaved+96(%rip)\n movq %r13, SetCtxSaved+104(%rip)\n"
    "  movq %r14, SetCtxSaved+112(%rip)\n movq %r15, SetCtxSaved+120(%rip)\n"
    "  andq $-16, %rsp\n  subq $32, %rsp\n  xorl %ecx, %ecx\n  call ExitThread\n"
    /* Spin in user code with register i = 0x5a01 + i */
    ".globl SetCtxBusyLoop\n"
    "SetCtxBusyLoop:\n"
    "  movq $0x5a01, %rax\n  movq $0x5a02, %rcx\n  movq $0x5a03, %rdx\n  movq $0x5a04, %rbx\n"
    "  movq $0x5a06, %rbp\n  movq $0x5a07, %rsi\n  movq $0x5a08, %rdi\n"
    "  movq $0x5a09, %r8\n  movq $0x5a0a, %r9\n  movq $0x5a0b, %r10\n  movq $0x5a0c, %r11\n"
    "  movq $0x5a0d, %r12\n  movq $0x5a0e, %r13\n  movq $0x5a0f, %r14\n  movq $0x5a10, %r15\n"
    "1: jmp 1b\n"
    /* Wait in NtWaitForSingleObject (its stub keeps the non-volatile
       registers as they are) with non-volatile register i = 0x5a01 + i */
    ".globl SetCtxWaitWithRegs\n"
    "SetCtxWaitWithRegs:\n"
    "  subq $40, %rsp\n"
    "  movq $0x5a04, %rbx\n  movq $0x5a06, %rbp\n  movq $0x5a07, %rsi\n  movq $0x5a08, %rdi\n"
    "  movq $0x5a0d, %r12\n  movq $0x5a0e, %r13\n  movq $0x5a0f, %r14\n  movq $0x5a10, %r15\n"
    "  movq SetCtxEvent(%rip), %rcx\n  xorl %edx, %edx\n  xorl %r8d, %r8d\n  call NtWaitForSingleObject\n"
    "  movl $1, SetCtxWaitReturned(%rip)\n"
    "2: jmp 2b\n");
#else
#define NREG 8
static const char *Names[NREG] = { "eax","ecx","edx","ebx","esp","ebp","esi","edi" };
static const BOOLEAN NonVolatile[NREG] = { 0,0,0,1, 0,1,1,1 };
ULONG SetCtxSaved[NREG];
HANDLE SetCtxEvent;
volatile LONG SetCtxWaitReturned;
void SetCtxLanding(void);
void SetCtxBusyLoop(void);
void SetCtxWaitWithRegs(void);
__asm__(
    ".globl _SetCtxLanding\n"
    "_SetCtxLanding:\n"
    "  movl %eax, _SetCtxSaved+0\n  movl %ecx, _SetCtxSaved+4\n"
    "  movl %edx, _SetCtxSaved+8\n  movl %ebx, _SetCtxSaved+12\n"
    "  movl %esp, _SetCtxSaved+16\n movl %ebp, _SetCtxSaved+20\n"
    "  movl %esi, _SetCtxSaved+24\n movl %edi, _SetCtxSaved+28\n"
    "  andl $-16, %esp\n  pushl $0\n  call _ExitThread@4\n"
    ".globl _SetCtxBusyLoop\n"
    "_SetCtxBusyLoop:\n"
    "  movl $0x5a01, %eax\n  movl $0x5a02, %ecx\n  movl $0x5a03, %edx\n  movl $0x5a04, %ebx\n"
    "  movl $0x5a06, %ebp\n  movl $0x5a07, %esi\n  movl $0x5a08, %edi\n"
    "1: jmp 1b\n"
    ".globl _SetCtxWaitWithRegs\n"
    "_SetCtxWaitWithRegs:\n"
    "  movl $0x5a04, %ebx\n  movl $0x5a06, %ebp\n  movl $0x5a07, %esi\n  movl $0x5a08, %edi\n"
    "  pushl $0\n  pushl $0\n  pushl _SetCtxEvent\n  call _NtWaitForSingleObject@12\n"
    "  movl $1, _SetCtxWaitReturned\n"
    "2: jmp 2b\n");
#endif

static
ULONG_PTR
GetReg(PCONTEXT Context, ULONG i)
{
#ifdef _WIN64
    return (&Context->Rax)[i]; /* Rax to R15 follow each other in this order */
#else
    switch (i)
    {
        case 0: return Context->Eax;
        case 1: return Context->Ecx;
        case 2: return Context->Edx;
        case 3: return Context->Ebx;
        case 4: return Context->Esp;
        case 5: return Context->Ebp;
        case 6: return Context->Esi;
        default: return Context->Edi;
    }
#endif
}

static
VOID
SetReg(PCONTEXT Context, ULONG i, ULONG_PTR Value)
{
#ifdef _WIN64
    (&Context->Rax)[i] = Value;
#else
    switch (i)
    {
        case 0: Context->Eax = Value; break;
        case 1: Context->Ecx = Value; break;
        case 2: Context->Edx = Value; break;
        case 3: Context->Ebx = Value; break;
        case 4: Context->Esp = Value; break;
        case 5: Context->Ebp = Value; break;
        case 6: Context->Esi = Value; break;
        default: Context->Edi = Value; break;
    }
#endif
}

/* Suspend the thread, read its context, check the registers it is known to
   have and point the context at SetCtxLanding */
static
BOOLEAN
Redirect(HANDLE Thread, PCONTEXT Context, const BOOLEAN *Known, PCSTR Case)
{
    ULONG i;

    ok(SuspendThread(Thread) != (DWORD)-1, "%s: SuspendThread failed: %lu\n", Case, GetLastError());
    Context->ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(Thread, Context))
    {
        ok(0, "%s: GetThreadContext failed: %lu\n", Case, GetLastError());
        return FALSE;
    }
    for (i = 0; i < NREG; i++)
    {
        if (!Known[i])
            continue;
        ok(GetReg(Context, i) == (ULONG_PTR)(0x5a01 + i), "%s: read %s = %p, expected %p\n",
           Case, Names[i], (PVOID)GetReg(Context, i), (PVOID)(ULONG_PTR)(0x5a01 + i));
    }
#ifdef _WIN64
    Context->Rip = (ULONG_PTR)SetCtxLanding;
#else
    Context->Eip = (ULONG_PTR)SetCtxLanding;
#endif
    return TRUE;
}

static
VOID
CheckLanded(PCONTEXT Context, BOOLEAN ControlOnly, PCSTR Case)
{
    ULONG i;

    for (i = 0; i < NREG; i++)
    {
        /* With CONTEXT_CONTROL only the stack pointer is known */
        if (ControlOnly && i != 4)
            continue;
        ok(SetCtxSaved[i] == GetReg(Context, i), "%s: %s = %p, expected %p\n", Case, Names[i],
           (PVOID)SetCtxSaved[i], (PVOID)GetReg(Context, i));
    }
}

/* A thread blocked in a wait gets new values in every register (or only a new
   instruction and stack pointer) */
static
VOID
TestWaitNewRegisters(BOOLEAN AllRegisters)
{
    static const BOOLEAN None[NREG] = { 0 };
    PCSTR Case = AllRegisters ? "wait, full context" : "wait, control context";
    HANDLE Thread;
    CONTEXT Context;
    PVOID Stack;
    ULONG i;

    Stack = VirtualAlloc(NULL, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    ok(Stack != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    SetCtxEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    SetCtxWaitReturned = 0;
    RtlZeroMemory(SetCtxSaved, sizeof(SetCtxSaved));
    Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SetCtxWaitWithRegs, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Stack || !SetCtxEvent || !Thread)
        return;

    /* Let it block in the wait */
    Sleep(300);
    if (Redirect(Thread, &Context, None, Case))
    {
        for (i = 0; i < NREG; i++)
            SetReg(&Context, i, (ULONG_PTR)0x1100 * (i + 1) + 0x11);
        SetReg(&Context, 4, (ULONG_PTR)Stack + 0x8000 - sizeof(PVOID));
        if (!AllRegisters)
            Context.ContextFlags = CONTEXT_CONTROL;
        ok(SetThreadContext(Thread, &Context), "SetThreadContext failed: %lu\n", GetLastError());
        ResumeThread(Thread);
        Sleep(100);
        SetEvent(SetCtxEvent);

        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: the thread did not end\n", Case);
        ok(SetCtxWaitReturned == 0, "%s: the thread went on after its wait\n", Case);
        CheckLanded(&Context, !AllRegisters, Case);
    }

    TerminateThread(Thread, 0);
    CloseHandle(Thread);
    CloseHandle(SetCtxEvent);
    VirtualFree(Stack, 0, MEM_RELEASE);
}

/* A thread blocked in a wait: change only its instruction pointer. Its
   non-volatile registers must be read correctly and survive (Cygwin) */
static
VOID
TestWaitReadBack(VOID)
{
    PCSTR Case = "wait, read back";
    HANDLE Thread;
    CONTEXT Context;

    SetCtxEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    SetCtxWaitReturned = 0;
    RtlZeroMemory(SetCtxSaved, sizeof(SetCtxSaved));
    Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SetCtxWaitWithRegs, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!SetCtxEvent || !Thread)
        return;

    Sleep(300);
    if (Redirect(Thread, &Context, NonVolatile, Case))
    {
        ok(SetThreadContext(Thread, &Context), "SetThreadContext failed: %lu\n", GetLastError());
        ResumeThread(Thread);
        Sleep(100);
        SetEvent(SetCtxEvent);
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: the thread did not end\n", Case);
        ok(SetCtxWaitReturned == 0, "%s: the thread went on after its wait\n", Case);
        CheckLanded(&Context, FALSE, Case);
    }

    TerminateThread(Thread, 0);
    CloseHandle(Thread);
    CloseHandle(SetCtxEvent);
}

/* A thread running user code: change only its instruction pointer. Every
   register must be read correctly and survive (Cygwin) */
static
VOID
TestBusyReadBack(VOID)
{
    BOOLEAN AllButSp[NREG];
    PCSTR Case = "busy, read back";
    HANDLE Thread;
    CONTEXT Context;
    ULONG i;

    for (i = 0; i < NREG; i++)
        AllButSp[i] = (i != 4);
    RtlZeroMemory(SetCtxSaved, sizeof(SetCtxSaved));
    Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SetCtxBusyLoop, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;

    Sleep(300);
    if (Redirect(Thread, &Context, AllButSp, Case))
    {
        ok(SetThreadContext(Thread, &Context), "SetThreadContext failed: %lu\n", GetLastError());
        ResumeThread(Thread);
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: the thread did not end\n", Case);
        CheckLanded(&Context, FALSE, Case);
    }

    TerminateThread(Thread, 0);
    CloseHandle(Thread);
}

START_TEST(SetContextInSyscall)
{
    TestWaitNewRegisters(TRUE);
    TestWaitNewRegisters(FALSE);
    TestWaitReadBack();
    TestBusyReadBack();
}

#else

START_TEST(SetContextInSyscall)
{
    skip("Needs GCC inline assembly\n");
}

#endif
