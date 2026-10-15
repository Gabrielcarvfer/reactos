/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 8 and 10 kernel32 functions used by modern applications:
 *              process mitigations and information, CPU sets, overlapped
 *              results with a timeout, app packages (Chromium and Electron
 *              programs, Firefox, OBS)
 */

#include <k32_vista.h>
#include <ndk/exfuncs.h>
#include <ndk/mmfuncs.h>

#define NDEBUG
#include <debug.h>

/* PROCESS MITIGATIONS (Windows 8) ******************************************/

/* (winnt.h has them only for Windows 8 and later) */
#define K32ProcessDEPPolicy 0
#define K32ProcessASLRPolicy 1
#define K32ProcessMitigationOptionsMask 9
#define K32MaxProcessMitigationPolicy 22

/*
 * ReactOS enforces none of the mitigations a process can ask for (other than
 * DEP, always on in 64-bit processes): every policy reads as off, and setting
 * one is accepted. (Chromium's sandbox sets several and checks the result.)
 */
BOOL
WINAPI
GetProcessMitigationPolicy(
    _In_ HANDLE hProcess,
    _In_ DWORD MitigationPolicy,
    _Out_writes_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    if (!lpBuffer || MitigationPolicy >= K32MaxProcessMitigationPolicy ||
        (MitigationPolicy == K32ProcessDEPPolicy ? dwLength < 2 * sizeof(DWORD) : dwLength < sizeof(DWORD)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RtlZeroMemory(lpBuffer, dwLength);
#ifdef _WIN64
    if (MitigationPolicy == K32ProcessDEPPolicy)
    {
        /* Enable, and Permanent */
        ((DWORD *)lpBuffer)[0] = 1;
        ((BOOLEAN *)lpBuffer)[sizeof(DWORD)] = TRUE;
    }
#endif
    return TRUE;
}

BOOL
WINAPI
SetProcessMitigationPolicy(
    _In_ DWORD MitigationPolicy,
    _In_reads_bytes_(dwLength) PVOID lpBuffer,
    _In_ SIZE_T dwLength)
{
    if (!lpBuffer || MitigationPolicy >= K32MaxProcessMitigationPolicy ||
        MitigationPolicy == K32ProcessMitigationOptionsMask)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DPRINT("SetProcessMitigationPolicy(%lu): accepted, not enforced\n", MitigationPolicy);
    return TRUE;
}

/* PROCESS INFORMATION (Windows 8) ******************************************/

#define K32ProcessMemoryPriority 0
#define K32ProcessMemoryExhaustionInfo 1
#define K32ProcessPowerThrottling 4
#define K32ProcessProtectionLevelInfo 7
#define K32ProcessLeapSecondInfo 8
#define K32ProcessMachineTypeInfo 9

#define K32_PROTECTION_LEVEL_NONE 0xFFFFFFFE
#define K32_MEMORY_PRIORITY_NORMAL 5

typedef struct _K32_PROCESS_MACHINE_INFORMATION
{
    USHORT ProcessMachine;
    USHORT Res0;
    DWORD MachineAttributes;
} K32_PROCESS_MACHINE_INFORMATION;

/* The machine a process runs (it can be another process) */
static USHORT GetProcessMachine(HANDLE hProcess, PUSHORT NativeMachine)
{
    ULONG_PTR Wow64 = 0;

    NtQueryInformationProcess(hProcess, ProcessWow64Information, &Wow64, sizeof(Wow64), NULL);
#if defined(_M_AMD64)
    *NativeMachine = IMAGE_FILE_MACHINE_AMD64;
#else
    {
        /* A 32-bit program on a 64-bit system: this process is under WoW64 */
        ULONG_PTR SelfWow64 = 0;
        NtQueryInformationProcess(NtCurrentProcess(), ProcessWow64Information,
                                  &SelfWow64, sizeof(SelfWow64), NULL);
        *NativeMachine = SelfWow64 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386;
    }
#endif
    return Wow64 ? IMAGE_FILE_MACHINE_I386 : *NativeMachine;
}

/*
 * ReactOS has no memory priorities, power throttling or memory exhaustion
 * notices: settings are accepted, and the process keeps running as before.
 */
BOOL
WINAPI
SetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ DWORD ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationSize) LPVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    if (ProcessInformation)
    {
        switch (ProcessInformationClass)
        {
            case K32ProcessMemoryPriority:
                if (ProcessInformationSize == sizeof(ULONG)) return TRUE;
                break;
            case K32ProcessMemoryExhaustionInfo:
                if (ProcessInformationSize == 3 * sizeof(ULONG)) return TRUE;
                break;
            case K32ProcessPowerThrottling:
                if (ProcessInformationSize == 3 * sizeof(ULONG)) return TRUE;
                break;
            case K32ProcessLeapSecondInfo:
                if (ProcessInformationSize == 2 * sizeof(ULONG)) return TRUE;
                break;
        }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL
WINAPI
GetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ DWORD ProcessInformationClass,
    _Out_writes_bytes_(ProcessInformationSize) LPVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    if (ProcessInformation)
    {
        switch (ProcessInformationClass)
        {
            case K32ProcessMemoryPriority:
                if (ProcessInformationSize != sizeof(ULONG)) break;
                *(PULONG)ProcessInformation = K32_MEMORY_PRIORITY_NORMAL;
                return TRUE;

            case K32ProcessPowerThrottling:
                if (ProcessInformationSize != 3 * sizeof(ULONG)) break;
                RtlZeroMemory(ProcessInformation, ProcessInformationSize);
                *(PULONG)ProcessInformation = 1; /* Version */
                return TRUE;

            case K32ProcessProtectionLevelInfo:
                if (ProcessInformationSize != sizeof(ULONG)) break;
                *(PULONG)ProcessInformation = K32_PROTECTION_LEVEL_NONE;
                return TRUE;

            case K32ProcessMachineTypeInfo:
            {
                K32_PROCESS_MACHINE_INFORMATION *Info = ProcessInformation;
                USHORT Native;

                if (ProcessInformationSize != sizeof(*Info)) break;
                Info->ProcessMachine = GetProcessMachine(hProcess, &Native);
                Info->Res0 = 0;
                Info->MachineAttributes = PROCESS_MACHINE_ATTRIBUTE_USER_ENABLED |
                    (Info->ProcessMachine == Native ? PROCESS_MACHINE_ATTRIBUTE_KERNEL_ENABLED |
                                                      PROCESS_MACHINE_ATTRIBUTE_NATIVE_OS
                                                    : PROCESS_MACHINE_ATTRIBUTE_WOW64_CONTAINER);
                return TRUE;
            }
        }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* Which machine a process runs, and the system's (Windows 10) */
BOOL
WINAPI
IsWow64Process2(
    _In_ HANDLE hProcess,
    _Out_ USHORT *pProcessMachine,
    _Out_opt_ USHORT *pNativeMachine)
{
    USHORT Native, Machine;

    if (!pProcessMachine)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Machine = GetProcessMachine(hProcess, &Native);
    /* (IMAGE_FILE_MACHINE_UNKNOWN: not a WoW64 process) */
    *pProcessMachine = (Machine == Native) ? IMAGE_FILE_MACHINE_UNKNOWN : Machine;
    if (pNativeMachine) *pNativeMachine = Native;
    return TRUE;
}

/* MEMORY (Windows 8) *******************************************************/

/* A hint to read pages in ahead of time: the pages come when they are used */
BOOL
WINAPI
PrefetchVirtualMemory(
    _In_ HANDLE hProcess,
    _In_ ULONG_PTR NumberOfEntries,
    _In_reads_(NumberOfEntries) PVOID VirtualAddresses,
    _In_ ULONG Flags)
{
    if (!NumberOfEntries || !VirtualAddresses || Flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* The flags (placeholders, transient boost) are not supported: a plain unmap */
BOOL
WINAPI
UnmapViewOfFileEx(
    _In_ PVOID BaseAddress,
    _In_ ULONG UnmapFlags)
{
    if (UnmapFlags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return UnmapViewOfFile(BaseAddress);
}

/* CPU SETS (Windows 10) ****************************************************/

typedef struct _K32_SYSTEM_CPU_SET_INFORMATION
{
    DWORD Size;
    DWORD Type;     /* CpuSetInformation (0) */
    DWORD Id;
    WORD Group;
    BYTE LogicalProcessorIndex;
    BYTE CoreIndex;
    BYTE LastLevelCacheIndex;
    BYTE NumaNodeIndex;
    BYTE EfficiencyClass;
    BYTE AllFlags;
    DWORD Reserved;
    DWORD64 AllocationTag;
} K32_SYSTEM_CPU_SET_INFORMATION;

/* One CPU set per processor (Ids from 0x100, as on Windows), all in group 0 */
BOOL
WINAPI
GetSystemCpuSetInformation(
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnedLength) PVOID Information,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength,
    _In_opt_ HANDLE Process,
    _Reserved_ ULONG Flags)
{
    SYSTEM_INFO SystemInfo;
    K32_SYSTEM_CPU_SET_INFORMATION *Entry = Information;
    ULONG i, Needed;

    if (!ReturnedLength || Flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    GetSystemInfo(&SystemInfo);
    Needed = SystemInfo.dwNumberOfProcessors * sizeof(*Entry);
    *ReturnedLength = Needed;
    if (!Information || BufferLength < Needed)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (i = 0; i < SystemInfo.dwNumberOfProcessors; i++, Entry++)
    {
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->Size = sizeof(*Entry);
        Entry->Id = 0x100 + i;
        Entry->LogicalProcessorIndex = (BYTE)i;
        Entry->CoreIndex = (BYTE)i;
    }
    return TRUE;
}

/* OVERLAPPED I/O (Windows 8) ***********************************************/

/* GetOverlappedResult, waiting at most dwMilliseconds, alertably if asked */
BOOL
WINAPI
GetOverlappedResultEx(
    _In_ HANDLE hFile,
    _In_ LPOVERLAPPED lpOverlapped,
    _Out_ LPDWORD lpNumberOfBytesTransferred,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable)
{
    if (lpOverlapped->Internal == STATUS_PENDING)
    {
        DWORD Wait;

        if (!dwMilliseconds)
        {
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }
        Wait = WaitForSingleObjectEx(lpOverlapped->hEvent ? lpOverlapped->hEvent : hFile,
                                     dwMilliseconds, bAlertable);
        if (Wait == WAIT_FAILED)
            return FALSE;
        if (Wait == WAIT_TIMEOUT || Wait == WAIT_IO_COMPLETION)
        {
            SetLastError(Wait);
            return FALSE;
        }
    }
    return GetOverlappedResult(hFile, lpOverlapped, lpNumberOfBytesTransferred, FALSE);
}

/* APP PACKAGES (Windows 8) *************************************************/

/*
 * No app package is installed (see GetCurrentPackageId): none has a path,
 * and none can be opened.
 */
LONG
WINAPI
GetPackagePathByFullName(
    _In_ PCWSTR packageFullName,
    _Inout_ UINT32 *pathLength,
    _Out_writes_opt_(*pathLength) PWSTR path)
{
    if (!packageFullName || !pathLength)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

LONG
WINAPI
GetCurrentPackageInfo(
    _In_ const UINT32 flags,
    _Inout_ UINT32 *bufferLength,
    _Out_writes_bytes_opt_(*bufferLength) BYTE *buffer,
    _Out_opt_ UINT32 *count)
{
    if (!bufferLength)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
OpenPackageInfoByFullName(
    _In_ PCWSTR packageFullName,
    _Reserved_ const UINT32 reserved,
    _Out_ PVOID *packageInfoReference)
{
    if (!packageFullName || !packageInfoReference || reserved)
        return ERROR_INVALID_PARAMETER;
    *packageInfoReference = NULL;
    return ERROR_NOT_FOUND;
}

LONG
WINAPI
GetPackageInfo(
    _In_ PVOID packageInfoReference,
    _In_ const UINT32 flags,
    _Inout_ UINT32 *bufferLength,
    _Out_writes_bytes_opt_(*bufferLength) BYTE *buffer,
    _Out_opt_ UINT32 *count)
{
    /* (No reference can have been opened) */
    return ERROR_INVALID_PARAMETER;
}

LONG
WINAPI
ClosePackageInfo(
    _In_ PVOID packageInfoReference)
{
    return packageInfoReference ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
}

/* HANDLES (Windows 10) *****************************************************/

/*
 * Do two handles refer to the same object? Windows asks the kernel
 * (NtCompareObjects); here the system's handle list says which object each
 * handle of this process refers to.
 */
BOOL
WINAPI
CompareObjectHandles(
    _In_ HANDLE hFirstObjectHandle,
    _In_ HANDLE hSecondObjectHandle)
{
    PSYSTEM_HANDLE_INFORMATION_EX Info = NULL;
    ULONG Size = 0x10000, i;
    ULONG_PTR ProcessId = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    PVOID First = NULL, Second = NULL;
    NTSTATUS Status;

    if (hFirstObjectHandle == hSecondObjectHandle)
        return TRUE;

    for (;;)
    {
        Info = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Info)
            return FALSE;
        Status = NtQuerySystemInformation(SystemExtendedHandleInformation, Info, Size, NULL);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        Size *= 2;
    }

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < Info->NumberOfHandles; i++)
        {
            if (Info->Handles[i].UniqueProcessId != ProcessId)
                continue;
            if (Info->Handles[i].HandleValue == (ULONG_PTR)hFirstObjectHandle)
                First = Info->Handles[i].Object;
            if (Info->Handles[i].HandleValue == (ULONG_PTR)hSecondObjectHandle)
                Second = Info->Handles[i].Object;
        }
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
    return First && First == Second;
}
