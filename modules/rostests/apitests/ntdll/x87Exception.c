/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that unmasked x87 floating point exceptions reach the program
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * An x87 instruction that hits an unmasked exception makes the next x87
 * instruction (here FWAIT) raise #MF. 64-bit ReactOS did not implement that
 * trap (KiFloatingErrorFault) and stopped in the debugger; Windows raises a
 * STATUS_FLOAT_* exception in the program. 32-bit programs (WOW64) use the
 * x87 unit a lot (AnyDesk hit it). Its 32-bit build is the interesting one.
 */

#if defined(__GNUC__) && (defined(_M_IX86) || defined(_M_AMD64))

static USHORT SavedControlWord;

static void SetControlWord(USHORT Cw)
{
    __asm__ __volatile__("fldcw %0" : : "m" (Cw));
}

static NTSTATUS DivideByZero(void)
{
    NTSTATUS Status = STATUS_SUCCESS;
    volatile double One = 1.0, Zero = 0.0, Result;

    _SEH2_TRY
    {
        /* Unmask zero divide (bit 2) only */
        SetControlWord(SavedControlWord & ~0x0004);
        __asm__ __volatile__("fldl %1\n\tfdivl %2\n\tfstpl %0\n\tfwait"
                             : "=m" (Result) : "m" (One), "m" (Zero));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    __asm__ __volatile__("fnclex");
    SetControlWord(SavedControlWord);
    return Status;
}

static NTSTATUS StackUnderflow(void)
{
    NTSTATUS Status = STATUS_SUCCESS;
    volatile double Result;

    _SEH2_TRY
    {
        /* Unmask invalid operation (bit 0); pop from an empty x87 stack */
        __asm__ __volatile__("fninit");
        SetControlWord(SavedControlWord & ~0x0001);
        __asm__ __volatile__("fstpl %0\n\tfwait" : "=m" (Result));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    __asm__ __volatile__("fninit");
    SetControlWord(SavedControlWord);
    return Status;
}

START_TEST(x87Exception)
{
    NTSTATUS Status;

    __asm__ __volatile__("fnstcw %0" : "=m" (SavedControlWord));

    Status = DivideByZero();
    ok(Status == STATUS_FLOAT_DIVIDE_BY_ZERO, "Divide by zero: got 0x%lx\n", Status);

    Status = StackUnderflow();
    ok(Status == STATUS_FLOAT_STACK_CHECK, "Stack underflow: got 0x%lx\n", Status);

    /* With the exceptions masked again, the same division just gives infinity */
    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        volatile double One = 1.0, Zero = 0.0, Result;
        __asm__ __volatile__("fldl %1\n\tfdivl %2\n\tfstpl %0\n\tfwait"
                             : "=m" (Result) : "m" (One), "m" (Zero));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok(Status == STATUS_SUCCESS, "Masked divide by zero raised 0x%lx\n", Status);
    __asm__ __volatile__("fnclex");
}

#else

START_TEST(x87Exception)
{
    skip("Needs GCC inline assembly on x86/x64\n");
}

#endif
