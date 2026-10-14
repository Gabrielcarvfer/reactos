/*
 * PROJECT:     ReactOS WoW64
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The CSR client of 32-bit programs: the Csr* routines of the
 *              32-bit ntdll come here, and their messages go to CSRSS in
 *              64-bit form, through the 64-bit CSR client of ntdll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winbase_undoc.h>
#include <wingdi.h>
#include <winreg.h>
#include <wincon.h>
#include <wincon_undoc.h>
#include <winuser.h>
#define NTOS_MODE_USER
#include <ndk/ntndk.h>
#include <csr/csr.h>
#include <win/base.h>
#include <win/basemsg.h>
#include <win/console.h>
#include <win/conmsg.h>
#include <win/winmsg.h>
#include <string.h>

/* The conversions between the 32-bit and the 64-bit messages */
struct csr_field
{
    ULONG off32;
    ULONG off64;
    ULONG kind;
};

struct csr_api
{
    ULONG size32;
    ULONG size64;
    void (*to64)(void *dst, const void *src);
    void (*to32)(void *dst, const void *src);
    const struct csr_field *fields;
};

#define CSR_FIELD_PTR    0
#define CSR_FIELD_HANDLE 1
#define CSR_FIELD_PTRINT 2

#include "csr_thunks.h"

/* From reactos.c */
extern void *wow64_find_export(HMODULE module, const char *name);
extern void wow64_protect_image32(void *base, BOOL writable);

/* A 32-bit capture buffer. The 32-bit ntdll records the address of each
 * message field that points into it. */
typedef struct _CSR_CAPTURE_BUFFER32
{
    ULONG Size;
    ULONG PreviousCaptureBuffer;
    ULONG PointerCount;
    ULONG BufferEnd;
    ULONG PointerOffsetsArray[ANYSIZE_ARRAY];
} CSR_CAPTURE_BUFFER32, *PCSR_CAPTURE_BUFFER32;

/* What wow64 keeps in front of each 32-bit capture buffer */
typedef struct _CAPTURE_HEADER32
{
    ULONG ArgumentCount;
    ULONG DataStart;
    ULONG64 Alignment;
} CAPTURE_HEADER32, *PCAPTURE_HEADER32;

/* A 32-bit CSR message: the 32-bit PORT_MESSAGE, then the CSR header */
typedef struct _CSR_API_MESSAGE32
{
    BYTE Header[0x18];
    ULONG CsrCaptureData;
    CSR_API_NUMBER ApiNumber;
    NTSTATUS Status;
    ULONG Reserved;
    BYTE Data[ANYSIZE_ARRAY];
} CSR_API_MESSAGE32, *PCSR_API_MESSAGE32;

C_ASSERT(FIELD_OFFSET(CSR_API_MESSAGE32, Data) == 0x28);

#ifndef CSR_SERVER_DLL_MAX
#define CSR_SERVER_DLL_MAX 4
#endif

/* The 32-bit copies of the static data of the servers */
static ULONG *StaticServerData32;

/* CAPTURE BUFFERS *********************************************************/

static PCSR_CAPTURE_BUFFER32
AllocateCaptureBuffer32(ULONG ArgumentCount, ULONG BufferSize)
{
    PCAPTURE_HEADER32 Header;
    PCSR_CAPTURE_BUFFER32 CaptureBuffer;
    ULONG Size;

    if (ArgumentCount > (MAXLONG / sizeof(ULONG)) / 4 || BufferSize >= MAXLONG / 2)
        return NULL;

    /* Like the 32-bit CsrAllocateCaptureBuffer: header, offsets, data,
     * and 3 bytes of alignment for each argument */
    Size = FIELD_OFFSET(CSR_CAPTURE_BUFFER32, PointerOffsetsArray) +
           ArgumentCount * sizeof(ULONG) + BufferSize + ArgumentCount * 3;
    Size = (Size + 3) & ~3;

    Header = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Header) + Size);
    if (!Header) return NULL;
    CaptureBuffer = (PCSR_CAPTURE_BUFFER32)(Header + 1);
    CaptureBuffer->Size = Size;
    CaptureBuffer->BufferEnd = PtrToUlong(&CaptureBuffer->PointerOffsetsArray[ArgumentCount]);
    Header->ArgumentCount = ArgumentCount;
    Header->DataStart = CaptureBuffer->BufferEnd;
    return CaptureBuffer;
}

