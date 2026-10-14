/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 7 and 8 kernel32 functions used by modern applications:
 *              thread error mode and information, memory offering, process
 *              snapshots (Qt 6, VS Code, Python 3.13, GLib)
 */

#include <k32_vista.h>
#include <ndk/mmfuncs.h>

#define NDEBUG
#include <debug.h>

/* THREAD ERROR MODE (Windows 7) ********************************************/

/* The SEM_* flags of a thread, and their RTL_SEM_* values in its TEB */
static const struct
{
    DWORD Win32;
    ULONG Rtl;
} ThreadErrorModes[] =
{
    { SEM_FAILCRITICALERRORS, RTL_SEM_FAILCRITICALERRORS },
    { SEM_NOGPFAULTERRORBOX,  RTL_SEM_NOGPFAULTERRORBOX },
    { SEM_NOOPENFILEERRORBOX, RTL_SEM_NOALIGNMENTFAULTEXCEPT },
};

DWORD
WINAPI
GetThreadErrorMode(VOID)
{
    ULONG Mode = RtlGetThreadErrorMode();
    DWORD Result = 0;
    UINT i;

    for (i = 0; i < ARRAYSIZE(ThreadErrorModes); i++)
    {
        if (Mode & ThreadErrorModes[i].Rtl)
            Result |= ThreadErrorModes[i].Win32;
    }
    return Result;
}

