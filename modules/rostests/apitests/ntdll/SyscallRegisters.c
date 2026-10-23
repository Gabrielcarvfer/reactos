/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     System calls keep the caller's registers and return real statuses
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Every system call stub of ntdll is called through an assembly harness that
 * loads the non-volatile registers (rbx, rbp, rdi, rsi, r12-r15, xmm6-15;
 * ebx, ebp, edi, esi in 32-bit code) with sentinels first. After the call
 * they and the stack pointer must be unchanged, the direction flag clear,
 * and the result a status, not the system call's number (a stale eax/rax).
 * The calls get zero arguments (NULL pointers and handles) so that they fail
 * at once; the few that can't be called that way are skipped, with a reason.
 *
 * Then the Chrome case: a thread waits, alertable, in NtWaitForSingleObject
 * while another one suspends it, gets and sets its context, and queues APCs
 * to it. The wait must return a real status (the event, STATUS_USER_APC,
 * STATUS_ALERTED). In 32-bit ReactOS programs it returned an old eax: msafd
 * took that for the end of a select() and freed its buffer while the request
 * was still pending, and the late completion corrupted the heap (Chrome
 * 155's updater).
 */

#include "precomp.h"

#if defined(__GNUC__) && (defined(_M_AMD64) || defined(_M_IX86))

#define NARGS 20

typedef struct _SYSREG_OUT
{
    ULONG_PTR Ret;
    ULONG_PTR Regs[8];      /* rbx rbp rdi rsi r12 r13 r14 r15 / ebx ebp edi esi */
    ULONG_PTR Sp, SpBefore;
    ULONG_PTR Flags;
    M128A Xmm[10];          /* xmm6-15 (64-bit) */
    PVOID Self;             /* (32-bit: where to store) */
} SYSREG_OUT, *PSYSREG_OUT;

ULONG_PTR SysregCall(PVOID Fn, ULONG_PTR *Args, PSYSREG_OUT Out);

#ifdef _M_AMD64
#define NREGS 8
static const char *RegNames[NREGS] = { "rbx", "rbp", "rdi", "rsi", "r12", "r13", "r14", "r15" };
static const ULONG_PTR Sentinel[NREGS] =
{
    0x1111111111111b01ULL, 0x2222222222222b02ULL, 0x3333333333333b03ULL, 0x4444444444444b04ULL,
    0x5555555555555b05ULL, 0x6666666666666b06ULL, 0x7777777777777b07ULL, 0x8888888888888b08ULL
};
M128A SysregXmmSentinel[10];
ULONG_PTR SysregSentinel[NREGS];

/* rcx = Fn, rdx = Args, r8 = Out */
__asm__(
    ".globl SysregCall\n"
    "SysregCall:\n"
    "  pushq %rbx\n  pushq %rbp\n  pushq %rdi\n  pushq %rsi\n"
    "  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
    /* 0x00-0x7f: arguments, 0x80-0x11f: the caller's xmm6-15, 0x120: Out, 0x128: pad */
    "  subq $0x138, %rsp\n"
    "  movdqu %xmm6, 0x80(%rsp)\n  movdqu %xmm7, 0x90(%rsp)\n  movdqu %xmm8, 0xa0(%rsp)\n"
    "  movdqu %xmm9, 0xb0(%rsp)\n  movdqu %xmm10, 0xc0(%rsp)\n movdqu %xmm11, 0xd0(%rsp)\n"
    "  movdqu %xmm12, 0xe0(%rsp)\n movdqu %xmm13, 0xf0(%rsp)\n movdqu %xmm14, 0x100(%rsp)\n"
    "  movdqu %xmm15, 0x110(%rsp)\n"
    "  movq %r8, 0x120(%rsp)\n"
    "  movq %rsp, 0x50(%r8)\n"                  /* Out->SpBefore */
    /* stack arguments 5..16 */
    "  movq %rcx, %rax\n"
    "  movq %rdx, %r11\n"
    "  movq 0x20(%r11), %r10\n movq %r10, 0x20(%rsp)\n movq 0x28(%r11), %r10\n movq %r10, 0x28(%rsp)\n"
    "  movq 0x30(%r11), %r10\n movq %r10, 0x30(%rsp)\n movq 0x38(%r11), %r10\n movq %r10, 0x38(%rsp)\n"
    "  movq 0x40(%r11), %r10\n movq %r10, 0x40(%rsp)\n movq 0x48(%r11), %r10\n movq %r10, 0x48(%rsp)\n"
    "  movq 0x50(%r11), %r10\n movq %r10, 0x50(%rsp)\n movq 0x58(%r11), %r10\n movq %r10, 0x58(%rsp)\n"
    "  movq 0x60(%r11), %r10\n movq %r10, 0x60(%rsp)\n movq 0x68(%r11), %r10\n movq %r10, 0x68(%rsp)\n"
    "  movq 0x70(%r11), %r10\n movq %r10, 0x70(%rsp)\n movq 0x78(%r11), %r10\n movq %r10, 0x78(%rsp)\n"
    "  movq 0x00(%r11), %rcx\n movq 0x08(%r11), %rdx\n movq 0x10(%r11), %r8\n movq 0x18(%r11), %r9\n"
    /* sentinels */
    "  movdqu SysregXmmSentinel+0x00(%rip), %xmm6\n  movdqu SysregXmmSentinel+0x10(%rip), %xmm7\n"
    "  movdqu SysregXmmSentinel+0x20(%rip), %xmm8\n  movdqu SysregXmmSentinel+0x30(%rip), %xmm9\n"
    "  movdqu SysregXmmSentinel+0x40(%rip), %xmm10\n movdqu SysregXmmSentinel+0x50(%rip), %xmm11\n"
    "  movdqu SysregXmmSentinel+0x60(%rip), %xmm12\n movdqu SysregXmmSentinel+0x70(%rip), %xmm13\n"
    "  movdqu SysregXmmSentinel+0x80(%rip), %xmm14\n movdqu SysregXmmSentinel+0x90(%rip), %xmm15\n"
    "  movq SysregSentinel+0x00(%rip), %rbx\n  movq SysregSentinel+0x08(%rip), %rbp\n"
    "  movq SysregSentinel+0x10(%rip), %rdi\n  movq SysregSentinel+0x18(%rip), %rsi\n"
    "  movq SysregSentinel+0x20(%rip), %r12\n  movq SysregSentinel+0x28(%rip), %r13\n"
    "  movq SysregSentinel+0x30(%rip), %r14\n  movq SysregSentinel+0x38(%rip), %r15\n"
    "  call *%rax\n"
    /* (a changed rsp makes Out unreachable: the test then crashes) */
    "  movq 0x120(%rsp), %r11\n"
    "  movq %rax, 0x00(%r11)\n"
    "  movq %rbx, 0x08(%r11)\n  movq %rbp, 0x10(%r11)\n  movq %rdi, 0x18(%r11)\n  movq %rsi, 0x20(%r11)\n"
    "  movq %r12, 0x28(%r11)\n  movq %r13, 0x30(%r11)\n  movq %r14, 0x38(%r11)\n  movq %r15, 0x40(%r11)\n"
    "  movq %rsp, 0x48(%r11)\n"
    "  pushfq\n  popq %rcx\n  movq %rcx, 0x58(%r11)\n"
    "  movdqu %xmm6, 0x60(%r11)\n  movdqu %xmm7, 0x70(%r11)\n  movdqu %xmm8, 0x80(%r11)\n"
    "  movdqu %xmm9, 0x90(%r11)\n  movdqu %xmm10, 0xa0(%r11)\n movdqu %xmm11, 0xb0(%r11)\n"
    "  movdqu %xmm12, 0xc0(%r11)\n movdqu %xmm13, 0xd0(%r11)\n movdqu %xmm14, 0xe0(%r11)\n"
    "  movdqu %xmm15, 0xf0(%r11)\n"
    "  cld\n"
    "  movdqu 0x80(%rsp), %xmm6\n  movdqu 0x90(%rsp), %xmm7\n  movdqu 0xa0(%rsp), %xmm8\n"
    "  movdqu 0xb0(%rsp), %xmm9\n  movdqu 0xc0(%rsp), %xmm10\n movdqu 0xd0(%rsp), %xmm11\n"
    "  movdqu 0xe0(%rsp), %xmm12\n movdqu 0xf0(%rsp), %xmm13\n movdqu 0x100(%rsp), %xmm14\n"
    "  movdqu 0x110(%rsp), %xmm15\n"
    "  addq $0x138, %rsp\n"
    "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n"
    "  popq %rsi\n  popq %rdi\n  popq %rbp\n  popq %rbx\n"
    "  ret\n"
);
#else
#define NREGS 4
static const char *RegNames[NREGS] = { "ebx", "ebp", "edi", "esi" };
static const ULONG_PTR Sentinel[NREGS] = { 0x11111b01, 0x22222b02, 0x33333b03, 0x44444b04 };
ULONG_PTR SysregSentinel[NREGS];
PSYSREG_OUT SysregOut;     /* (one thread at a time) */
ULONG_PTR SysregEsp;

/* cdecl: [esp+4] = Fn, [esp+8] = Args, [esp+12] = Out */
__asm__(
    ".globl _SysregCall\n"
    "_SysregCall:\n"
    "  pushl %ebp\n  pushl %ebx\n  pushl %esi\n  pushl %edi\n"
    "  movl 20(%esp), %eax\n"                   /* Fn */
    "  movl 24(%esp), %ecx\n"                   /* Args */
    "  movl 28(%esp), %edx\n"                   /* Out */
    "  movl %edx, _SysregOut\n"
    "  movl %esp, _SysregEsp\n"
    "  movl %esp, 0x28(%edx)\n"                 /* Out->SpBefore */
    /* 20 stdcall arguments, the first one last */
    "  pushl 76(%ecx)\n pushl 72(%ecx)\n pushl 68(%ecx)\n pushl 64(%ecx)\n"
    "  pushl 60(%ecx)\n pushl 56(%ecx)\n pushl 52(%ecx)\n pushl 48(%ecx)\n"
    "  pushl 44(%ecx)\n pushl 40(%ecx)\n pushl 36(%ecx)\n pushl 32(%ecx)\n"
    "  pushl 28(%ecx)\n pushl 24(%ecx)\n pushl 20(%ecx)\n pushl 16(%ecx)\n"
    "  pushl 12(%ecx)\n pushl 8(%ecx)\n  pushl 4(%ecx)\n  pushl 0(%ecx)\n"
    "  movl _SysregSentinel+0, %ebx\n  movl _SysregSentinel+4, %ebp\n"
    "  movl _SysregSentinel+8, %edi\n  movl _SysregSentinel+12, %esi\n"
    "  call *%eax\n"
    "  movl _SysregOut, %ecx\n"
    "  movl %eax, 0x00(%ecx)\n"
    "  movl %ebx, 0x04(%ecx)\n  movl %ebp, 0x08(%ecx)\n  movl %edi, 0x0c(%ecx)\n  movl %esi, 0x10(%ecx)\n"
    /* (the stub popped its own arguments: how many is not known here) */
    "  movl %esp, 0x24(%ecx)\n"
    "  pushfl\n  popl %edx\n  movl %edx, 0x2c(%ecx)\n"
    "  cld\n"
    "  movl _SysregEsp, %esp\n"
    "  popl %edi\n  popl %esi\n  popl %ebx\n  popl %ebp\n"
    "  ret\n"
);
#endif

/* The system call number of an ntdll stub, or -1 if it is no stub */
static LONG StubNumber(PUCHAR Code)
{
#ifdef _M_AMD64
    if (Code[0] == 0x4C && Code[1] == 0x8B && Code[2] == 0xD1 && Code[3] == 0xB8)
        return *(LONG UNALIGNED *)&Code[4];
#else
    if (Code[0] == 0xB8)
        return *(LONG UNALIGNED *)&Code[1];
#endif
    return -1;
}

/* Can't be called with zero arguments, and why */
static const struct { const char *Name; const char *Reason; } Skips[] =
{
    { "NtTerminateProcess", "NULL ends this process's other threads" },
    { "NtTerminateThread", "NULL ends this thread" },
    { "NtContinue", "takes over the thread" },
    { "NtContinueEx", "takes over the thread" },
    { "NtRaiseException", "raises in this thread" },
    { "NtCallbackReturn", "returns from a callback that does not exist" },
    { "NtWaitForAlertByThreadId", "waits until alerted" },
    { "NtWaitForKeyedEvent", "NULL is the global keyed event: waits" },
    { "NtReleaseKeyedEvent", "NULL is the global keyed event: waits for a waiter" },
    { "NtShutdownSystem", "shuts down if privileged" },
    { "NtSetSystemPowerState", "changes the power state" },
    { "NtInitiatePowerAction", "changes the power state" },
    { "NtRaiseHardError", "may show a message box" },
    { "NtSystemDebugControl", "debugger control" },
    { "NtVdmControl", "VDM control" },
    { "NtSuspendProcess", "NULL handle semantics differ" },
    { "NtSetSystemTime", "may change the clock" },
    { "NtTestAlert", "delivers this thread's APCs" },
    { "NtYieldExecution", "returns STATUS_NO_YIELD_PERFORMED, nothing to check" },
};

static const char *SkipReason(const char *Name)
{
    ULONG i;
    for (i = 0; i < _countof(Skips); i++)
        if (!strcmp(Name, Skips[i].Name)) return Skips[i].Reason;
    return NULL;
}

/* Checks the harness's output; returns TRUE if all is well */
static BOOLEAN CheckOut(const char *Name, LONG Number, PSYSREG_OUT Out, BOOLEAN Report)
{
    BOOLEAN Good = TRUE;
    ULONG i;

    for (i = 0; i < NREGS; i++)
    {
        if (Out->Regs[i] != Sentinel[i])
        {
            Good = FALSE;
            if (Report) ok(0, "%s: %s changed to %Ix\n", Name, RegNames[i], Out->Regs[i]);
        }
    }
#ifdef _M_AMD64
    for (i = 0; i < 10; i++)
    {
        if (memcmp(&Out->Xmm[i], &SysregXmmSentinel[i], sizeof(M128A)))
        {
            Good = FALSE;
            if (Report) ok(0, "%s: xmm%lu changed\n", Name, i + 6);
        }
    }
    if (Out->Sp != Out->SpBefore)
    {
        Good = FALSE;
        if (Report) ok(0, "%s: rsp %Ix, was %Ix\n", Name, Out->Sp, Out->SpBefore);
    }
#else
    /* stdcall: the stub pops at most the 20 arguments */
    if (Out->Sp < Out->SpBefore - NARGS * 4 || Out->Sp > Out->SpBefore)
    {
        Good = FALSE;
        if (Report) ok(0, "%s: esp %Ix, before the arguments %Ix\n", Name, Out->Sp, Out->SpBefore);
    }
#endif
    if (Out->Flags & 0x400)
    {
        Good = FALSE;
        if (Report) ok(0, "%s: the direction flag is set\n", Name);
    }
    if (Number >= 0 && (ULONG)Out->Ret == (ULONG)Number && Number != 0)
    {
        Good = FALSE;
        if (Report) ok(0, "%s: returned its own number %lx (a stale eax)\n", Name, Number);
    }
    return Good;
}

/* ALL THE STUBS **************************************************************/

typedef struct
{
    const char *Names[2048];
    PVOID Fns[2048];
    LONG Numbers[2048];
    ULONG Count;
    volatile LONG Next;      /* the call being made */
    volatile LONG Done;
} STUB_LIST;

static STUB_LIST Stubs;

static DWORD WINAPI CallStubs(PVOID Param)
{
    ULONG_PTR Args[NARGS] = { 0 };
    SYSREG_OUT Out;
    LONG i;

    while ((i = Stubs.Next) < (LONG)Stubs.Count)
    {
        RtlZeroMemory(&Out, sizeof(Out));
        SysregCall(Stubs.Fns[i], Args, &Out);
        CheckOut(Stubs.Names[i], Stubs.Numbers[i], &Out, TRUE);
        InterlockedIncrement(&Stubs.Done);
        InterlockedCompareExchange(&Stubs.Next, i + 1, i);
    }
    return 0;
}

static void TestAllStubs(void)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PIMAGE_NT_HEADERS Nt = RtlImageNtHeader(Ntdll);
    PIMAGE_EXPORT_DIRECTORY Exports;
    PULONG Names, Functions;
    PUSHORT Ordinals;
    ULONG i, Skipped = 0, Blocked = 0;
    HANDLE Thread;
    LONG Last;

    Exports = (PVOID)((PUCHAR)Ntdll + Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    Names = (PULONG)((PUCHAR)Ntdll + Exports->AddressOfNames);
    Functions = (PULONG)((PUCHAR)Ntdll + Exports->AddressOfFunctions);
    Ordinals = (PUSHORT)((PUCHAR)Ntdll + Exports->AddressOfNameOrdinals);

    for (i = 0; i < Exports->NumberOfNames && Stubs.Count < _countof(Stubs.Names); i++)
    {
        const char *Name = (const char *)Ntdll + Names[i];
        PUCHAR Code = (PUCHAR)Ntdll + Functions[Ordinals[i]];
        const char *Reason;
        LONG Number;

        if (Name[0] != 'N' || Name[1] != 't') continue;
        Number = StubNumber(Code);
        if (Number < 0) continue;
        if ((Reason = SkipReason(Name)))
        {
            trace("skipped %s: %s\n", Name, Reason);
            Skipped++;
            continue;
        }
        Stubs.Names[Stubs.Count] = Name;
        Stubs.Fns[Stubs.Count] = Code;
        Stubs.Numbers[Stubs.Count] = Number;
        Stubs.Count++;
    }
    ok(Stubs.Count > 100, "only %lu system call stubs found\n", Stubs.Count);

    /* One thread calls them; a call that blocks is reported and passed over */
    Thread = CreateThread(NULL, 0, CallStubs, NULL, 0, NULL);
    while (WaitForSingleObject(Thread, 3000) == WAIT_TIMEOUT)
    {
        Last = Stubs.Next;
        Sleep(2000);
        if (Stubs.Next == Last && Last < (LONG)Stubs.Count)
        {
            trace("%s blocks with zero arguments\n", Stubs.Names[Last]);
            Blocked++;
            InterlockedCompareExchange(&Stubs.Next, Last + 1, Last);
            CloseHandle(Thread);
            Thread = CreateThread(NULL, 0, CallStubs, NULL, 0, NULL);
        }
    }
    CloseHandle(Thread);
    trace("%ld system calls checked, %lu skipped, %lu blocked\n", Stubs.Done, Skipped, Blocked);
}

/* THE CHROME CASE ************************************************************/

static HANDLE WaitEvent;
static volatile LONG StopWaiting, BadStatus, BadRegs, Waits, Apcs;

static VOID NTAPI CountApc(ULONG_PTR Param)
{
    InterlockedIncrement(&Apcs);
}

static DWORD WINAPI Waiter(PVOID Param)
{
    PVOID Wait = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtWaitForSingleObject");
    LONG Number = StubNumber(Wait);
    ULONG_PTR Args[NARGS] = { 0 };
    LARGE_INTEGER Timeout;
    SYSREG_OUT Out;
    NTSTATUS Status;

    Timeout.QuadPart = -10 * 1000 * 5; /* 5 ms */
    Args[0] = (ULONG_PTR)WaitEvent;
    Args[1] = TRUE;
    Args[2] = (ULONG_PTR)&Timeout;
    while (!StopWaiting)
    {
        RtlZeroMemory(&Out, sizeof(Out));
        SysregCall(Wait, Args, &Out);
        Status = (NTSTATUS)Out.Ret;
        InterlockedIncrement(&Waits);
        if (Status != STATUS_SUCCESS && Status != STATUS_TIMEOUT &&
            Status != STATUS_USER_APC && Status != STATUS_ALERTED)
        {
            if (InterlockedIncrement(&BadStatus) <= 5)
                ok(0, "the wait returned %lx (its number is %lx)\n", Status, Number);
        }
        if (!CheckOut("NtWaitForSingleObject (context changed meanwhile)", Number, &Out, BadRegs < 5))
            InterlockedIncrement(&BadRegs);
    }
    return 0;
}

static void TestContextDuringWait(void)
{
    HANDLE Thread;
    CONTEXT Context;
    DWORD Start;
    ULONG Sets = 0;

    WaitEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, Waiter, NULL, 0, NULL);

    /* (once it waits: the start of a thread is not what this is about) */
    Start = GetTickCount();
    while (!Waits && GetTickCount() - Start < 5000)
        Sleep(10);

    Start = GetTickCount();
    while (GetTickCount() - Start < 10000)
    {
        if (SuspendThread(Thread) != (DWORD)-1)
        {
            Context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(Thread, &Context))
            {
                /* Unchanged: the thread must not notice */
                if (SetThreadContext(Thread, &Context)) Sets++;
            }
            ResumeThread(Thread);
        }
        if (!(Sets & 3)) QueueUserAPC(CountApc, Thread, 0);
        if (!(Sets & 7)) SetEvent(WaitEvent);
        /* (let the waiter get to its next wait now and then) */
        Sleep((Sets & 15) ? 0 : 1);
    }
    StopWaiting = TRUE;
    SetEvent(WaitEvent);
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "the waiting thread hangs\n");
    CloseHandle(Thread);
    CloseHandle(WaitEvent);

    ok(BadStatus == 0, "%ld of %ld waits returned a status that a wait can't return\n", BadStatus, Waits);
    ok(BadRegs == 0, "%ld of %ld waits changed registers\n", BadRegs, Waits);
    ok(Sets > 100, "only %lu contexts set\n", Sets);
    trace("%ld waits, %lu contexts set, %ld APCs\n", Waits, Sets, Apcs);
}

START_TEST(SyscallRegisters)
{
    ULONG i;

    for (i = 0; i < NREGS; i++) SysregSentinel[i] = Sentinel[i];
#ifdef _M_AMD64
    for (i = 0; i < 10; i++)
    {
        SysregXmmSentinel[i].Low = 0x0123456789abcdefULL + i;
        SysregXmmSentinel[i].High = (LONG64)(0xfedcba9876543210ULL - i);
    }
#endif
    TestContextDuringWait();
    TestAllStubs();
}

#else

START_TEST(SyscallRegisters)
{
    skip("needs GCC and x86 or x64\n");
}

#endif
