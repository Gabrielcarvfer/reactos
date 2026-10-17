/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           Condition Variable Routines
 * PROGRAMMERS:       Thomas Weidenmueller <w3seek@reactos.com>
 *                    Stephan A. R�ger
 */

/* NOTE: A condition variable is a wake counter in the low 32 bits of its
   pointer (0 when initialized). A sleeper reads it, drops the caller's lock
   and waits (RtlWaitOnAddress) until a waker changes it; wakers bump it and
   wake one or all of the threads waiting on its address. This is the design
   of Wine's ntdll (LGPL-2.1+).

   The previous version kept the sleepers on a list and released them with
   a zero-timeout keyed event release: a sleeper that was on the list but not
   asleep yet missed its wakeup and slept forever (Mesa's llvmpipe threads
   hung that way, e.g. in Flutter programs such as RustDesk). */

/* INCLUDES ******************************************************************/

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
RtlAcquireSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock);
VOID
NTAPI
RtlAcquireSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock);
VOID
NTAPI
RtlReleaseSRWLockExclusive(IN OUT PRTL_SRWLOCK SRWLock);
VOID
NTAPI
RtlReleaseSRWLockShared(IN OUT PRTL_SRWLOCK SRWLock);

static HANDLE CondVarKeyedEventHandle = NULL;

/* (kept for the loader, which creates and closes it per process) */
VOID
NTAPI
RtlpInitializeKeyedEvent(VOID)
{
    ASSERT(CondVarKeyedEventHandle == NULL);
    NtCreateKeyedEvent(&CondVarKeyedEventHandle, EVENT_ALL_ACCESS, NULL, 0);
}

VOID
NTAPI
RtlpCloseKeyedEvent(VOID)
{
    ASSERT(CondVarKeyedEventHandle != NULL);
    NtClose(CondVarKeyedEventHandle);
    CondVarKeyedEventHandle = NULL;
}

static inline volatile LONG *
InternalCondVarCounter(PRTL_CONDITION_VARIABLE ConditionVariable)
{
    return (volatile LONG *)&ConditionVariable->Ptr;
}

/* EXPORTED FUNCTIONS ********************************************************/

VOID
NTAPI
RtlInitializeConditionVariable(OUT PRTL_CONDITION_VARIABLE ConditionVariable)
{
    ConditionVariable->Ptr = NULL;
}

VOID
NTAPI
RtlWakeConditionVariable(IN OUT PRTL_CONDITION_VARIABLE ConditionVariable)
{
    InterlockedIncrement(InternalCondVarCounter(ConditionVariable));
    RtlWakeAddressSingle((PVOID)InternalCondVarCounter(ConditionVariable));
}

VOID
NTAPI
RtlWakeAllConditionVariable(IN OUT PRTL_CONDITION_VARIABLE ConditionVariable)
{
    InterlockedIncrement(InternalCondVarCounter(ConditionVariable));
    RtlWakeAddressAll((PVOID)InternalCondVarCounter(ConditionVariable));
}

NTSTATUS
NTAPI
RtlSleepConditionVariableCS(IN OUT PRTL_CONDITION_VARIABLE ConditionVariable,
                            IN OUT PRTL_CRITICAL_SECTION CriticalSection,
                            IN PLARGE_INTEGER TimeOut OPTIONAL)
{
    LONG Value = *InternalCondVarCounter(ConditionVariable);
    NTSTATUS Status;

    RtlLeaveCriticalSection(CriticalSection);
    Status = RtlWaitOnAddress(InternalCondVarCounter(ConditionVariable), &Value, sizeof(Value), TimeOut);
    RtlEnterCriticalSection(CriticalSection);
    return Status;
}

NTSTATUS
NTAPI
RtlSleepConditionVariableSRW(IN OUT PRTL_CONDITION_VARIABLE ConditionVariable,
                             IN OUT PRTL_SRWLOCK SRWLock,
                             IN PLARGE_INTEGER TimeOut OPTIONAL,
                             IN ULONG Flags)
{
    LONG Value = *InternalCondVarCounter(ConditionVariable);
    NTSTATUS Status;

    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlReleaseSRWLockShared(SRWLock);
    else
        RtlReleaseSRWLockExclusive(SRWLock);

    Status = RtlWaitOnAddress(InternalCondVarCounter(ConditionVariable), &Value, sizeof(Value), TimeOut);

    if (Flags & RTL_CONDITION_VARIABLE_LOCKMODE_SHARED)
        RtlAcquireSRWLockShared(SRWLock);
    else
        RtlAcquireSRWLockExclusive(SRWLock);
    return Status;
}

/* EOF */
