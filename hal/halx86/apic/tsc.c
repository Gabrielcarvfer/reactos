/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/tsc.c
 * PURPOSE:         HAL Routines for TSC handling
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "tsc.h"
#include "apicp.h"
#define NDEBUG
#include <debug.h>

LARGE_INTEGER HalpCpuClockFrequency = {{INITIAL_STALL_COUNT * 1000000}};

UCHAR TscCalibrationPhase;
ULONG64 TscCalibrationArray[NUM_SAMPLES];

#define RTC_MODE 6 /* Mode 6 is 1024 Hz */
#define SAMPLE_FREQUENCY ((32768 << 1) >> RTC_MODE)

/* PRIVATE FUNCTIONS *********************************************************/

static
ULONG64
DoLinearRegression(
    ULONG XMax,
    ULONG64 *ArrayY)
{
    ULONG X, SumXX;
    ULONG64 SumXY;

    /* Calculate the sum of the squares of X */
    SumXX = (XMax * (XMax + 1) * (2*XMax + 1)) / 6;

    /* Calculate the sum of the differences to the first value
       weighted by x */
    for (SumXY = 0, X = 1; X <= XMax; X++)
    {
         SumXY += X * (ArrayY[X] - ArrayY[0]);
    }

    /* Account for sample frequency */
    SumXY *= SAMPLE_FREQUENCY;

    /* Return the quotient of the sums */
    return (SumXY + (SumXX/2)) / SumXX;
}


/* Measure the TSC against PIT channel 2 (1193182 Hz), polling its output
 * bit, over 50 ms. The RTC calibration below only spans 4 periodic RTC
 * interrupts (~3 ms): in a virtual machine each interrupt can arrive
 * hundreds of microseconds late, which made the performance counter run
 * 30% fast or several times slow depending on the boot. Returns 0 if the
 * PIT doesn't behave. */
#define PIT_FREQUENCY 1193182
#define PIT_CALIBRATION_MS 50

static
ULONG64
HalpCalibrateTscWithPit(VOID)
{
    ULONG Latch = PIT_FREQUENCY / (1000 / PIT_CALIBRATION_MS);
    UCHAR Port61;
    ULONG64 Start, End;
    ULONG Loops = 0;

    /* Gate channel 2 on, speaker off */
    Port61 = __inbyte(0x61);
    __outbyte(0x61, (Port61 & ~0x02) | 0x01);

    /* Channel 2, low then high byte, mode 0 (output goes high at 0) */
    __outbyte(0x43, 0xB0);
    __outbyte(0x42, Latch & 0xFF);
    __outbyte(0x42, (Latch >> 8) & 0xFF);

    Start = __rdtsc();
    do
    {
        End = __rdtsc();
        Loops++;
    } while (!(__inbyte(0x61) & 0x20) && Loops < 50000000);

    __outbyte(0x61, Port61);

    /* The output never went high, or went high at once */
    if (!(__inbyte(0x61) & 0x20) && Loops >= 50000000) return 0;
    if (Loops < 2) return 0;

    return (End - Start) * (1000 / PIT_CALIBRATION_MS);
}

VOID
NTAPI
HalpInitializeTsc(VOID)
{
    ULONG_PTR Flags;
    PVOID PreviousHandler;
    UCHAR RegisterA, RegisterB;

    /* Check if the CPU supports RDTSC */
    if (!(KeGetCurrentPrcb()->FeatureBits & KF_RDTSC))
    {
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }

     /* Save flags and disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Enable the periodic interrupt in the CMOS */
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB | RTC_REG_B_PI);

    /* Modify register A to RTC_MODE to get SAMPLE_FREQUENCY */
    RegisterA = HalpReadCmos(RTC_REGISTER_A);
    RegisterA = (RegisterA & 0xF0) | RTC_MODE;
    HalpWriteCmos(RTC_REGISTER_A, RegisterA);

    /* Save old IDT entry */
    PreviousHandler = KeQueryInterruptHandler(APIC_CLOCK_VECTOR);

    /* Set the calibration ISR */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, TscCalibrationISR);

    /* Don't reset the TSC: the performance counter is the TSC, and it must
       agree between processors (the other ones keep counting from power-on,
       a thread moving to another processor would see the time jump) */

    /* Enable the timer interrupt */
    HalEnableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL, Latched);

    /* Read register C, so that the next interrupt can happen */
    HalpReadCmos(RTC_REGISTER_C);

    /* Wait for completion */
    _enable();
    while (TscCalibrationPhase < NUM_SAMPLES) _ReadWriteBarrier();
    _disable();

    /* Disable the periodic interrupt in the CMOS */
    HalpWriteCmos(RTC_REGISTER_B, RegisterB & ~RTC_REG_B_PI);

    /* Disable the timer interrupt */
    HalDisableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL);

    /* Restore the previous handler */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, PreviousHandler);

    /* Calculate an average, using simplified linear regression */
    HalpCpuClockFrequency.QuadPart = DoLinearRegression(NUM_SAMPLES - 1,
                                                        TscCalibrationArray);

    /* Prefer a longer measurement against the PIT: take it when two of them
     * agree within 0.5% */
    {
        ULONG64 Pit1 = HalpCalibrateTscWithPit();
        ULONG64 Pit2 = HalpCalibrateTscWithPit();
        if (Pit1 && Pit2 &&
            (Pit1 > Pit2 ? Pit1 - Pit2 : Pit2 - Pit1) < Pit1 / 200)
        {
            DPRINT1("TSC: %I64u Hz (PIT), %I64u Hz (RTC)\n",
                    (Pit1 + Pit2) / 2, HalpCpuClockFrequency.QuadPart);
            HalpCpuClockFrequency.QuadPart = (Pit1 + Pit2) / 2;
        }
        else
        {
            DPRINT1("TSC: PIT calibration failed (%I64u, %I64u), RTC says %I64u Hz\n",
                    Pit1, Pit2, HalpCpuClockFrequency.QuadPart);
        }
    }

    /* Restore flags */
    __writeeflags(Flags);

}

CODE_SEG("INIT")
VOID
NTAPI
HalpCalibrateStallExecution(VOID)
{
    // Timer interrupt is now active

    HalpInitializeTsc();

    KeGetPcr()->StallScaleFactor = (ULONG)(HalpCpuClockFrequency.QuadPart / 1000000);
}

/* PUBLIC FUNCTIONS ***********************************************************/

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Result;

    /* Make sure it's calibrated */
    ASSERT(HalpCpuClockFrequency.QuadPart != 0);

    /* Does the caller want the frequency? */
    if (PerformanceFrequency)
    {
        /* Return tsc frequency */
        *PerformanceFrequency = HalpCpuClockFrequency;
    }

    /* Return the current value */
    Result.QuadPart = __rdtsc();
    return Result;
}

VOID
NTAPI
KeStallExecutionProcessor(ULONG MicroSeconds)
{
    ULONG64 StartTime, EndTime;

    /* Get the initial time */
    StartTime = __rdtsc();

    /* Calculate the ending time */
    EndTime = StartTime + KeGetPcr()->StallScaleFactor * MicroSeconds;

    /* Loop until time is elapsed */
    while (__rdtsc() < EndTime);
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    IN volatile PLONG Count,
    IN ULONGLONG NewCount)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
}