static ULONG
AllocateMessagePointer32(PCSR_CAPTURE_BUFFER32 CaptureBuffer, ULONG MessageLength, PULONG CapturedData)
{
    if (MessageLength == 0)
    {
        *CapturedData = 0;
        CapturedData = NULL;
    }
    else
    {
        *CapturedData = CaptureBuffer->BufferEnd;
        if (MessageLength >= MAXLONG) return 0;
        MessageLength = (MessageLength + 3) & ~3;
        CaptureBuffer->BufferEnd += MessageLength;
    }
    CaptureBuffer->PointerOffsetsArray[CaptureBuffer->PointerCount++] = PtrToUlong(CapturedData);
    return MessageLength;
}

static VOID
CaptureMessageString32(PCSR_CAPTURE_BUFFER32 CaptureBuffer, PCSTR String, ULONG StringLength,
                       ULONG MaximumLength, PSTRING32 CapturedString)
{
    if (!String)
    {
        CapturedString->Length = 0;
        CapturedString->MaximumLength = (USHORT)MaximumLength;
        AllocateMessagePointer32(CaptureBuffer, MaximumLength, &CapturedString->Buffer);
    }
    else
    {
        if (StringLength > MaximumLength) StringLength = MaximumLength;
        CapturedString->Length = (USHORT)StringLength;
        CapturedString->MaximumLength =
            (USHORT)AllocateMessagePointer32(CaptureBuffer, MaximumLength, &CapturedString->Buffer);
        if (StringLength)
            RtlMoveMemory(ULongToPtr(CapturedString->Buffer), String, StringLength);
    }
    if (CapturedString->Length < CapturedString->MaximumLength)
        ((PCHAR)ULongToPtr(CapturedString->Buffer))[CapturedString->Length] = ANSI_NULL;
}

static NTSTATUS WINAPI wow64_CsrAllocateCaptureBuffer(UINT *args)
{
    return (NTSTATUS)PtrToUlong(AllocateCaptureBuffer32(args[0], args[1]));
}

static NTSTATUS WINAPI wow64_CsrAllocateMessagePointer(UINT *args)
{
    return AllocateMessagePointer32(ULongToPtr(args[0]), args[1], ULongToPtr(args[2]));
}

