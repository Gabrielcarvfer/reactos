/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Slim Reader/Writer (SRW) Routines
 * PROGRAMMERS:       Thomas Weidenmueller <w3seek@reactos.com> (first version)
 *                    Algorithm from Wine's ntdll (Sebastian Lackner, LGPL-2.1+)
 *
 * NOTES:
 * The lock is one 32-bit counter word in the pointer (0 = free):
 *   bit 31      an exclusive owner holds the lock (IN_EXCLUSIVE)
 *   bits 16-30  exclusive owner + exclusive waiters
 *   bits 0-15   shared owners + shared waiters
 * Waiters sleep on the process-wide keyed event (the NULL handle): exclusive
 * ones on the lock's address, shared ones on the address + 2. Releasing a
 * keyed event waits for its waiter to arrive, so no wakeup is ever lost and
 * nothing lives on a waiter's stack.
 *
 * The previous version queued wait blocks on the waiters' stacks and was
 * racy on multiprocessor machines: RtlReleaseSRWLockExclusive could find a
 * shared wait block whose wake chain was NULL and crash (seen in Flutter
 * programs such as RustDesk).
 */

/* INCLUDES *****************************************************************/

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * Spin-waiting helper (used by condvar.c): spin a little, then give the CPU
 * away, eventually sleeping so that threads of lower priority run too.
 */
VOID
NTAPI
RtlpSrwBackoff(IN OUT PULONG Spins)
{
    LARGE_INTEGER Interval;

    if (++*Spins < 64)
    {
        YieldProcessor();
    }
    else if (*Spins < 128)
    {
        NtYieldExecution();
    }
    else
    {
        Interval.QuadPart = -10000; /* 1 ms */
        NtDelayExecution(FALSE, &Interval);
    }
}

#define SRWLOCK_MASK_IN_EXCLUSIVE     0x80000000
#define SRWLOCK_MASK_EXCLUSIVE_QUEUE  0x7fff0000
#define SRWLOCK_MASK_SHARED_QUEUE     0x0000ffff
#define SRWLOCK_RES_EXCLUSIVE         0x00010000
#define SRWLOCK_RES_SHARED            0x00000001

static inline PVOID
RtlpSrwKeyExclusive(PRTL_SRWLOCK SRWLock)
{
    return (PVOID)(((ULONG_PTR)&SRWLock->Ptr + 1) & ~(ULONG_PTR)1);
}

static inline PVOID
RtlpSrwKeyShared(PRTL_SRWLOCK SRWLock)
{
    return (PVOID)(((ULONG_PTR)&SRWLock->Ptr + 3) & ~(ULONG_PTR)1);
}

static inline volatile LONG *
RtlpSrwWord(PRTL_SRWLOCK SRWLock)
{
    return (volatile LONG *)&SRWLock->Ptr;
}

static inline VOID
RtlpSrwCheckInvalid(ULONG Value)
{
    /* Throw an exception when the lock can't be acquired or released this way */
    if ((Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE) == SRWLOCK_MASK_EXCLUSIVE_QUEUE ||
        (Value & SRWLOCK_MASK_SHARED_QUEUE) == SRWLOCK_MASK_SHARED_QUEUE)
    {
        RtlRaiseStatus(STATUS_RESOURCE_NOT_OWNED);
    }
}

/* Add Increment. When exclusive acquirers wait and nobody is in the shared
 * queue, mark the lock IN_EXCLUSIVE: shared acquirers may use the shared
 * queue again. Returns the previous value. */
