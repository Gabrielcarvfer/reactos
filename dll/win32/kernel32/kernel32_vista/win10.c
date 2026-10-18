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
#include <appmodel.h>

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

/* MEM_PRESERVE_PLACEHOLDER: the view becomes a placeholder again */
BOOL
WINAPI
UnmapViewOfFileEx(
    _In_ PVOID BaseAddress,
    _In_ ULONG UnmapFlags)
{
    NTSTATUS Status;

    Status = NtUnmapViewOfSectionEx(NtCurrentProcess(), BaseAddress, UnmapFlags);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
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

/* The application IDs of an opened package: Microsoft Edge's msedge.dll
 * (WebView2, used by TeamViewer's window) imports it */
LONG
WINAPI
GetPackageApplicationIds(
    _In_ PVOID packageInfoReference,
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

/* APP PACKAGE NAMES (Windows 8) ********************************************/

/*
 * A package full name is "Name_Version_Architecture_ResourceId_PublisherId"
 * (the resource id may be empty), its family name "Name_PublisherId". These
 * only take strings apart; no package needs to exist.
 */
#define PACKAGE_FULL_NAME_PARTS 5

static BOOL SplitPackageName(PCWSTR Name, PCWSTR Parts[], SIZE_T Lengths[], ULONG Count)
{
    PCWSTR Start = Name, End;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        End = wcschr(Start, L'_');
        if ((i == Count - 1) != (End == NULL))
            return FALSE;
        if (!End)
            End = Start + wcslen(Start);
        Parts[i] = Start;
        Lengths[i] = End - Start;
        Start = End + 1;
    }
    /* Name, version, architecture and publisher id are never empty
     * (the resource id, the fourth part of a full name, may be) */
    for (i = 0; i < Count; i++)
    {
        if (!Lengths[i] && !(Count == PACKAGE_FULL_NAME_PARTS && i == 3))
            return FALSE;
    }
    return TRUE;
}

LONG
WINAPI
VerifyPackageFullName(
    _In_ PCWSTR packageFullName)
{
    PCWSTR Parts[PACKAGE_FULL_NAME_PARTS];
    SIZE_T Lengths[PACKAGE_FULL_NAME_PARTS];

    if (!packageFullName)
        return ERROR_INVALID_PARAMETER;
    return SplitPackageName(packageFullName, Parts, Lengths, PACKAGE_FULL_NAME_PARTS)
           ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}

LONG
WINAPI
VerifyPackageFamilyName(
    _In_ PCWSTR packageFamilyName)
{
    PCWSTR Parts[2];
    SIZE_T Lengths[2];

    if (!packageFamilyName)
        return ERROR_INVALID_PARAMETER;
    return SplitPackageName(packageFamilyName, Parts, Lengths, 2) ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}

LONG
WINAPI
PackageFamilyNameFromFullName(
    _In_ PCWSTR packageFullName,
    _Inout_ UINT32 *packageFamilyNameLength,
    _Out_writes_opt_(*packageFamilyNameLength) PWSTR packageFamilyName)
{
    PCWSTR Parts[PACKAGE_FULL_NAME_PARTS];
    SIZE_T Lengths[PACKAGE_FULL_NAME_PARTS];
    UINT32 Needed;

    if (!packageFullName || !packageFamilyNameLength ||
        !SplitPackageName(packageFullName, Parts, Lengths, PACKAGE_FULL_NAME_PARTS))
        return ERROR_INVALID_PARAMETER;

    /* "Name_PublisherId" and its terminator */
    Needed = (UINT32)(Lengths[0] + 1 + Lengths[4] + 1);
    if (!packageFamilyName || *packageFamilyNameLength < Needed)
    {
        *packageFamilyNameLength = Needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    RtlCopyMemory(packageFamilyName, Parts[0], Lengths[0] * sizeof(WCHAR));
    packageFamilyName[Lengths[0]] = L'_';
    RtlCopyMemory(packageFamilyName + Lengths[0] + 1, Parts[4], Lengths[4] * sizeof(WCHAR));
    packageFamilyName[Needed - 1] = UNICODE_NULL;
    *packageFamilyNameLength = Needed;
    return ERROR_SUCCESS;
}

#ifndef PACKAGE_NAME_MAX_LENGTH
#define PACKAGE_NAME_MAX_LENGTH 50
#endif
#ifndef PACKAGE_FULL_NAME_MAX_LENGTH
#define PACKAGE_FULL_NAME_MAX_LENGTH 127
#endif

/* The publisher id of a package: the first 64 bits of the SHA-256 of the
 * publisher (UTF-16), in base 32 (13 characters; "8wekyb3d8bbwe" for
 * Microsoft's "CN=Microsoft Corporation, O=Microsoft Corporation,
 * L=Redmond, S=Washington, C=US") */
static const ULONG Sha256K[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void Sha256Block(ULONG State[8], const UCHAR Block[64])
{
    ULONG W[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        W[i] = (Block[i * 4] << 24) | (Block[i * 4 + 1] << 16) | (Block[i * 4 + 2] << 8) | Block[i * 4 + 3];
    for (i = 16; i < 64; i++)
        W[i] = (ROR32(W[i - 2], 17) ^ ROR32(W[i - 2], 19) ^ (W[i - 2] >> 10)) + W[i - 7] +
               (ROR32(W[i - 15], 7) ^ ROR32(W[i - 15], 18) ^ (W[i - 15] >> 3)) + W[i - 16];
    a = State[0]; b = State[1]; c = State[2]; d = State[3];
    e = State[4]; f = State[5]; g = State[6]; h = State[7];
    for (i = 0; i < 64; i++)
    {
        t1 = h + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + Sha256K[i] + W[i];
        t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    State[0] += a; State[1] += b; State[2] += c; State[3] += d;
    State[4] += e; State[5] += f; State[6] += g; State[7] += h;
}

static void PublisherIdFromPublisher(PCWSTR Publisher, WCHAR Id[14])
{
    static const char Digits[] = "0123456789abcdefghjkmnpqrstvwxyz";
    ULONG State[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    UCHAR Block[64];
    ULONGLONG Bits = 0, Hash;
    SIZE_T Length = wcslen(Publisher) * sizeof(WCHAR), Done = 0, Used = 0;
    const UCHAR *Data = (const UCHAR *)Publisher;
    int i;

    /* SHA-256 of the UTF-16LE bytes */
    while (Done < Length)
    {
        Block[Used++] = Data[Done++];
        if (Used == 64)
        {
            Sha256Block(State, Block);
            Used = 0;
        }
    }
    Bits = (ULONGLONG)Length * 8;
    Block[Used++] = 0x80;
    if (Used > 56)
    {
        while (Used < 64) Block[Used++] = 0;
        Sha256Block(State, Block);
        Used = 0;
    }
    while (Used < 56) Block[Used++] = 0;
    for (i = 7; i >= 0; i--) Block[Used++] = (UCHAR)(Bits >> (i * 8));
    Sha256Block(State, Block);

    /* The first 8 bytes, 5 bits at a time (the last digit gets 4 bits and a 0) */
    Hash = ((ULONGLONG)State[0] << 32) | State[1];
    for (i = 0; i < 12; i++)
        Id[i] = Digits[(Hash >> (59 - i * 5)) & 31];
    Id[12] = Digits[(Hash & 15) << 1];
    Id[13] = UNICODE_NULL;
}

static PCWSTR ArchitectureName(UINT32 Architecture)
{
    switch (Architecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        case PROCESSOR_ARCHITECTURE_ARM: return L"arm";
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
        case 11 /* PROCESSOR_ARCHITECTURE_NEUTRAL */: return L"neutral";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"arm64";
        case 14 /* PROCESSOR_ARCHITECTURE_IA32_ON_ARM64 */: return L"x86a64";
        default: return NULL;
    }
}

static LONG CopyPackageName(PCWSTR Name, UINT32 *Length, PWSTR Buffer)
{
    UINT32 Needed = (UINT32)wcslen(Name) + 1;

    if (!Buffer || *Length < Needed)
    {
        *Length = Needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    RtlCopyMemory(Buffer, Name, Needed * sizeof(WCHAR));
    *Length = Needed;
    return ERROR_SUCCESS;
}

/* "Name_PublisherId" of a package id (Microsoft.WindowsAppRuntime.dll, used
 * by PowerToys, imports it) */
LONG
WINAPI
PackageFamilyNameFromId(
    _In_ const PACKAGE_ID *packageId,
    _Inout_ UINT32 *packageFamilyNameLength,
    _Out_writes_opt_(*packageFamilyNameLength) PWSTR packageFamilyName)
{
    WCHAR Name[PACKAGE_NAME_MAX_LENGTH + 1 + 14], Id[14];

    if (!packageId || !packageFamilyNameLength || !packageId->name || !packageId->publisher ||
        !packageId->name[0] || wcslen(packageId->name) > PACKAGE_NAME_MAX_LENGTH)
        return ERROR_INVALID_PARAMETER;

    PublisherIdFromPublisher(packageId->publisher, Id);
    _snwprintf(Name, ARRAYSIZE(Name), L"%s_%s", packageId->name, Id);
    Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
    return CopyPackageName(Name, packageFamilyNameLength, packageFamilyName);
}

/* "Name_Version_Architecture_ResourceId_PublisherId" of a package id */
LONG
WINAPI
PackageFullNameFromId(
    _In_ const PACKAGE_ID *packageId,
    _Inout_ UINT32 *packageFullNameLength,
    _Out_writes_opt_(*packageFullNameLength) PWSTR packageFullName)
{
    WCHAR Name[PACKAGE_FULL_NAME_MAX_LENGTH + 1], Id[14];
    PCWSTR Architecture;

    if (!packageId || !packageFullNameLength || !packageId->name || !packageId->publisher ||
        !packageId->name[0] || wcslen(packageId->name) > PACKAGE_NAME_MAX_LENGTH ||
        !(Architecture = ArchitectureName(packageId->processorArchitecture)))
        return ERROR_INVALID_PARAMETER;

    PublisherIdFromPublisher(packageId->publisher, Id);
    _snwprintf(Name, ARRAYSIZE(Name), L"%s_%u.%u.%u.%u_%s_%s_%s", packageId->name,
               packageId->version.Major, packageId->version.Minor,
               packageId->version.Build, packageId->version.Revision, Architecture,
               packageId->resourceId ? packageId->resourceId : L"", Id);
    Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
    return CopyPackageName(Name, packageFullNameLength, packageFullName);
}

/* GEOGRAPHIC LOCATION (Windows 10 1709) ************************************/

/* The two-letter ISO 3166 name of the user's location (UWP programs,
 * Windows.Globalization) */
int
WINAPI
GetUserDefaultGeoName(
    _Out_writes_opt_(geoNameCount) LPWSTR geoName,
    _In_ int geoNameCount)
{
    WCHAR Name[16];
    int Length;

    if (geoNameCount < 0 || (geoNameCount && !geoName))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Length = GetGeoInfoW(GetUserGeoID(GEOCLASS_NATION), GEO_ISO2, Name, ARRAYSIZE(Name), 0);
    if (!Length)
    {
        /* No location set: Windows answers "world" */
        wcscpy(Name, L"001");
        Length = 4;
    }
    if (!geoNameCount)
        return Length;
    if (geoNameCount < Length)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    RtlCopyMemory(geoName, Name, Length * sizeof(WCHAR));
    return Length;
}
