/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 7 kernel32 functions used by modern applications
 *              (Chromium among others)
 */

#include <k32_vista.h>
#include <winreg.h>
#include <ndk/cmfuncs.h>

#define NDEBUG
#include <debug.h>

/*
 * QueryUnbiasedInterruptTime: interrupt time in 100ns units. "Unbiased" means it
 * does not count time spent sleeping/hibernating; ReactOS does not track that,
 * so this is the plain interrupt time, which is monotonic as required.
 */
BOOL
WINAPI
QueryUnbiasedInterruptTime(
    _Out_ PULONGLONG UnbiasedTime)
{
    LARGE_INTEGER Time;

    if (!UnbiasedTime)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    do
    {
        Time.HighPart = SharedUserData->InterruptTime.High1Time;
        Time.LowPart = SharedUserData->InterruptTime.LowPart;
    } while (Time.HighPart != SharedUserData->InterruptTime.High2Time);

    *UnbiasedTime = Time.QuadPart;
    return TRUE;
}

/*
 * Power requests ("keep the display/system on while playing a video"). ReactOS
 * has no idle timeouts that would need to be held off, so a request only needs
 * to be a valid handle that can be closed.
 */
HANDLE
WINAPI
PowerCreateRequest(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return CreateEventW(NULL, TRUE, FALSE, NULL);
}