static ULONG
RtlpSrwLockExclusive(PRTL_SRWLOCK SRWLock, LONG Increment)
{
    ULONG Value, New;

    for (Value = *RtlpSrwWord(SRWLock);; Value = New)
    {
        New = Value + Increment;
        RtlpSrwCheckInvalid(New);
        if ((New & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(New & SRWLOCK_MASK_SHARED_QUEUE))
            New |= SRWLOCK_MASK_IN_EXCLUSIVE;
        New = (ULONG)InterlockedCompareExchange(RtlpSrwWord(SRWLock), (LONG)New, (LONG)Value);
        if (New == Value)
            break;
    }
    return Value;
}

/* Add Increment. When no exclusive acquirer is left, drop IN_EXCLUSIVE
 * (only the shared queue remains). Returns the previous value. */
static ULONG
RtlpSrwUnlockExclusive(PRTL_SRWLOCK SRWLock, LONG Increment)
{
    ULONG Value, New;

    for (Value = *RtlpSrwWord(SRWLock);; Value = New)
    {
        New = Value + Increment;
        RtlpSrwCheckInvalid(New);
        if (!(New & SRWLOCK_MASK_EXCLUSIVE_QUEUE))
            New &= SRWLOCK_MASK_SHARED_QUEUE;
        New = (ULONG)InterlockedCompareExchange(RtlpSrwWord(SRWLock), (LONG)New, (LONG)Value);
        if (New == Value)
            break;
    }
    return Value;
}

/* An exclusive owner left: the next exclusive waiter goes first, else every
 * shared waiter */
static VOID
RtlpSrwLeaveExclusive(PRTL_SRWLOCK SRWLock, ULONG Value)
{
    if (Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
    {
        NtReleaseKeyedEvent(NULL, RtlpSrwKeyExclusive(SRWLock), FALSE, NULL);
    }
    else
    {
        Value &= SRWLOCK_MASK_SHARED_QUEUE;
        while (Value--)
            NtReleaseKeyedEvent(NULL, RtlpSrwKeyShared(SRWLock), FALSE, NULL);
    }
}

/* A shared owner left: wake one exclusive waiter once the last one is gone */
static VOID
RtlpSrwLeaveShared(PRTL_SRWLOCK SRWLock, ULONG Value)
{
    if ((Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(Value & SRWLOCK_MASK_SHARED_QUEUE))
        NtReleaseKeyedEvent(NULL, RtlpSrwKeyExclusive(SRWLock), FALSE, NULL);
}

VOID
NTAPI
RtlInitializeSRWLock(OUT PRTL_SRWLOCK SRWLock)
{
    SRWLock->Ptr = NULL;
}

VOID
NTAPI
RtlAcquireSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    if (RtlpSrwLockExclusive(SRWLock, SRWLOCK_RES_EXCLUSIVE))
        NtWaitForKeyedEvent(NULL, RtlpSrwKeyExclusive(SRWLock), FALSE, NULL);
}

VOID
NTAPI
RtlAcquireSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    ULONG Value, New;

    /* While exclusive acquirers wait and the shared queue may not be used,
       queue as an exclusive acquirer first */
    for (Value = *RtlpSrwWord(SRWLock);; Value = New)
    {
        if ((Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(Value & SRWLOCK_MASK_IN_EXCLUSIVE))
            New = Value + SRWLOCK_RES_EXCLUSIVE;
        else
            New = Value + SRWLOCK_RES_SHARED;
        RtlpSrwCheckInvalid(New);
        New = (ULONG)InterlockedCompareExchange(RtlpSrwWord(SRWLock), (LONG)New, (LONG)Value);
        if (New == Value)
            break;
    }

    /* Got the exclusive turn: trade it for a shared one */
    if ((Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE) && !(Value & SRWLOCK_MASK_IN_EXCLUSIVE))
    {
        NtWaitForKeyedEvent(NULL, RtlpSrwKeyExclusive(SRWLock), FALSE, NULL);
        Value = RtlpSrwUnlockExclusive(SRWLock, SRWLOCK_RES_SHARED - SRWLOCK_RES_EXCLUSIVE) -
                SRWLOCK_RES_EXCLUSIVE;
        RtlpSrwLeaveExclusive(SRWLock, Value);
    }

    if (Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
        NtWaitForKeyedEvent(NULL, RtlpSrwKeyShared(SRWLock), FALSE, NULL);
}

VOID
NTAPI
RtlReleaseSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock)
{
    RtlpSrwLeaveExclusive(SRWLock,
                          RtlpSrwUnlockExclusive(SRWLock, -SRWLOCK_RES_EXCLUSIVE) - SRWLOCK_RES_EXCLUSIVE);
}

VOID
NTAPI
RtlReleaseSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock)
{
    RtlpSrwLeaveShared(SRWLock,
                       RtlpSrwLockExclusive(SRWLock, -SRWLOCK_RES_SHARED) - SRWLOCK_RES_SHARED);
}

BOOLEAN
NTAPI
RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK SRWLock)
{
    return InterlockedCompareExchange(RtlpSrwWord(SRWLock),
                                      (LONG)(SRWLOCK_MASK_IN_EXCLUSIVE | SRWLOCK_RES_EXCLUSIVE),
                                      0) == 0;
}

BOOLEAN
NTAPI
RtlTryAcquireSRWLockShared(PRTL_SRWLOCK SRWLock)
{
    ULONG Value, New;

    for (Value = *RtlpSrwWord(SRWLock);; Value = New)
    {
        if (Value & SRWLOCK_MASK_EXCLUSIVE_QUEUE)
            return FALSE;
        New = (ULONG)InterlockedCompareExchange(RtlpSrwWord(SRWLock),
                                                (LONG)(Value + SRWLOCK_RES_SHARED), (LONG)Value);
        if (New == Value)
            return TRUE;
    }
}