static NTSTATUS WINAPI wow64_CsrCaptureMessageBuffer(UINT *args)
{
    PCSR_CAPTURE_BUFFER32 CaptureBuffer = ULongToPtr(args[0]);
    PVOID MessageBuffer = ULongToPtr(args[1]);
    ULONG MessageLength = args[2];
    PULONG CapturedData = ULongToPtr(args[3]);

    AllocateMessagePointer32(CaptureBuffer, MessageLength, CapturedData);
    if (MessageBuffer && MessageLength)
        RtlMoveMemory(ULongToPtr(*CapturedData), MessageBuffer, MessageLength);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI wow64_CsrCaptureMessageString(UINT *args)
{
    CaptureMessageString32(ULongToPtr(args[0]), ULongToPtr(args[1]), args[2], args[3], ULongToPtr(args[4]));
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI wow64_CsrCaptureMessageMultiUnicodeStringsInPlace(UINT *args)
{
    PULONG CaptureBuffer = ULongToPtr(args[0]);
    ULONG StringsCount = args[1];
    PULONG MessageStrings = ULongToPtr(args[2]);
    ULONG Count, Size = 0;

    if (!CaptureBuffer) return STATUS_INVALID_PARAMETER;
    if (!*CaptureBuffer)
    {
        for (Count = 0; Count < StringsCount; Count++)
        {
            if (MessageStrings[Count])
                Size += ((PSTRING32)ULongToPtr(MessageStrings[Count]))->MaximumLength;
        }
        *CaptureBuffer = PtrToUlong(AllocateCaptureBuffer32(StringsCount, Size));
        if (!*CaptureBuffer) return STATUS_NO_MEMORY;
    }
    for (Count = 0; Count < StringsCount; Count++)
    {
        PSTRING32 String = ULongToPtr(MessageStrings[Count]);

        if (!String) continue;
        CaptureMessageString32(ULongToPtr(*CaptureBuffer), ULongToPtr(String->Buffer),
                               String->Length, String->MaximumLength, String);
        if (String->Length + sizeof(WCHAR) <= String->MaximumLength)
            ((PWCHAR)ULongToPtr(String->Buffer))[String->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI wow64_CsrFreeCaptureBuffer(UINT *args)
{
    PCSR_CAPTURE_BUFFER32 CaptureBuffer = ULongToPtr(args[0]);

    RtlFreeHeap(RtlGetProcessHeap(), 0, (PCAPTURE_HEADER32)CaptureBuffer - 1);
    return STATUS_SUCCESS;
}

/* STATIC SERVER DATA ******************************************************/

static PWCHAR CopyString32(UNICODE_STRING_32 *Dst, const UNICODE_STRING *Src, PWCHAR Buffer)
{
    Dst->Length = Src->Length;
    Dst->MaximumLength = Src->MaximumLength;
    Dst->Buffer = 0;
    if (!Src->Buffer) return Buffer;
    RtlCopyMemory(Buffer, Src->Buffer, Src->MaximumLength);
    Dst->Buffer = PtrToUlong(Buffer);
    return (PWCHAR)((PBYTE)Buffer + ((Src->MaximumLength + 3) & ~3));
}

/* kernel32 reads the static data of the base server from the PEB: give the
 * 32-bit PEB a 32-bit copy of it */
static VOID UpdateStaticServerData32(VOID)
{
    PPEB Peb = NtCurrentPeb();
    PTEB32 Teb32 = (PTEB32)((PBYTE)NtCurrentTeb() + 0x2000);
    struct _PEB32 *Peb32 = ULongToPtr(Teb32->ProcessEnvironmentBlock);
    PBASE_STATIC_SERVER_DATA Base;
    BASE_STATIC_SERVER_DATA_32 *Base32;
    PWCHAR Strings;
    SIZE_T Size;

    if (!Peb->ReadOnlyStaticServerData) return;
    if (!StaticServerData32)
    {
        StaticServerData32 = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY,
                                             CSR_SERVER_DLL_MAX * sizeof(ULONG));
        if (!StaticServerData32) return;
        Peb32->ReadOnlyStaticServerData = PtrToUlong(StaticServerData32);
    }

    Base = Peb->ReadOnlyStaticServerData[BASESRV_SERVERDLL_INDEX];
    if (Base && !StaticServerData32[BASESRV_SERVERDLL_INDEX])
    {
        WCHAR SysWow64Buffer[MAX_PATH];
        UNICODE_STRING SysWow64;

        /* A 32-bit program's system directory is SysWOW64 (GetSystemDirectory,
         * the DLL search path, GetSystemWow64Directory) */
        RtlInitEmptyUnicodeString(&SysWow64, SysWow64Buffer, sizeof(SysWow64Buffer) - sizeof(WCHAR));
        RtlAppendUnicodeStringToString(&SysWow64, &Base->WindowsDirectory);
        RtlAppendUnicodeToString(&SysWow64, L"\\SysWOW64");
        SysWow64.Buffer[SysWow64.Length / sizeof(WCHAR)] = UNICODE_NULL;
        SysWow64.MaximumLength = SysWow64.Length + sizeof(WCHAR);

        Size = sizeof(*Base32) + Base->WindowsDirectory.MaximumLength +
               2 * SysWow64.MaximumLength + Base->NamedObjectDirectory.MaximumLength + 16;
        Base32 = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (!Base32) return;
        BASE_STATIC_SERVER_DATA_64to32(Base32, Base);
        Strings = (PWCHAR)(Base32 + 1);
        Strings = CopyString32(&Base32->WindowsDirectory, &Base->WindowsDirectory, Strings);
        Strings = CopyString32(&Base32->WindowsSystemDirectory, &SysWow64, Strings);
        Strings = CopyString32(&Base32->NamedObjectDirectory, &Base->NamedObjectDirectory, Strings);
        Strings = CopyString32(&Base32->WindowsSys32x86Directory, &SysWow64, Strings);
        /* FIXME: the INI file mapping (a tree of 64-bit structures) */
        Base32->IniFileMapping = 0;
        /* What a 32-bit program sees of the machine */
        Base32->SysInfo.MaximumUserModeAddress = 0x7ffeffff;
        StaticServerData32[BASESRV_SERVERDLL_INDEX] = PtrToUlong(Base32);
    }
}

/* CONNECTION AND CALLS ****************************************************/

static NTSTATUS WINAPI wow64_CsrClientConnectToServer(UINT *args)
{
    PWSTR ObjectDirectory = ULongToPtr(args[0]);
    ULONG ServerId = args[1];
    PVOID ConnectionInfo32 = ULongToPtr(args[2]);
    PULONG ConnectionInfoSize32 = ULongToPtr(args[3]);
    PBOOLEAN ServerToServerCall = ULongToPtr(args[4]);
    union
    {
        BASESRV_API_CONNECTINFO Base;
        CONSRV_API_CONNECTINFO Console;
        BYTE User[0x400];
    } Info;
    ULONG Size = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Info, sizeof(Info));
    if (ConnectionInfo32)
    {
        switch (ServerId)
        {
            case BASESRV_SERVERDLL_INDEX:
                Info.Base = *(PBASESRV_API_CONNECTINFO)ConnectionInfo32;
                Size = sizeof(Info.Base);
                break;

            case CONSRV_SERVERDLL_INDEX:
                /* (the console server starts the Ctrl+C, properties and IME
                 * routines in new threads: wow64 runs them as 32-bit code) */
                CONSRV_API_CONNECTINFO_32to64(&Info.Console, ConnectionInfo32);
                Size = sizeof(Info.Console);
                break;

            case USERSRV_SERVERDLL_INDEX:
                /* The 32-bit user32 of 64-bit ReactOS has it with the 64-bit
                 * layout already (USERCONNECT, see win32ss/include/kptr.h) */
                Size = ConnectionInfoSize32 ? min(*ConnectionInfoSize32, sizeof(Info.User)) : 0;
                RtlCopyMemory(Info.User, ConnectionInfo32, Size);
                break;

            default:
                DbgPrint("WOW64: CSR server %lu cannot be connected to yet\n", ServerId);
                return STATUS_NOT_SUPPORTED;
        }
    }

    Status = CsrClientConnectToServer(ObjectDirectory,
                                      ServerId,
                                      ConnectionInfo32 ? &Info : NULL,
                                      ConnectionInfo32 ? &Size : NULL,
                                      ServerToServerCall);

    if (ConnectionInfo32)
    {
        switch (ServerId)
        {
            case BASESRV_SERVERDLL_INDEX:
                *(PBASESRV_API_CONNECTINFO)ConnectionInfo32 = Info.Base;
                if (ConnectionInfoSize32) *ConnectionInfoSize32 = sizeof(Info.Base);
                break;

            case CONSRV_SERVERDLL_INDEX:
                CONSRV_API_CONNECTINFO_64to32(ConnectionInfo32, &Info.Console);
                if (ConnectionInfoSize32) *ConnectionInfoSize32 = sizeof(CONSRV_API_CONNECTINFO_32);
                break;

            case USERSRV_SERVERDLL_INDEX:
                RtlCopyMemory(ConnectionInfo32, Info.User, Size);
                if (ConnectionInfoSize32) *ConnectionInfoSize32 = Size;
                break;
        }
    }

    UpdateStaticServerData32();
    return Status;
}

static const struct csr_api *GetApi(CSR_API_NUMBER ApiNumber)
{
    ULONG Server = CSR_API_NUMBER_TO_SERVER_ID(ApiNumber);
    ULONG Api = CSR_API_NUMBER_TO_API_ID(ApiNumber);

    switch (Server)
    {
        case BASESRV_SERVERDLL_INDEX:
            Api -= BASESRV_FIRST_API_NUMBER;
            return Api < ARRAYSIZE(csr_apis_1) ? &csr_apis_1[Api] : NULL;
        case CONSRV_SERVERDLL_INDEX:
            Api -= CONSRV_FIRST_API_NUMBER;
            return Api < ARRAYSIZE(csr_apis_2) ? &csr_apis_2[Api] : NULL;
        case USERSRV_SERVERDLL_INDEX:
            Api -= USERSRV_FIRST_API_NUMBER;
            return Api < ARRAYSIZE(csr_apis_3) ? &csr_apis_3[Api] : NULL;
    }
    return NULL;
}

static BOOLEAN FindField(const struct csr_api *Api, ULONG Offset32, PULONG Offset64)
{
    const struct csr_field *Field;

    if (!Api->fields) return FALSE;
    for (Field = Api->fields; Field->off32 != ~0u; Field++)
    {
        if (Field->off32 == Offset32 && Field->kind == CSR_FIELD_PTR)
        {
            *Offset64 = Field->off64;
            return TRUE;
        }
    }
    return FALSE;
}

static NTSTATUS WINAPI wow64_CsrClientCallServer(UINT *args)
{
    PCSR_API_MESSAGE32 Message32 = ULongToPtr(args[0]);
    PCSR_CAPTURE_BUFFER32 CaptureBuffer32 = ULongToPtr(args[1]);
    CSR_API_NUMBER ApiNumber = args[2];
    ULONG DataLength32 = args[3];
    const struct csr_api *Api = GetApi(ApiNumber);
    PCSR_CAPTURE_BUFFER CaptureBuffer = NULL;
    PCAPTURE_HEADER32 Header32 = NULL;
    PBYTE Data32 = Message32->Data;
    PBYTE Data64, CapturedData = NULL;
    ULONG Size32, Size64, Used = 0, i;
    CSR_API_MESSAGE Message;
    struct
    {
        PULONG Field;
        ULONG Value;
    } Captured[32];
    ULONG CapturedCount = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Message, sizeof(Message));
    Data64 = (PBYTE)&Message.Data;

    if (Api && Api->size32)
    {
        Size32 = Api->size32;
        Size64 = Api->size64;
    }
    else
    {
        /* Not described: it has the same layout in 32 and 64 bits, or the
         * server does not implement it */
        DbgPrint("WOW64: CSR API %lx is not described\n", ApiNumber);
        Size32 = Size64 = min(DataLength32, sizeof(Message.Data));
        Api = NULL;
    }

    if (Api && Api->to64)
        Api->to64(Data64, Data32);
    else
        RtlCopyMemory(Data64, Data32, Size32);

    if (CaptureBuffer32)
    {
        Header32 = (PCAPTURE_HEADER32)CaptureBuffer32 - 1;
        Used = CaptureBuffer32->BufferEnd - Header32->DataStart;
        CaptureBuffer = CsrAllocateCaptureBuffer(Header32->ArgumentCount, Used);
        if (!CaptureBuffer) return STATUS_NO_MEMORY;

        /* Same data, at the same offsets; then point the message fields to it */
        CapturedData = CaptureBuffer->BufferEnd;
        RtlCopyMemory(CapturedData, ULongToPtr(Header32->DataStart), Used);
        CaptureBuffer->BufferEnd = CapturedData + Used;
        for (i = 0; i < CaptureBuffer32->PointerCount; i++)
        {
            ULONG Address32 = CaptureBuffer32->PointerOffsetsArray[i];
            ULONG Offset64, Value;
            PVOID *Field64;

            if (!Address32)
            {
                CaptureBuffer->PointerOffsetsArray[CaptureBuffer->PointerCount++] = 0;
                continue;
            }
            if (!Api || !FindField(Api, Address32 - PtrToUlong(Data32), &Offset64) ||
                CapturedCount == ARRAYSIZE(Captured))
            {
                DbgPrint("WOW64: CSR API %lx: no 64-bit field for offset %lx\n",
                         ApiNumber, Address32 - PtrToUlong(Data32));
                CsrFreeCaptureBuffer(CaptureBuffer);
                return STATUS_INVALID_PARAMETER;
            }
            Value = *(PULONG)ULongToPtr(Address32);
            Captured[CapturedCount].Field = ULongToPtr(Address32);
            Captured[CapturedCount].Value = Value;
            CapturedCount++;

            Field64 = (PVOID *)(Data64 + Offset64);
            *Field64 = Value ? CapturedData + (Value - Header32->DataStart) : NULL;
            CaptureBuffer->PointerOffsetsArray[CaptureBuffer->PointerCount++] = (ULONG_PTR)Field64;
        }
    }

    Status = CsrClientCallServer(&Message, CaptureBuffer, ApiNumber, Size64);

    /* And back */
    if (Api && Api->to32)
        Api->to32(Data32, Data64);
    else
        RtlCopyMemory(Data32, Data64, Size32);

    if (CaptureBuffer)
    {
        RtlCopyMemory(ULongToPtr(Header32->DataStart), CapturedData, Used);
        for (i = 0; i < CapturedCount; i++)
            *Captured[i].Field = Captured[i].Value;
        CsrFreeCaptureBuffer(CaptureBuffer);
    }

    Message32->ApiNumber = ApiNumber;
    Message32->Status = Message.Status;
    if (!NT_SUCCESS(Status))
        DbgPrint("WOW64: CSR API %lx failed: %lx\n", ApiNumber, Status);
    return Status;
}

static NTSTATUS WINAPI wow64_CsrGetProcessId(UINT *args)
{
    return HandleToUlong(CsrGetProcessId());
}

static NTSTATUS WINAPI wow64_CsrIdentifyAlertableThread(UINT *args)
{
    return CsrIdentifyAlertableThread();
}

static NTSTATUS WINAPI wow64_CsrNewThread(UINT *args)
{
    return CsrNewThread();
}

static NTSTATUS WINAPI wow64_CsrSetPriorityClass(UINT *args)
{
    return CsrSetPriorityClass(LongToHandle(args[0]), ULongToPtr(args[1]));
}

/* THE 32-BIT ENTRIES ******************************************************/

static const struct
{
    const char *Name;
    NTSTATUS (WINAPI *Thunk)(UINT *args);
    ULONG ArgBytes;
} CsrFunctions[] =
{
    { "CsrAllocateCaptureBuffer", wow64_CsrAllocateCaptureBuffer, 8 },
    { "CsrAllocateMessagePointer", wow64_CsrAllocateMessagePointer, 12 },
    { "CsrCaptureMessageBuffer", wow64_CsrCaptureMessageBuffer, 16 },
    { "CsrCaptureMessageMultiUnicodeStringsInPlace", wow64_CsrCaptureMessageMultiUnicodeStringsInPlace, 12 },
    { "CsrCaptureMessageString", wow64_CsrCaptureMessageString, 20 },
    { "CsrClientCallServer", wow64_CsrClientCallServer, 16 },
    { "CsrClientConnectToServer", wow64_CsrClientConnectToServer, 20 },
    { "CsrFreeCaptureBuffer", wow64_CsrFreeCaptureBuffer, 4 },
    { "CsrGetProcessId", wow64_CsrGetProcessId, 0 },
    { "CsrIdentifyAlertableThread", wow64_CsrIdentifyAlertableThread, 0 },
    { "CsrNewThread", wow64_CsrNewThread, 0 },
    { "CsrSetPriorityClass", wow64_CsrSetPriorityClass, 8 },
};

static ULONG_PTR CsrTable[sizeof(CsrFunctions) / sizeof(CsrFunctions[0])];
static BYTE CsrArgs[sizeof(CsrFunctions) / sizeof(CsrFunctions[0])];

/* The system call table of these entries: table 2 (numbers 0x2000 and up) */
#define CSR_TABLE_INDEX 2

VOID wow64_get_csr_table(ULONG_PTR **Table, ULONG *Count, BYTE **Args)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(CsrFunctions); i++)
    {
        CsrTable[i] = (ULONG_PTR)CsrFunctions[i].Thunk;
        CsrArgs[i] = (BYTE)CsrFunctions[i].ArgBytes;
    }
    *Table = CsrTable;
    *Count = ARRAYSIZE(CsrFunctions);
    *Args = CsrArgs;
}

