/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Waiting for a value in memory to change (WaitOnAddress)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* INCLUDES *****************************************************************/

#include <rtl_vista.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

/*
 * Each waiting thread puts an entry on its stack in the list of a bucket
 * chosen by the address, and sleeps on the process-wide keyed event (the
 * NULL handle) with the entry's address as the key. A waker takes the
 * entries of its address out of the list (marking each one by clearing
 * its Flink) and releases their keys after dropping the bucket lock: the
 * release of a keyed event waits until its waiter is sleeping.
 *
 * A waiter whose wait times out looks for its entry in the list: if a
 * waker already took it out, that waker is going to release its key, so
 * the waiter must take that release before its entry (its stack) goes away.
 */
typedef struct _RTLP_ADDRESS_WAITER
{
    LIST_ENTRY ListEntry;
    const volatile VOID *Address;
} RTLP_ADDRESS_WAITER, *PRTLP_ADDRESS_WAITER;

typedef struct _RTLP_ADDRESS_BUCKET
{
    RTL_SRWLOCK Lock;
    LIST_ENTRY Waiters; /* Initialized on first use, under the lock */
} RTLP_ADDRESS_BUCKET, *PRTLP_ADDRESS_BUCKET;

#define RTLP_ADDRESS_BUCKETS 64

static RTLP_ADDRESS_BUCKET RtlpAddressBuckets[RTLP_ADDRESS_BUCKETS];

/* FUNCTIONS ****************************************************************/

static
PRTLP_ADDRESS_BUCKET
RtlpLockAddressBucket(
    _In_ const volatile VOID *Address)
{
    PRTLP_ADDRESS_BUCKET Bucket;

    Bucket = &RtlpAddressBuckets[((ULONG_PTR)Address >> 3) % RTLP_ADDRESS_BUCKETS];
    RtlAcquireSRWLockExclusive(&Bucket->Lock);
    if (!Bucket->Waiters.Flink)
        InitializeListHead(&Bucket->Waiters);
    return Bucket;
}

static
BOOLEAN
RtlpAddressHasValue(
    _In_ const volatile VOID *Address,
    _In_ PVOID CompareAddress,
    _In_ SIZE_T AddressSize)
{
    switch (AddressSize)
    {
        case 1: return *(const volatile UCHAR *)Address == *(PUCHAR)CompareAddress;
        case 2: return *(const volatile USHORT *)Address == *(PUSHORT)CompareAddress;
        case 4: return *(const volatile ULONG *)Address == *(PULONG)CompareAddress;
        default: return *(const volatile ULONG64 *)Address == *(PULONG64)CompareAddress;
    }
}

/* EXPORTED FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
RtlWaitOnAddress(
    _In_ const volatile VOID *Address,
    _In_ PVOID CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PRTLP_ADDRESS_BUCKET Bucket;
    RTLP_ADDRESS_WAITER Waiter;
    NTSTATUS Status;

    if (AddressSize != 1 && AddressSize != 2 && AddressSize != 4 && AddressSize != 8)
        return STATUS_INVALID_PARAMETER;

    Bucket = RtlpLockAddressBucket(Address);

    /* The value already changed: nothing to wait for */
    if (!RtlpAddressHasValue(Address, CompareAddress, AddressSize))
    {
        RtlReleaseSRWLockExclusive(&Bucket->Lock);
        return STATUS_SUCCESS;
    }

    Waiter.Address = Address;
    InsertTailList(&Bucket->Waiters, &Waiter.ListEntry);
    RtlReleaseSRWLockExclusive(&Bucket->Lock);

    Status = NtWaitForKeyedEvent(NULL, &Waiter, FALSE, Timeout);
    if (Status == STATUS_SUCCESS)
        return STATUS_SUCCESS;

    /* Timed out: leave the list, unless a waker already took the entry */
    RtlAcquireSRWLockExclusive(&Bucket->Lock);
    if (Waiter.ListEntry.Flink)
    {
        RemoveEntryList(&Waiter.ListEntry);
        RtlReleaseSRWLockExclusive(&Bucket->Lock);
        return Status;
    }
    RtlReleaseSRWLockExclusive(&Bucket->Lock);

    /* That waker is releasing the key: take the release, and the wake */
    NtWaitForKeyedEvent(NULL, &Waiter, FALSE, NULL);
    return STATUS_SUCCESS;
}

static
VOID
RtlpWakeAddress(
    _In_ const volatile VOID *Address,
    _In_ BOOLEAN WakeAll)
{
    PRTLP_ADDRESS_BUCKET Bucket;
    PRTLP_ADDRESS_WAITER Waiter, Woken = NULL;
    PLIST_ENTRY Entry;

    Bucket = RtlpLockAddressBucket(Address);
    Entry = Bucket->Waiters.Flink;
    while (Entry != &Bucket->Waiters)
    {
        Waiter = CONTAINING_RECORD(Entry, RTLP_ADDRESS_WAITER, ListEntry);
        Entry = Entry->Flink;
        if (Waiter->Address != Address)
            continue;

        /* Take it out of the list, and chain it to the woken ones through Blink */
        RemoveEntryList(&Waiter->ListEntry);
        Waiter->ListEntry.Flink = NULL;
        Waiter->ListEntry.Blink = (PLIST_ENTRY)Woken;
        Woken = Waiter;
        if (!WakeAll)
            break;
    }
    RtlReleaseSRWLockExclusive(&Bucket->Lock);

    while (Woken)
    {
        /* Once released, the waiter returns and its entry goes away */
        Waiter = Woken;
        Woken = (PRTLP_ADDRESS_WAITER)Waiter->ListEntry.Blink;
        NtReleaseKeyedEvent(NULL, Waiter, FALSE, NULL);
    }
}

VOID
NTAPI
RtlWakeAddressAll(
    _In_ const volatile VOID *Address)
{
    RtlpWakeAddress(Address, TRUE);
}

VOID
NTAPI
RtlWakeAddressSingle(
    _In_ const volatile VOID *Address)
{
    RtlpWakeAddress(Address, FALSE);
}

/* EOF */
