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
 * The interrupt time functions of Windows 10, in 100ns units. The precise
 * ones read the performance counter instead of the last clock tick; ReactOS
 * does not track sleep time, so biased and unbiased are the same.
 */
VOID
WINAPI
QueryInterruptTime(
    _Out_ PULONGLONG lpInterruptTime)
{
    QueryUnbiasedInterruptTime(lpInterruptTime);
}

static
ULONGLONG
PerformanceCounterTime(VOID)
{
    LARGE_INTEGER Counter, Frequency;

    NtQueryPerformanceCounter(&Counter, &Frequency);
    if (!Frequency.QuadPart)
        return 0;
    /* (in two steps, so that it does not overflow) */
    return (Counter.QuadPart / Frequency.QuadPart) * 10000000ULL +
           (Counter.QuadPart % Frequency.QuadPart) * 10000000ULL / Frequency.QuadPart;
}

VOID
WINAPI
QueryInterruptTimePrecise(
    _Out_ PULONGLONG lpInterruptTimePrecise)
{
    *lpInterruptTimePrecise = PerformanceCounterTime();
}

VOID
WINAPI
QueryUnbiasedInterruptTimePrecise(
    _Out_ PULONGLONG lpUnbiasedInterruptTimePrecise)
{
    *lpUnbiasedInterruptTimePrecise = PerformanceCounterTime();
}

/*
 * SetWaitableTimerEx: SetWaitableTimer with power management hints (wake
 * context, tolerable delay), which ReactOS does not use.
 */
BOOL
WINAPI
SetWaitableTimerEx(
    _In_ HANDLE hTimer,
    _In_ const LARGE_INTEGER *lpDueTime,
    _In_ LONG lPeriod,
    _In_opt_ PTIMERAPCROUTINE pfnCompletionRoutine,
    _In_opt_ LPVOID lpArgToCompletionRoutine,
    _In_opt_ PVOID WakeContext,
    _In_ ULONG TolerableDelay)
{
    UNREFERENCED_PARAMETER(WakeContext);
    UNREFERENCED_PARAMETER(TolerableDelay);
    return SetWaitableTimer(hTimer, lpDueTime, lPeriod, pfnCompletionRoutine,
                            lpArgToCompletionRoutine, FALSE);
}

/*
 * The ideal processor of a thread, as a group and a number: ReactOS has one
 * processor group.
 */
