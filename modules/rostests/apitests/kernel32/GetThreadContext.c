/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test GetThreadContext's debug registers and context flags
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * A thread without hardware breakpoints has zero debug registers (TeraCopy
 * reports "Debugger detected" otherwise). On 64-bit Windows, context flags
 * without the CONTEXT_AMD64 bit, or with the x86 one (Delphi programs), are
 * taken as amd64 flags, and come back with CONTEXT_AMD64 (checked on
 * Windows 11).
 */

static DWORD WINAPI SleepThread(LPVOID Param)
{
    Sleep(INFINITE);
    return 0;
}

static void CheckDebugRegisters(HANDLE Thread, DWORD Flags, const char *What)
{
    CONTEXT Context;
    BOOL Ret;

    memset(&Context, 0xcc, sizeof(Context));
    Context.ContextFlags = Flags;
    SetLastError(0xdeadbeef);
    Ret = GetThreadContext(Thread, &Context);
    ok(Ret, "%s: GetThreadContext failed with %lu\n", What, GetLastError());
#ifdef _M_AMD64
    ok(Context.ContextFlags == CONTEXT_DEBUG_REGISTERS, "%s: flags 0x%lx\n", What, Context.ContextFlags);
#endif
    ok(Context.Dr0 == 0, "%s: Dr0 0x%Ix\n", What, (ULONG_PTR)Context.Dr0);
    ok(Context.Dr1 == 0, "%s: Dr1 0x%Ix\n", What, (ULONG_PTR)Context.Dr1);
    ok(Context.Dr2 == 0, "%s: Dr2 0x%Ix\n", What, (ULONG_PTR)Context.Dr2);
    ok(Context.Dr3 == 0, "%s: Dr3 0x%Ix\n", What, (ULONG_PTR)Context.Dr3);
    ok(Context.Dr7 == 0, "%s: Dr7 0x%Ix\n", What, (ULONG_PTR)Context.Dr7);
}

START_TEST(GetThreadContext)
{
    HANDLE Thread;

    CheckDebugRegisters(GetCurrentThread(), CONTEXT_DEBUG_REGISTERS, "current thread");

    Thread = CreateThread(NULL, 0, SleepThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return;
    Sleep(100);
    SuspendThread(Thread);
    CheckDebugRegisters(Thread, CONTEXT_DEBUG_REGISTERS, "waiting thread");
#ifdef _M_AMD64
    CheckDebugRegisters(Thread, 0x10, "no architecture flag");
    CheckDebugRegisters(Thread, 0x10010, "x86 flag");
#endif
    TerminateThread(Thread, 0);
    CloseHandle(Thread);
}
