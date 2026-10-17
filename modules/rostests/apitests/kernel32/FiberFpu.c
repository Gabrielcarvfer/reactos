/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test the x87 control word and MXCSR that fibers start with and keep
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/* (not in our winbase.h) */
WINBASEAPI LPVOID WINAPI ConvertThreadToFiberEx(LPVOID lpParameter, DWORD dwFlags);
#ifndef FIBER_FLAG_FLOAT_SWITCH
#define FIBER_FLAG_FLOAT_SWITCH 0x1
#endif

/*
 * 32-bit ReactOS loaded the (empty) FPU state of a fiber created without
 * FIBER_FLAG_FLOAT_SWITCH when the current fiber had the flag, so the new
 * fiber ran with every floating point exception unmasked (AnyDesk's service
 * died of STATUS_FLOAT_INEXACT_RESULT). 64-bit ReactOS had no SwitchToFiber.
 * The expected values are those of Windows 10: on x86 a fiber without the
 * flag keeps the current state, on x64 the state is always switched and every
 * new fiber starts with the initial one.
 */

#if defined(__GNUC__) && (defined(_M_IX86) || defined(_M_AMD64))

static USHORT GetCw(void) { USHORT Cw; __asm__ __volatile__("fnstcw %0" : "=m" (Cw)); return Cw; }
static void SetCw(USHORT Cw) { __asm__ __volatile__("fldcw %0" : : "m" (Cw)); }
static ULONG GetCsr(void) { ULONG Csr; __asm__ __volatile__("stmxcsr %0" : "=m" (Csr)); return Csr; }
static void SetCsr(ULONG Csr) { __asm__ __volatile__("ldmxcsr %0" : : "m" (Csr)); }

static PVOID MainFiber, Fiber;
static volatile USHORT FiberCw, FiberCw2;
static volatile ULONG FiberCsr, FiberCsr2;
static volatile double Third;

static VOID WINAPI FiberProc(PVOID Parameter)
{
    volatile double One = 1.0, Three = 3.0;

    FiberCw = GetCw();
    FiberCsr = GetCsr();
    Third = One / Three; /* Must not fault */
    SetCw(0x067f);
    SetCsr(0x3f80);
    SwitchToFiber(MainFiber);
    FiberCw2 = GetCw();
    FiberCsr2 = GetCsr();
    SwitchToFiber(MainFiber);
}

START_TEST(FiberFpu)
{
    USHORT Cw0 = GetCw();
    ULONG Csr0 = GetCsr();
    DWORD Flags;
    ULONG i;

    MainFiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
    ok(MainFiber != NULL, "ConvertThreadToFiberEx failed: %lu\n", GetLastError());
    if (!MainFiber) return;

    for (i = 0; i < 2; i++)
    {
        Flags = i ? FIBER_FLAG_FLOAT_SWITCH : 0;
        SetCw(0x0e7f);
        SetCsr(0x5f80);
        FiberCw = FiberCw2 = 0;
        FiberCsr = FiberCsr2 = 0;
        Fiber = CreateFiberEx(0, 0, Flags, FiberProc, NULL);
        ok(Fiber != NULL, "CreateFiberEx(%lu) failed: %lu\n", Flags, GetLastError());
        if (!Fiber) continue;

        SwitchToFiber(Fiber);
#ifdef _M_IX86
        if (!Flags)
        {
            ok(FiberCw == 0x0e7f, "New fiber (flags %lu): FCW %04x\n", Flags, FiberCw);
            ok(FiberCsr == 0x5f80, "New fiber (flags %lu): MXCSR %08lx\n", Flags, FiberCsr);
        }
        else
#endif
        {
            ok(FiberCw == 0x027f, "New fiber (flags %lu): FCW %04x\n", Flags, FiberCw);
            ok(FiberCsr == 0x1f80, "New fiber (flags %lu): MXCSR %08lx\n", Flags, FiberCsr);
        }
        ok(GetCw() == 0x0e7f, "Back in the main fiber (flags %lu): FCW %04x\n", Flags, GetCw());
        ok(GetCsr() == 0x5f80, "Back in the main fiber (flags %lu): MXCSR %08lx\n", Flags, GetCsr());

        SwitchToFiber(Fiber);
#ifdef _M_IX86
        if (Flags)
#endif
        {
            ok(FiberCw2 == 0x067f, "Fiber resumed (flags %lu): FCW %04x\n", Flags, FiberCw2);
            ok(FiberCsr2 == 0x3f80, "Fiber resumed (flags %lu): MXCSR %08lx\n", Flags, FiberCsr2);
        }
        DeleteFiber(Fiber);
    }

    SetCw(Cw0);
    SetCsr(Csr0);
    ok(ConvertFiberToThread(), "ConvertFiberToThread failed: %lu\n", GetLastError());
}

#else

START_TEST(FiberFpu)
{
    skip("Needs GCC inline assembly on x86/x64\n");
}

#endif