BOOL
WINAPI
PowerSetRequest(
    _In_ HANDLE PowerRequest,
    _In_ ULONG RequestType)
{
    UNREFERENCED_PARAMETER(RequestType);
    if (!PowerRequest || PowerRequest == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
PowerClearRequest(
    _In_ HANDLE PowerRequest,
    _In_ ULONG RequestType)
{
    return PowerSetRequest(PowerRequest, RequestType);
}

/*
 * CreateRemoteThreadEx: CreateRemoteThread with an attribute list (group
 * affinity, ideal processor...), which only tunes scheduling: ignore it.
 */
HANDLE
WINAPI
CreateRemoteThreadEx(
    _In_ HANDLE hProcess,
    _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
    _In_ SIZE_T dwStackSize,
    _In_ LPTHREAD_START_ROUTINE lpStartAddress,
    _In_opt_ LPVOID lpParameter,
    _In_ DWORD dwCreationFlags,
    _In_opt_ PVOID lpAttributeList,
    _Out_opt_ LPDWORD lpThreadId)
{
    UNREFERENCED_PARAMETER(lpAttributeList);
    return CreateRemoteThread(hProcess, lpThreadAttributes, dwStackSize, lpStartAddress,
                              lpParameter, dwCreationFlags & ~EXTENDED_STARTUPINFO_PRESENT, lpThreadId);
}

/*
 * Windows Error Reporting: applications register a module that analyses their
 * crashes. ReactOS has no WER, so accept and ignore the registration.
 */
HRESULT
WINAPI
WerRegisterRuntimeExceptionModule(
    _In_ PCWSTR pwszOutOfProcessCallbackDll,
    _In_ PVOID pContext)
{
    UNREFERENCED_PARAMETER(pwszOutOfProcessCallbackDll);
    UNREFERENCED_PARAMETER(pContext);
    return S_OK;
}

HRESULT
WINAPI
WerUnregisterRuntimeExceptionModule(
    _In_ PCWSTR pwszOutOfProcessCallbackDll,
    _In_ PVOID pContext)
{
    UNREFERENCED_PARAMETER(pwszOutOfProcessCallbackDll);
    UNREFERENCED_PARAMETER(pContext);
    return S_OK;
}

/*
 * RaiseFailFastException: terminate at once, without running exception
 * handlers (used by applications on fatal internal errors).
 */
VOID
WINAPI
RaiseFailFastException(
    _In_opt_ PEXCEPTION_RECORD pExceptionRecord,
    _In_opt_ PCONTEXT pContextRecord,
    _In_ DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(pContextRecord);
    UNREFERENCED_PARAMETER(dwFlags);
    DPRINT1("RaiseFailFastException(code 0x%lx): terminating the process\n",
            pExceptionRecord ? pExceptionRecord->ExceptionCode : 0);
    TerminateProcess(GetCurrentProcess(),
                     pExceptionRecord ? pExceptionRecord->ExceptionCode : 0xC0000602 /* STATUS_FAIL_FAST_EXCEPTION */);
}

/*
 * ResolveLocaleName: the specific locale a name stands for, e.g. "en" -> "en-US".
 * A NULL name means the user default locale.
 */
int
WINAPI
ResolveLocaleName(
    _In_opt_ LPCWSTR lpNameToResolve,
    _Out_writes_opt_(cchLocaleName) LPWSTR lpLocaleName,
    _In_ int cchLocaleName)
{
    LCID Lcid;

    if (lpNameToResolve)
    {
        Lcid = LocaleNameToLCID(lpNameToResolve, LOCALE_ALLOW_NEUTRAL_NAMES);
        if (!Lcid)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        /* A neutral language becomes its default specific locale */
        Lcid = ConvertDefaultLocale(Lcid);
    }
    else
    {
        Lcid = GetUserDefaultLCID();
    }

    return LCIDToLocaleName(Lcid, lpLocaleName, cchLocaleName, 0);
}

/*
 * Process/thread attribute lists (STARTUPINFOEX.lpAttributeList, Vista+).
 * The list is opaque to callers: it just records the attributes. CreateProcess
 * does not act on them yet (see CreateProcessInternalW): a handle list would
 * restrict inheritance to these handles, ReactOS inherits all inheritable ones.
 */
typedef struct _PROC_THREAD_ATTRIBUTE_ENTRY
{
    DWORD_PTR Attribute;
    SIZE_T Size;
    PVOID Value;
} PROC_THREAD_ATTRIBUTE_ENTRY;

typedef struct _PROC_THREAD_ATTRIBUTE_LIST_INTERNAL
{
    DWORD Flags;    /* attributes present, one bit per attribute number */
    ULONG Size;     /* number of entries allocated */
    ULONG Count;    /* number of entries used */
    ULONG Reserved;
    PROC_THREAD_ATTRIBUTE_ENTRY Entries[ANYSIZE_ARRAY];
} PROC_THREAD_ATTRIBUTE_LIST_INTERNAL, *PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL;

BOOL
WINAPI
InitializeProcThreadAttributeList(
    _Out_writes_bytes_to_opt_(*lpSize, *lpSize) LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwAttributeCount,
    _Reserved_ DWORD dwFlags,
    _When_(lpAttributeList == NULL, _Out_) _When_(lpAttributeList != NULL, _Inout_) PSIZE_T lpSize)
{
    PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL List = (PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL)lpAttributeList;
    SIZE_T Needed = FIELD_OFFSET(PROC_THREAD_ATTRIBUTE_LIST_INTERNAL, Entries[dwAttributeCount]);

    if (dwFlags || !lpSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* The first call usually only asks for the size */
    if (!List || *lpSize < Needed)
    {
        *lpSize = Needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    List->Flags = 0;
    List->Size = dwAttributeCount;
    List->Count = 0;
    List->Reserved = 0;
    *lpSize = Needed;
    return TRUE;
}

BOOL
WINAPI
UpdateProcThreadAttribute(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList,
    _In_ DWORD dwFlags,
    _In_ DWORD_PTR Attribute,
    _In_reads_bytes_opt_(cbSize) PVOID lpValue,
    _In_ SIZE_T cbSize,
    _Out_writes_bytes_opt_(cbSize) PVOID lpPreviousValue,
    _In_opt_ PSIZE_T lpReturnSize)
{
    PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL List = (PPROC_THREAD_ATTRIBUTE_LIST_INTERNAL)lpAttributeList;
    ULONG Number = (ULONG)(Attribute & PROC_THREAD_ATTRIBUTE_NUMBER);

    UNREFERENCED_PARAMETER(lpPreviousValue);
    UNREFERENCED_PARAMETER(lpReturnSize);

    if (!List || dwFlags || Number >= 32 || (!lpValue && cbSize))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (List->Flags & (1UL << Number))
    {
        /* Each attribute can be given once */
        SetLastError(ERROR_OBJECT_NAME_EXISTS);
        return FALSE;
    }
    if (List->Count >= List->Size)
    {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    List->Entries[List->Count].Attribute = Attribute;
    List->Entries[List->Count].Size = cbSize;
    List->Entries[List->Count].Value = lpValue;
    List->Count++;
    List->Flags |= 1UL << Number;
    return TRUE;
}

VOID
WINAPI
DeleteProcThreadAttributeList(
    _Inout_ LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList)
{
    /* Nothing was allocated: the caller owns the memory */
    UNREFERENCED_PARAMETER(lpAttributeList);
}

/* REG_TZI_FORMAT: the TZI value of the keys under Time Zones */
typedef struct _BASEP_TZI
{
    LONG Bias;
    LONG StandardBias;
    LONG DaylightBias;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
} BASEP_TZI;

static BOOLEAN
BasepQueryValue(HANDLE Key, PCWSTR Name, PVOID Data, ULONG Size, PULONG ReturnedSize)
{
    UNICODE_STRING ValueName;
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 256];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    ULONG Length;

    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length)) ||
        Value->DataLength > Size)
    {
        return FALSE;
    }
    RtlCopyMemory(Data, Value->Data, Value->DataLength);
    if (ReturnedSize)
        *ReturnedSize = Value->DataLength;
    return TRUE;
}

/* The name of the key under Time Zones that describes this time zone */
static VOID
BasepFindTimeZoneKeyName(
    _In_ const TIME_ZONE_INFORMATION *TimeZone,
    _Out_writes_(Count) PWSTR KeyName,
    _In_ ULONG Count)
{
    UNICODE_STRING ZonesName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UCHAR Buffer[FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + 128 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Info = (PKEY_BASIC_INFORMATION)Buffer;
    HANDLE Zones, Zone;
    ULONG Index, Length;
    BOOLEAN NameMatched = FALSE;

    InitializeObjectAttributes(&ObjectAttributes, &ZonesName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(NtOpenKey(&Zones, KEY_READ, &ObjectAttributes)))
        return;

    for (Index = 0;
         !NameMatched && NT_SUCCESS(NtEnumerateKey(Zones, Index, KeyBasicInformation, Buffer, sizeof(Buffer) - sizeof(WCHAR), &Length));
         Index++)
    {
        UNICODE_STRING SubKey = { (USHORT)Info->NameLength, (USHORT)Info->NameLength, Info->Name };
        BASEP_TZI Tzi;
        WCHAR Std[32] = { 0 };

        InitializeObjectAttributes(&ObjectAttributes, &SubKey, OBJ_CASE_INSENSITIVE, Zones, NULL);
        if (!NT_SUCCESS(NtOpenKey(&Zone, KEY_QUERY_VALUE, &ObjectAttributes)))
            continue;
        if (BasepQueryValue(Zone, L"TZI", &Tzi, sizeof(Tzi), NULL) &&
            Tzi.Bias == TimeZone->Bias && Tzi.StandardBias == TimeZone->StandardBias &&
            Tzi.DaylightBias == TimeZone->DaylightBias &&
            !memcmp(&Tzi.StandardDate, &TimeZone->StandardDate, sizeof(SYSTEMTIME)) &&
            !memcmp(&Tzi.DaylightDate, &TimeZone->DaylightDate, sizeof(SYSTEMTIME)))
        {
            /* Same rules: a candidate, the right one if the names match too */
            BasepQueryValue(Zone, L"Std", Std, sizeof(Std) - sizeof(WCHAR), NULL);
            NameMatched = !wcscmp(Std, TimeZone->StandardName);
            if (NameMatched || !KeyName[0])
            {
                Length = min(Info->NameLength / sizeof(WCHAR), Count - 1);
                RtlCopyMemory(KeyName, Info->Name, Length * sizeof(WCHAR));
                KeyName[Length] = UNICODE_NULL;
            }
        }
        NtClose(Zone);
    }
    NtClose(Zones);
}

/*
 * GetDynamicTimeZoneInformation (Vista): the time zone, plus the name of its
 * key under HKLM\...\Time Zones, which programs (Chromium/ICU) map to IANA
 * zone names. ReactOS does not always store that name: then the key whose
 * rules match the time zone is used.
 */
DWORD
WINAPI
GetDynamicTimeZoneInformation(
    _Out_ PDYNAMIC_TIME_ZONE_INFORMATION pTimeZoneInformation)
{
    TIME_ZONE_INFORMATION TimeZone;
    DWORD Result;
    ULONG Size;

    if (!pTimeZoneInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return TIME_ZONE_ID_INVALID;
    }

    Result = GetTimeZoneInformation(&TimeZone);
    if (Result == TIME_ZONE_ID_INVALID)
        return Result;

    RtlZeroMemory(pTimeZoneInformation, sizeof(*pTimeZoneInformation));
    pTimeZoneInformation->Bias = TimeZone.Bias;
    RtlCopyMemory(pTimeZoneInformation->StandardName, TimeZone.StandardName, sizeof(TimeZone.StandardName));
    pTimeZoneInformation->StandardDate = TimeZone.StandardDate;
    pTimeZoneInformation->StandardBias = TimeZone.StandardBias;
    RtlCopyMemory(pTimeZoneInformation->DaylightName, TimeZone.DaylightName, sizeof(TimeZone.DaylightName));
    pTimeZoneInformation->DaylightDate = TimeZone.DaylightDate;
    pTimeZoneInformation->DaylightBias = TimeZone.DaylightBias;
    pTimeZoneInformation->DynamicDaylightTimeDisabled = FALSE;

    /* kernel32 does not use advapi32: read the registry with the native API */
    {
        UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation");
        UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"TimeZoneKeyName");
        OBJECT_ATTRIBUTES ObjectAttributes;
        UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(pTimeZoneInformation->TimeZoneKeyName)];
        PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
        HANDLE Key;

        InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        if (NT_SUCCESS(NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
        {
            if (NT_SUCCESS(NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation,
                                           Buffer, sizeof(Buffer), &Size)) &&
                Value->Type == REG_SZ)
            {
                Size = min(Value->DataLength, sizeof(pTimeZoneInformation->TimeZoneKeyName) - sizeof(WCHAR));
                RtlCopyMemory(pTimeZoneInformation->TimeZoneKeyName, Value->Data, Size);
            }
            NtClose(Key);
        }
    }
    if (!pTimeZoneInformation->TimeZoneKeyName[0])
    {
        /* Not stored: find the time zone's key by its rules */
        BasepFindTimeZoneKeyName(&TimeZone, pTimeZoneInformation->TimeZoneKeyName,
                                 ARRAYSIZE(pTimeZoneInformation->TimeZoneKeyName));
    }
    if (!pTimeZoneInformation->TimeZoneKeyName[0])
    {
        RtlCopyMemory(pTimeZoneInformation->TimeZoneKeyName, TimeZone.StandardName,
                      sizeof(TimeZone.StandardName));
    }
    if (!pTimeZoneInformation->TimeZoneKeyName[0] && TimeZone.Bias == 0 &&
        TimeZone.StandardDate.wMonth == 0)
    {
        /* No time zone set up (LiveCD): coordinated universal time, Windows' "UTC" */
        wcscpy(pTimeZoneInformation->TimeZoneKeyName, L"UTC");
    }

    return Result;
}
