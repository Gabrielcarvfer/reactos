/*
 * PROJECT:     ReactOS NT User Mode Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows Notification Facility (WNF) entry points
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * WinUI 3 and the Windows App SDK (Microsoft.Internal.FrameworkUdk.dll,
 * PowerToys) import these from ntdll: without them the DLL does not load.
 *
 * ReactOS has no WNF. Every state name reads as "no data published yet"
 * (an empty buffer, change stamp 0), as a state nobody has written does on
 * Windows; a subscription succeeds and never calls back, since no state
 * ever changes.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _WNF_STATE_NAME
{
    ULONG Data[2];
} WNF_STATE_NAME, *PWNF_STATE_NAME;
typedef const WNF_STATE_NAME *PCWNF_STATE_NAME;
typedef ULONG WNF_CHANGE_STAMP, *PWNF_CHANGE_STAMP;

NTSTATUS
NTAPI
NtQueryWnfStateData(
    _In_ PCWNF_STATE_NAME StateName,
    _In_opt_ const GUID *TypeId,
    _In_opt_ const VOID *ExplicitScope,
    _Out_ PWNF_CHANGE_STAMP ChangeStamp,
    _Out_writes_bytes_to_opt_(*BufferSize, *BufferSize) PVOID Buffer,
    _Inout_ PULONG BufferSize)
{
    if (!StateName || !ChangeStamp || !BufferSize)
        return STATUS_INVALID_PARAMETER;

    DPRINT("NtQueryWnfStateData(%08lx %08lx): no data\n", StateName->Data[0], StateName->Data[1]);
    *ChangeStamp = 0;
    *BufferSize = 0;
    return STATUS_SUCCESS;
}

/* (a subscription: nothing to free, it never calls back) */
static ULONG WnfDummySubscription;

NTSTATUS
NTAPI
RtlSubscribeWnfStateChangeNotification(
    _Outptr_ PVOID *Subscription,
    _In_ WNF_STATE_NAME StateName,
    _In_ WNF_CHANGE_STAMP ChangeStamp,
    _In_ PVOID Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ const GUID *TypeId,
    _In_opt_ ULONG SerializationGroup,
    _In_opt_ ULONG Unknown)
{
    if (!Subscription || !Callback)
        return STATUS_INVALID_PARAMETER;

    DPRINT("RtlSubscribeWnfStateChangeNotification(%08lx %08lx)\n", StateName.Data[0], StateName.Data[1]);
    *Subscription = &WnfDummySubscription;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlUnsubscribeWnfStateChangeNotification(
    _In_ PVOID Subscription)
{
    return Subscription ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}