/* Make the Csr* routines of the 32-bit ntdll jump to 32-bit stubs that enter
 * wow64 like system calls do:
 *     B8 nn nn 00 00       mov eax, 2000h + n
 *     64 FF 15 C0 00 00 00 call dword ptr fs:[0C0h]
 *     C2 nn 00             ret <arguments>
 */
VOID wow64_init_csr(HMODULE Ntdll32)
{
    PBYTE Stubs = NULL, Code;
    SIZE_T Size = 0x1000;
    ULONG OldProtect, i;

    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), (PVOID *)&Stubs, 0x7fffffff, &Size,
                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))
        return;

    wow64_protect_image32(Ntdll32, TRUE);
    for (i = 0; i < ARRAYSIZE(CsrFunctions); i++)
    {
        PBYTE Stub = Stubs + i * 16;

        Stub[0] = 0xb8;
        *(PULONG)(Stub + 1) = (CSR_TABLE_INDEX << 12) + i;
        Stub[5] = 0x64;
        Stub[6] = 0xff;
        Stub[7] = 0x15;
        *(PULONG)(Stub + 8) = 0xc0;
        Stub[12] = 0xc2;
        *(PUSHORT)(Stub + 13) = (USHORT)CsrFunctions[i].ArgBytes;

        Code = wow64_find_export(Ntdll32, CsrFunctions[i].Name);
        if (!Code)
        {
            DbgPrint("WOW64: no %s in the 32-bit ntdll\n", CsrFunctions[i].Name);
            continue;
        }
        Code[0] = 0xe9;  /* jmp rel32 */
        *(PULONG)(Code + 1) = PtrToUlong(Stub) - (PtrToUlong(Code) + 5);
    }
    wow64_protect_image32(Ntdll32, FALSE);
    NtProtectVirtualMemory(NtCurrentProcess(), (PVOID *)&Stubs, &Size, PAGE_EXECUTE_READ, &OldProtect);
}