BOOL
WINAPI
SetThreadIdealProcessorEx(
    _In_ HANDLE hThread,
    _In_ PPROCESSOR_NUMBER lpIdealProcessor,
    _Out_opt_ PPROCESSOR_NUMBER lpPreviousIdealProcessor)
{
    DWORD Previous;

    if (!lpIdealProcessor || lpIdealProcessor->Group != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Previous = SetThreadIdealProcessor(hThread, lpIdealProcessor->Number);
    if (Previous == (DWORD)-1)
        return FALSE;

    if (lpPreviousIdealProcessor)
    {
        lpPreviousIdealProcessor->Group = 0;
        lpPreviousIdealProcessor->Number = (BYTE)Previous;
        lpPreviousIdealProcessor->Reserved = 0;
    }
    return TRUE;
}

BOOL
WINAPI
GetThreadIdealProcessorEx(
    _In_ HANDLE hThread,
    _Out_ PPROCESSOR_NUMBER lpIdealProcessor)
{
    /* MAXIMUM_PROCESSORS asks for the current one without changing it */
    DWORD Current = SetThreadIdealProcessor(hThread, MAXIMUM_PROCESSORS);

    if (Current == (DWORD)-1)
        return FALSE;
    lpIdealProcessor->Group = 0;
    lpIdealProcessor->Number = (BYTE)Current;
    lpIdealProcessor->Reserved = 0;
    return TRUE;
}

/*
 * GetNumaProcessorNodeEx: the NUMA node of a processor given as a group and
 * a number. ReactOS has one processor group and one node.
 */
BOOL
WINAPI
GetNumaProcessorNodeEx(
    _In_ PPROCESSOR_NUMBER Processor,
    _Out_ PUSHORT NodeNumber)
{
    SYSTEM_INFO SystemInfo;

    if (!Processor || !NodeNumber)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    GetSystemInfo(&SystemInfo);
    if (Processor->Group != 0 || Processor->Number >= SystemInfo.dwNumberOfProcessors)
    {
        *NodeNumber = 0xFFFF;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *NodeNumber = 0;
    return TRUE;
}

/*
 * CopyFile2 (Windows 8): CopyFileExW with its flags in a structure and an
 * HRESULT. Its progress callback has another form: not called.
 * (COPYFILE2_EXTENDED_PARAMETERS, which winbase.h has for Windows 8 builds)
 */
typedef struct _COPYFILE2_PARAMETERS_ROS
{
    DWORD dwSize;
    DWORD dwCopyFlags;
    BOOL *pfCancel;
    PVOID pProgressRoutine;
    PVOID pvCallbackContext;
} COPYFILE2_PARAMETERS_ROS;

HRESULT
WINAPI
CopyFile2(
    _In_ PCWSTR pwszExistingFileName,
    _In_ PCWSTR pwszNewFileName,
    _In_opt_ COPYFILE2_PARAMETERS_ROS *pExtendedParameters)
{
    DWORD Flags = 0;
    LPBOOL Cancel = NULL;

    if (pExtendedParameters)
    {
        if (pExtendedParameters->dwSize < sizeof(*pExtendedParameters))
            return E_INVALIDARG;
        Flags = pExtendedParameters->dwCopyFlags;
        Cancel = pExtendedParameters->pfCancel;
    }

    if (!CopyFileExW(pwszExistingFileName, pwszNewFileName, NULL, NULL, Cancel, Flags))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

/*
 * FindStringOrdinal: where one string is in another, comparing code points
 * (no locale). Returns the index of the first character, or -1.
 */
INT
WINAPI
FindStringOrdinal(
    _In_ DWORD dwFindStringOrdinalFlags,
    _In_reads_(cchSource) LPCWSTR lpStringSource,
    _In_ INT cchSource,
    _In_reads_(cchValue) LPCWSTR lpStringValue,
    _In_ INT cchValue,
    _In_ BOOL bIgnoreCase)
{
    DWORD Flags = dwFindStringOrdinalFlags;
    BOOL Forward = (Flags == FIND_FROMSTART || Flags == FIND_STARTSWITH);
    INT Offset, Count;

    if (!lpStringSource || !lpStringValue)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (Flags != FIND_FROMSTART && Flags != FIND_FROMEND &&
        Flags != FIND_STARTSWITH && Flags != FIND_ENDSWITH)
    {
        SetLastError(ERROR_INVALID_FLAGS);
        return -1;
    }
    if (cchSource == -1) cchSource = lstrlenW(lpStringSource);
    if (cchValue == -1) cchValue = lstrlenW(lpStringValue);

    SetLastError(ERROR_SUCCESS);
    if (cchSource < cchValue)
        return -1;

    /* FROMSTART/FROMEND look everywhere, STARTSWITH/ENDSWITH at one place */
    Count = (Flags == FIND_FROMSTART || Flags == FIND_FROMEND) ? cchSource - cchValue + 1 : 1;
    Offset = Forward ? 0 : cchSource - cchValue;
    while (Count--)
    {
        if (CompareStringOrdinal(lpStringSource + Offset, cchValue, lpStringValue, cchValue, bIgnoreCase) == CSTR_EQUAL)
            return Offset;
        Offset += Forward ? 1 : -1;
    }
    return -1;
}

/* The other things a program can give WER for its crash reports: kept nowhere */
HRESULT
WINAPI
WerSetFlags(
    _In_ DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(dwFlags);
    return S_OK;
}

HRESULT
WINAPI
WerGetFlags(
    _In_ HANDLE hProcess,
    _Out_ PDWORD pdwFlags)
{
    UNREFERENCED_PARAMETER(hProcess);
    if (!pdwFlags)
        return E_INVALIDARG;
    *pdwFlags = 0;
    return S_OK;
}

HRESULT
WINAPI
WerRegisterMemoryBlock(
    _In_ PVOID pvAddress,
    _In_ DWORD dwSize)
{
    UNREFERENCED_PARAMETER(pvAddress);
    UNREFERENCED_PARAMETER(dwSize);
    return S_OK;
}

HRESULT
WINAPI
WerUnregisterMemoryBlock(
    _In_ PVOID pvAddress)
{
    UNREFERENCED_PARAMETER(pvAddress);
    return S_OK;
}

HRESULT
WINAPI
WerRegisterFile(
    _In_ PCWSTR pwzFile,
    _In_ DWORD regFileType,
    _In_ DWORD dwFlags)
{
    UNREFERENCED_PARAMETER(pwzFile);
    UNREFERENCED_PARAMETER(regFileType);
    UNREFERENCED_PARAMETER(dwFlags);
    return S_OK;
}

HRESULT
WINAPI
WerUnregisterFile(
    _In_ PCWSTR pwzFilePath)
{
    UNREFERENCED_PARAMETER(pwzFilePath);
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
 * acts on PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES (an AppContainer) only:
 * a handle list would restrict inheritance to these handles, ReactOS inherits
 * all inheritable ones.
 */
#include <procthreadattr.h>

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

/*
 * GetTimeZoneInformationForYear: the rules of a time zone for one year, from
 * its "Dynamic DST" registry key when it has one for that year (the rules
 * changed over the years), else its usual rules.
 */
BOOL
WINAPI
GetTimeZoneInformationForYear(
    _In_ USHORT wYear,
    _In_opt_ PDYNAMIC_TIME_ZONE_INFORMATION pdtzi,
    _Out_ LPTIME_ZONE_INFORMATION ptzi)
{
    DYNAMIC_TIME_ZONE_INFORMATION Current;
    WCHAR Path[160], YearName[8];
    UNICODE_STRING PathString;
    OBJECT_ATTRIBUTES ObjectAttributes;
    BASEP_TZI Tzi;
    HANDLE Key;

    if (!ptzi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!pdtzi)
    {
        if (GetDynamicTimeZoneInformation(&Current) == TIME_ZONE_ID_INVALID)
            return FALSE;
        pdtzi = &Current;
    }

    ptzi->Bias = pdtzi->Bias;
    RtlCopyMemory(ptzi->StandardName, pdtzi->StandardName, sizeof(ptzi->StandardName));
    ptzi->StandardDate = pdtzi->StandardDate;
    ptzi->StandardBias = pdtzi->StandardBias;
    RtlCopyMemory(ptzi->DaylightName, pdtzi->DaylightName, sizeof(ptzi->DaylightName));
    ptzi->DaylightDate = pdtzi->DaylightDate;
    ptzi->DaylightBias = pdtzi->DaylightBias;

    if (!pdtzi->TimeZoneKeyName[0] || pdtzi->DynamicDaylightTimeDisabled)
        return TRUE;

    _snwprintf(Path, RTL_NUMBER_OF(Path) - 1,
               L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\%s\\Dynamic DST",
               pdtzi->TimeZoneKeyName);
    Path[RTL_NUMBER_OF(Path) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&PathString, Path);
    InitializeObjectAttributes(&ObjectAttributes, &PathString, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
    {
        _snwprintf(YearName, RTL_NUMBER_OF(YearName) - 1, L"%u", wYear);
        YearName[RTL_NUMBER_OF(YearName) - 1] = UNICODE_NULL;
        if (BasepQueryValue(Key, YearName, &Tzi, sizeof(Tzi), NULL))
        {
            ptzi->Bias = Tzi.Bias;
            ptzi->StandardBias = Tzi.StandardBias;
            ptzi->DaylightBias = Tzi.DaylightBias;
            ptzi->StandardDate = Tzi.StandardDate;
            ptzi->DaylightDate = Tzi.DaylightDate;
        }
        NtClose(Key);
    }
    return TRUE;
}

/*
 * CancelIoEx: cancels the I/O of every thread of the process on a file, or
 * only one request (its OVERLAPPED is its I/O status block)
 */
BOOL
WINAPI
CancelIoEx(
    _In_ HANDLE hFile,
    _In_opt_ LPOVERLAPPED lpOverlapped)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtCancelIoFileEx(hFile, (PIO_STATUS_BLOCK)lpOverlapped, &IoStatusBlock);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

NTSTATUS WINAPI TpCallbackMayRunLong(PTP_CALLBACK_INSTANCE Instance);

BOOL
WINAPI
CallbackMayRunLong(
    _Inout_ PTP_CALLBACK_INSTANCE pci)
{
    NTSTATUS Status = TpCallbackMayRunLong(pci);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
GetPhysicallyInstalledSystemMemory(
    _Out_ PULONGLONG TotalMemoryInKilobytes)
{
    MEMORYSTATUSEX Status;

    if (!TotalMemoryInKilobytes)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Status.dwLength = sizeof(Status);
    if (!GlobalMemoryStatusEx(&Status))
        return FALSE;
    *TotalMemoryInKilobytes = Status.ullTotalPhys / 1024;
    return TRUE;
}

/*
 * The "cycle time" of a thread or a process: ReactOS does not count CPU cycles,
 * so this is its CPU time in 100 ns units, which grows the same way.
 */
BOOL
WINAPI
QueryThreadCycleTime(
    _In_ HANDLE ThreadHandle,
    _Out_ PULONG64 CycleTime)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;

    Status = NtQueryInformationThread(ThreadHandle, ThreadTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    *CycleTime = Times.KernelTime.QuadPart + Times.UserTime.QuadPart;
    return TRUE;
}

BOOL
WINAPI
QueryProcessCycleTime(
    _In_ HANDLE ProcessHandle,
    _Out_ PULONG64 CycleTime)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    *CycleTime = Times.KernelTime.QuadPart + Times.UserTime.QuadPart;
    return TRUE;
}

HRESULT
WINAPI
UnregisterApplicationRestart(VOID)
{
    /* Nothing restarts programs here (see RegisterApplicationRestart) */
    return S_OK;
}

#ifdef _WIN64
BOOL
WINAPI
Wow64GetThreadContext(
    _In_ HANDLE hThread,
    _Inout_ PWOW64_CONTEXT lpContext)
{
    NTSTATUS Status = RtlWow64GetThreadContext(hThread, lpContext);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
Wow64SetThreadContext(
    _In_ HANDLE hThread,
    _In_ const WOW64_CONTEXT *lpContext)
{
    NTSTATUS Status = RtlWow64SetThreadContext(hThread, (PWOW64_CONTEXT)lpContext);

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
#endif

/* (Not declared at every _WIN32_WINNT the headers are used with) */
BOOL WINAPI GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, PDWORD ReturnedLength);

/*
 * GetLogicalProcessorInformationEx: the same information as
 * GetLogicalProcessorInformation, in the newer variable-size records (a single
 * processor group, as ReactOS has).
 */
BOOL
WINAPI
GetLogicalProcessorInformationEx(
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
    _Out_writes_bytes_to_opt_(*ReturnedLength, *ReturnedLength) PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer,
    _Inout_ PDWORD ReturnedLength)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Info = NULL;
    DWORD InfoLength = 0, Needed = 0, Count, i, Size;
    PUCHAR Out = (PUCHAR)Buffer;
    SYSTEM_INFO SystemInfo;
    BOOL Fits;

    if (!ReturnedLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    GetLogicalProcessorInformation(NULL, &InfoLength);
    Info = RtlAllocateHeap(RtlGetProcessHeap(), 0, InfoLength);
    if (!Info)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!GetLogicalProcessorInformation(Info, &InfoLength))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        return FALSE;
    }
    Count = InfoLength / sizeof(*Info);
    GetSystemInfo(&SystemInfo);

    /* Two passes: the size, then (if it fits) the records */
    for (Fits = FALSE; ; Fits = TRUE)
    {
        Needed = 0;
        for (i = 0; i < Count; i++)
        {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Ex;

            if (RelationshipType != RelationAll && Info[i].Relationship != RelationshipType)
                continue;

            switch (Info[i].Relationship)
            {
                case RelationProcessorCore:
                case RelationProcessorPackage:
                    Size = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor.GroupMask[1]);
                    break;
                case RelationNumaNode:
                    Size = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, NumaNode) + sizeof(NUMA_NODE_RELATIONSHIP);
                    break;
                case RelationCache:
                    Size = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Cache) + sizeof(CACHE_RELATIONSHIP);
                    break;
                default:
                    continue;
            }
            Size = (Size + 7) & ~7;

            if (Fits)
            {
                Ex = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Out + Needed);
                RtlZeroMemory(Ex, Size);
                Ex->Relationship = Info[i].Relationship;
                Ex->Size = Size;
                switch (Info[i].Relationship)
                {
                    case RelationProcessorCore:
                    case RelationProcessorPackage:
                        Ex->Processor.Flags = (Info[i].Relationship == RelationProcessorCore) ?
                                              Info[i].ProcessorCore.Flags : 0;
                        Ex->Processor.GroupCount = 1;
                        Ex->Processor.GroupMask[0].Mask = Info[i].ProcessorMask;
                        break;
                    case RelationNumaNode:
                        Ex->NumaNode.NodeNumber = Info[i].NumaNode.NodeNumber;
                        Ex->NumaNode.GroupMask.Mask = Info[i].ProcessorMask;
                        break;
                    case RelationCache:
                        Ex->Cache.Level = Info[i].Cache.Level;
                        Ex->Cache.Associativity = Info[i].Cache.Associativity;
                        Ex->Cache.LineSize = Info[i].Cache.LineSize;
                        Ex->Cache.CacheSize = Info[i].Cache.Size;
                        Ex->Cache.Type = Info[i].Cache.Type;
                        Ex->Cache.GroupMask.Mask = Info[i].ProcessorMask;
                        break;
                    default:
                        break;
                }
            }
            Needed += Size;
        }

        /* The one processor group */
        if (RelationshipType == RelationAll || RelationshipType == RelationGroup)
        {
            Size = (FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group.GroupInfo[1]) + 7) & ~7;
            if (Fits)
            {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Ex =
                    (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Out + Needed);
                RtlZeroMemory(Ex, Size);
                Ex->Relationship = RelationGroup;
                Ex->Size = Size;
                Ex->Group.MaximumGroupCount = 1;
                Ex->Group.ActiveGroupCount = 1;
                Ex->Group.GroupInfo[0].MaximumProcessorCount = (BYTE)SystemInfo.dwNumberOfProcessors;
                Ex->Group.GroupInfo[0].ActiveProcessorCount = (BYTE)SystemInfo.dwNumberOfProcessors;
                Ex->Group.GroupInfo[0].ActiveProcessorMask = SystemInfo.dwActiveProcessorMask;
            }
            Needed += Size;
        }

        if (Fits)
            break;
        if (!Buffer || *ReturnedLength < Needed)
        {
            *ReturnedLength = Needed;
            RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
    }

    *ReturnedLength = Needed;
    RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
    return TRUE;
}

/* (winbase.h does not always declare it) */
BOOL WINAPI GetNumaNodeProcessorMask(UCHAR Node, PULONGLONG ProcessorMask);

/*
 * GetNumaNodeProcessorMaskEx: the processors of a NUMA node, with their
 * group. There is one group (and the nodes of GetNumaNodeProcessorMask).
 */
BOOL
WINAPI
GetNumaNodeProcessorMaskEx(
    _In_ USHORT Node,
    _Out_ PGROUP_AFFINITY ProcessorMask)
{
    ULONGLONG Mask;

    if (!ProcessorMask || Node > 0xff)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetNumaNodeProcessorMask((UCHAR)Node, &Mask))
        return FALSE;
    RtlZeroMemory(ProcessorMask, sizeof(*ProcessorMask));
    ProcessorMask->Mask = (KAFFINITY)Mask;
    ProcessorMask->Group = 0;
    return TRUE;
}
