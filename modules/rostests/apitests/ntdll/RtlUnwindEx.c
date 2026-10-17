/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test RtlUnwindEx with a collided unwind (x64)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * LLVM's libunwind (and GCC's libgcc) unwind C++ exceptions with RtlUnwindEx.
 * When the language handler of a frame with a cleanup or catch landing pad is
 * called during the unwind, it starts a new RtlUnwindEx to that landing pad,
 * from inside the handler. The new unwind collides with the running one and
 * calls the handler of that frame again, which, being the target, sets RDX
 * (the landing pad's selector) in DispatcherContext->ContextRecord.
 *
 * ReactOS did not flag that second call as the target unwind, and gave it the
 * caller's context instead of the one it restores. The landing pad got a wrong
 * selector: Krita 6 (OpenColorIO) ran its cleanup code and called
 * _Unwind_Resume again, forever.
 */

#ifdef _M_AMD64

#define STATUS_TEST_THROW  0xE0544854
#define STATUS_TEST_UNWIND 0xE0544855

ULONG64 CallWithUnwindHandler(VOID (*Callback)(PVOID EstablisherFrame), PULONG64 Results);
VOID CollidedLandingPad(VOID);

static ULONG HandlerCalls, TargetCalls, CollidedCalls, CollidedTargetCalls;

EXCEPTION_DISPOSITION
CollidedTestHandler(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _In_ PDISPATCHER_CONTEXT DispatcherContext)
{
    CONTEXT NewContext;

    /* (A broken unwind could call us endlessly) */
    if (++HandlerCalls > 10)
        return ExceptionContinueSearch;
    if (!(ExceptionRecord->ExceptionFlags & EXCEPTION_UNWINDING))
        return ExceptionContinueSearch;
    if (ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND)
        TargetCalls++;

    if (ExceptionRecord->ExceptionCode == STATUS_TEST_UNWIND)
    {
        /* Our own unwind to the landing pad, after the collision */
        if (ExceptionRecord->ExceptionFlags & EXCEPTION_COLLIDED_UNWIND)
            CollidedCalls++;
        if (ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND)
        {
            CollidedTargetCalls++;
            DispatcherContext->ContextRecord->Rdx = ExceptionRecord->ExceptionInformation[3];
        }
        return ExceptionContinueSearch;
    }

    /* As libunwind does for a landing pad: unwind to it, from here */
    ExceptionRecord->ExceptionCode = STATUS_TEST_UNWIND;
    ExceptionRecord->NumberParameters = 4;
    ExceptionRecord->ExceptionInformation[3] = 0x1234;
    RtlUnwindEx(EstablisherFrame, (PVOID)CollidedLandingPad, ExceptionRecord,
                (PVOID)0x5678, &NewContext, DispatcherContext->HistoryTable);
    ok(0, "RtlUnwindEx returned\n");
    return ExceptionContinueSearch;
}

static VOID
UnwindToFrame(PVOID Frame)
{
    EXCEPTION_RECORD Record;
    CONTEXT Context;

    /* As libunwind's _Unwind_Resume does: unwind to the frame that catches */
    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = STATUS_TEST_THROW;
    RtlUnwindEx(Frame, (PVOID)CollidedLandingPad, &Record, NULL, &Context, NULL);
    ok(0, "RtlUnwindEx returned\n");
}

START_TEST(RtlUnwindEx)
{
    ULONG64 Results[2] = { 0, 0 };
    ULONG64 Landed;

    Landed = CallWithUnwindHandler(UnwindToFrame, Results);
    ok(Landed == 1, "Landed = %I64u\n", Landed);
    ok(Results[0] == 0x5678, "RAX = 0x%I64x\n", Results[0]);
    ok(Results[1] == 0x1234, "RDX = 0x%I64x (the handler's value for the landing pad)\n", Results[1]);
    ok(HandlerCalls == 2, "HandlerCalls = %lu\n", HandlerCalls);
    ok(TargetCalls == 2, "TargetCalls = %lu\n", TargetCalls);
    ok(CollidedCalls == 1, "CollidedCalls = %lu\n", CollidedCalls);
    ok(CollidedTargetCalls == 1, "CollidedTargetCalls = %lu\n", CollidedTargetCalls);
}

#else

START_TEST(RtlUnwindEx)
{
    skip("x64 only\n");
}

#endif