BOOL
WINAPI
SetThreadErrorMode(
    _In_ DWORD dwNewMode,
    _Out_opt_ LPDWORD lpOldMode)
{
    ULONG Mode = 0;
    DWORD OldMode = GetThreadErrorMode();
    NTSTATUS Status;
    UINT i;

    if (dwNewMode & ~(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (i = 0; i < ARRAYSIZE(ThreadErrorModes); i++)
    {
        if (dwNewMode & ThreadErrorModes[i].Win32)
            Mode |= ThreadErrorModes[i].Rtl;
    }

    Status = RtlSetThreadErrorMode(Mode, NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    if (lpOldMode) *lpOldMode = OldMode;
    return TRUE;
}

/* THREAD INFORMATION (Windows 8) *******************************************/

/* (processthreadsapi.h has them only for Windows 8 and later) */
typedef enum _K32_THREAD_INFORMATION_CLASS
{
    K32ThreadMemoryPriority,
    K32ThreadAbsoluteCpuPriority,
    K32ThreadDynamicCodePolicy,
    K32ThreadPowerThrottling,
} K32_THREAD_INFORMATION_CLASS;

typedef struct _K32_MEMORY_PRIORITY_INFORMATION
{
    ULONG MemoryPriority;
} K32_MEMORY_PRIORITY_INFORMATION;

typedef struct _K32_THREAD_POWER_THROTTLING_STATE
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} K32_THREAD_POWER_THROTTLING_STATE;

#define K32_MEMORY_PRIORITY_NORMAL 5

/*
 * ReactOS has no page priorities and no power throttling: a thread's settings
 * are accepted, and it keeps running as before (as on a machine where the
 * scheduler ignores them).
 */
BOOL
WINAPI
SetThreadInformation(
    _In_ HANDLE hThread,
    _In_ K32_THREAD_INFORMATION_CLASS ThreadInformationClass,
    _In_reads_bytes_(ThreadInformationSize) LPVOID ThreadInformation,
    _In_ DWORD ThreadInformationSize)
{
    switch (ThreadInformationClass)
    {
        case K32ThreadMemoryPriority:
            if (ThreadInformationSize != sizeof(K32_MEMORY_PRIORITY_INFORMATION))
                break;
            return TRUE;

        case K32ThreadPowerThrottling:
            if (ThreadInformationSize != sizeof(K32_THREAD_POWER_THROTTLING_STATE))
                break;
            return TRUE;

        default:
            break;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
GetThreadInformation(
    _In_ HANDLE hThread,
    _In_ K32_THREAD_INFORMATION_CLASS ThreadInformationClass,
    _Out_writes_bytes_(ThreadInformationSize) LPVOID ThreadInformation,
    _In_ DWORD ThreadInformationSize)
{
    switch (ThreadInformationClass)
    {
        case K32ThreadMemoryPriority:
            if (ThreadInformationSize != sizeof(K32_MEMORY_PRIORITY_INFORMATION))
                break;
            ((K32_MEMORY_PRIORITY_INFORMATION *)ThreadInformation)->MemoryPriority = K32_MEMORY_PRIORITY_NORMAL;
            return TRUE;

        case K32ThreadPowerThrottling:
            if (ThreadInformationSize != sizeof(K32_THREAD_POWER_THROTTLING_STATE))
                break;
            RtlZeroMemory(ThreadInformation, ThreadInformationSize);
            ((K32_THREAD_POWER_THROTTLING_STATE *)ThreadInformation)->Version = 1;
            return TRUE;

        default:
            break;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* MEMORY (Windows 8.1) *****************************************************/

/* The pages' contents may be thrown away (and are: see MEM_RESET) */
DWORD
WINAPI
DiscardVirtualMemory(
    _Inout_updates_(Size) PVOID VirtualAddress,
    _In_ SIZE_T Size)
{
    PVOID Base = VirtualAddress;
    SIZE_T RegionSize = Size;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &RegionSize, MEM_RESET, PAGE_NOACCESS);
    return NT_SUCCESS(Status) ? ERROR_SUCCESS : RtlNtStatusToDosError(Status);
}

/* Offered pages may be reclaimed by the system; here they never are, so
 * reclaiming them always finds their contents intact */
DWORD
WINAPI
OfferVirtualMemory(
    _Inout_updates_(Size) PVOID VirtualAddress,
    _In_ SIZE_T Size,
    _In_ ULONG Priority)
{
    if (!VirtualAddress || !Size)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

DWORD
WINAPI
ReclaimVirtualMemory(
    _In_reads_(Size) const void *VirtualAddress,
    _In_ SIZE_T Size)
{
    if (!VirtualAddress || !Size)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

/* PROCESS SNAPSHOTS (Windows 8.1) ******************************************/

/* (processsnapshot.h) */
typedef struct _K32_PSS_PROCESS_INFORMATION
{
    DWORD ExitStatus;
    void *PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    DWORD ProcessId;
    DWORD ParentProcessId;
    DWORD Flags;
    FILETIME CreateTime;
    FILETIME ExitTime;
    FILETIME KernelTime;
    FILETIME UserTime;
    DWORD PriorityClass;
    ULONG_PTR PeakVirtualSize;
    ULONG_PTR VirtualSize;
    DWORD PageFaultCount;
    ULONG_PTR PeakWorkingSetSize;
    ULONG_PTR WorkingSetSize;
    ULONG_PTR QuotaPeakPagedPoolUsage;
    ULONG_PTR QuotaPagedPoolUsage;
    ULONG_PTR QuotaPeakNonPagedPoolUsage;
    ULONG_PTR QuotaNonPagedPoolUsage;
    ULONG_PTR PagefileUsage;
    ULONG_PTR PeakPagefileUsage;
    ULONG_PTR PrivateUsage;
    DWORD ExecuteFlags;
    WCHAR ImageFileName[MAX_PATH];
} K32_PSS_PROCESS_INFORMATION;

#define K32_PSS_QUERY_PROCESS_INFORMATION 0

#define PSS_SNAPSHOT_MAGIC 'SssP'

/*
 * A snapshot holds the process information only (what Python asks for, to
 * know the parent process); the other parts (handles, threads, the address
 * space) are not captured, and asking for them says so.
 */
typedef struct _PSS_SNAPSHOT
{
    ULONG Magic;
    K32_PSS_PROCESS_INFORMATION Process;
} PSS_SNAPSHOT, *PPSS_SNAPSHOT;

/* (winbase.h has it only for Vista and later builds) */
BOOL WINAPI QueryFullProcessImageNameW(HANDLE hProcess, DWORD dwFlags, LPWSTR lpExeName, PDWORD lpdwSize);

static void FileTimeFromLargeInteger(FILETIME *Time, const LARGE_INTEGER *Value)
{
    Time->dwLowDateTime = Value->LowPart;
    Time->dwHighDateTime = (DWORD)Value->HighPart;
}

DWORD
WINAPI
PssCaptureSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ DWORD CaptureFlags,
    _In_opt_ DWORD ThreadContextFlags,
    _Out_ HANDLE *SnapshotHandle)
{
    PROCESS_BASIC_INFORMATION Basic;
    KERNEL_USER_TIMES Times;
    VM_COUNTERS_EX Counters;
    PPSS_SNAPSHOT Snapshot;
    DWORD Length;
    NTSTATUS Status;

    if (!SnapshotHandle)
        return ERROR_INVALID_PARAMETER;
    *SnapshotHandle = NULL;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &Basic, sizeof(Basic), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Snapshot = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Snapshot));
    if (!Snapshot)
        return ERROR_NOT_ENOUGH_MEMORY;
    Snapshot->Magic = PSS_SNAPSHOT_MAGIC;

    Snapshot->Process.ExitStatus = Basic.ExitStatus;
    Snapshot->Process.PebBaseAddress = Basic.PebBaseAddress;
    Snapshot->Process.AffinityMask = Basic.AffinityMask;
    Snapshot->Process.BasePriority = Basic.BasePriority;
    Snapshot->Process.ProcessId = (DWORD)Basic.UniqueProcessId;
    Snapshot->Process.ParentProcessId = (DWORD)Basic.InheritedFromUniqueProcessId;

    if (NT_SUCCESS(NtQueryInformationProcess(ProcessHandle, ProcessTimes, &Times, sizeof(Times), NULL)))
    {
        FileTimeFromLargeInteger(&Snapshot->Process.CreateTime, &Times.CreateTime);
        FileTimeFromLargeInteger(&Snapshot->Process.ExitTime, &Times.ExitTime);
        FileTimeFromLargeInteger(&Snapshot->Process.KernelTime, &Times.KernelTime);
        FileTimeFromLargeInteger(&Snapshot->Process.UserTime, &Times.UserTime);
    }

    RtlZeroMemory(&Counters, sizeof(Counters));
    if (NT_SUCCESS(NtQueryInformationProcess(ProcessHandle, ProcessVmCounters, &Counters, sizeof(VM_COUNTERS), NULL)))
    {
        Snapshot->Process.PeakVirtualSize = Counters.PeakVirtualSize;
        Snapshot->Process.VirtualSize = Counters.VirtualSize;
        Snapshot->Process.PageFaultCount = Counters.PageFaultCount;
        Snapshot->Process.PeakWorkingSetSize = Counters.PeakWorkingSetSize;
        Snapshot->Process.WorkingSetSize = Counters.WorkingSetSize;
        Snapshot->Process.QuotaPeakPagedPoolUsage = Counters.QuotaPeakPagedPoolUsage;
        Snapshot->Process.QuotaPagedPoolUsage = Counters.QuotaPagedPoolUsage;
        Snapshot->Process.QuotaPeakNonPagedPoolUsage = Counters.QuotaPeakNonPagedPoolUsage;
        Snapshot->Process.QuotaNonPagedPoolUsage = Counters.QuotaNonPagedPoolUsage;
        Snapshot->Process.PagefileUsage = Counters.PagefileUsage;
        Snapshot->Process.PeakPagefileUsage = Counters.PeakPagefileUsage;
        Snapshot->Process.PrivateUsage = Counters.PagefileUsage;
    }

    Snapshot->Process.PriorityClass = GetPriorityClass(ProcessHandle);
    Length = ARRAYSIZE(Snapshot->Process.ImageFileName);
    if (!QueryFullProcessImageNameW(ProcessHandle, 0, Snapshot->Process.ImageFileName, &Length))
        Snapshot->Process.ImageFileName[0] = UNICODE_NULL;

    *SnapshotHandle = (HANDLE)Snapshot;
    return ERROR_SUCCESS;
}

DWORD
WINAPI
PssQuerySnapshot(
    _In_ HANDLE SnapshotHandle,
    _In_ DWORD InformationClass,
    _Out_writes_bytes_(BufferLength) void *Buffer,
    _In_ DWORD BufferLength)
{
    PPSS_SNAPSHOT Snapshot = (PPSS_SNAPSHOT)SnapshotHandle;

    if (!Snapshot || Snapshot->Magic != PSS_SNAPSHOT_MAGIC || !Buffer)
        return ERROR_INVALID_PARAMETER;
    if (InformationClass != K32_PSS_QUERY_PROCESS_INFORMATION)
        return ERROR_NOT_FOUND;
    if (BufferLength != sizeof(Snapshot->Process))
        return ERROR_BAD_LENGTH;
    RtlCopyMemory(Buffer, &Snapshot->Process, sizeof(Snapshot->Process));
    return ERROR_SUCCESS;
}

DWORD
WINAPI
PssFreeSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE SnapshotHandle)
{
    PPSS_SNAPSHOT Snapshot = (PPSS_SNAPSHOT)SnapshotHandle;

    if (!Snapshot || Snapshot->Magic != PSS_SNAPSHOT_MAGIC)
        return ERROR_INVALID_PARAMETER;
    Snapshot->Magic = 0;
    RtlFreeHeap(RtlGetProcessHeap(), 0, Snapshot);
    return ERROR_SUCCESS;
}
