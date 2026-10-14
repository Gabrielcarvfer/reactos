/*
 * PROJECT:     ReactOS NT User Mode Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Manifest-based event provider API (EtwEvent*, exported by
 *              advapi32 as Event*)
 *
 * ReactOS has no event tracing sessions yet, so no provider is ever enabled:
 * registering succeeds and hands out a handle, writing an event drops it.
 * That is what Windows does when nobody listens, which is the common case.
 */

#include <ntdll.h>
#include <evntprov.h>

#define NDEBUG
#include <debug.h>

/* Handles only need to be unique and non-zero */
static LONG EtwpNextRegHandle;

ULONG
NTAPI
EtwEventRegister(
    _In_ LPCGUID ProviderId,
    _In_opt_ PENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    UNREFERENCED_PARAMETER(EnableCallback);
    UNREFERENCED_PARAMETER(CallbackContext);

    if (!ProviderId || !RegHandle)
        return ERROR_INVALID_PARAMETER;

    *RegHandle = (REGHANDLE)InterlockedIncrement(&EtwpNextRegHandle);
    return ERROR_SUCCESS;
}

ULONG
NTAPI
EtwEventUnregister(
    _In_ REGHANDLE RegHandle)
{
    UNREFERENCED_PARAMETER(RegHandle);
    return ERROR_SUCCESS;
}

BOOLEAN
NTAPI
EtwEventEnabled(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(EventDescriptor);
    return FALSE;
}

BOOLEAN
NTAPI
EtwEventProviderEnabled(
    _In_ REGHANDLE RegHandle,
    _In_ UCHAR Level,
    _In_ ULONGLONG Keyword)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(Keyword);
    return FALSE;
}

ULONG
NTAPI
EtwEventWrite(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(UserDataCount);
    UNREFERENCED_PARAMETER(UserData);

    if (!EventDescriptor)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

ULONG
NTAPI
EtwEventWriteTransfer(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_opt_ LPCGUID ActivityId,
    _In_opt_ LPCGUID RelatedActivityId,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    UNREFERENCED_PARAMETER(ActivityId);
    UNREFERENCED_PARAMETER(RelatedActivityId);
    return EtwEventWrite(RegHandle, EventDescriptor, UserDataCount, UserData);
}

ULONG
NTAPI
EtwEventWriteString(
    _In_ REGHANDLE RegHandle,
    _In_ UCHAR Level,
    _In_ ULONGLONG Keyword,
    _In_ PCWSTR String)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(Keyword);
    UNREFERENCED_PARAMETER(String);
    return ERROR_SUCCESS;
}

ULONG
NTAPI
EtwEventSetInformation(
    _In_ REGHANDLE RegHandle,
    _In_ EVENT_INFO_CLASS InformationClass,
    _In_reads_bytes_opt_(InformationLength) PVOID EventInformation,
    _In_ ULONG InformationLength)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(EventInformation);
    UNREFERENCED_PARAMETER(InformationLength);

    /* Provider traits and the like only matter to listening sessions */
    return ERROR_SUCCESS;
}

/* Activity IDs only need to be unique on this machine */
static VOID
EtwpCreateActivityId(
    _Out_ LPGUID ActivityId)
{
    static LONG Counter;
    LARGE_INTEGER Time;

    NtQuerySystemTime(&Time);
    ActivityId->Data1 = (ULONG)InterlockedIncrement(&Counter);
    ActivityId->Data2 = (USHORT)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    ActivityId->Data3 = (USHORT)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    RtlCopyMemory(ActivityId->Data4, &Time, sizeof(ActivityId->Data4));
}

ULONG
NTAPI
EtwEventActivityIdControl(
    _In_ ULONG ControlCode,
    _Inout_ LPGUID ActivityId)
{
    static GUID ThreadActivityId; /* FIXME: should be per thread */

    if (!ActivityId)
        return ERROR_INVALID_PARAMETER;

    switch (ControlCode)
    {
        case EVENT_ACTIVITY_CTRL_GET_ID:
            *ActivityId = ThreadActivityId;
            return ERROR_SUCCESS;
        case EVENT_ACTIVITY_CTRL_SET_ID:
            ThreadActivityId = *ActivityId;
            return ERROR_SUCCESS;
        case EVENT_ACTIVITY_CTRL_CREATE_ID:
            EtwpCreateActivityId(ActivityId);
            return ERROR_SUCCESS;
        case EVENT_ACTIVITY_CTRL_GET_SET_ID:
        {
            GUID Old = ThreadActivityId;
            ThreadActivityId = *ActivityId;
            *ActivityId = Old;
            return ERROR_SUCCESS;
        }
        case EVENT_ACTIVITY_CTRL_CREATE_SET_ID:
            *ActivityId = ThreadActivityId;
            EtwpCreateActivityId(&ThreadActivityId);
            return ERROR_SUCCESS;
        default:
            return ERROR_INVALID_PARAMETER;
    }
}
