/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     SetFileInformationByHandle
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "k32_vista.h"

#include <ndk/rtlfuncs.h>
#include <ndk/iofuncs.h>

#define NDEBUG
#include <debug.h>

/* The NT classes of the Windows 10 FileDispositionInfoEx and FileRenameInfoEx */
#define FileDispositionInformationExClass 64
#define FileRenameInformationExClass      65

/* A rename gives a DOS path (or a name relative to RootDirectory): the native
 * function wants an NT path */
static NTSTATUS
SetRenameInformation(HANDLE hFile, PFILE_RENAME_INFO Info, DWORD Size, FILE_INFORMATION_CLASS Class)
{
    PFILE_RENAME_INFORMATION NativeInfo;
    UNICODE_STRING DosName, NtName;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    ULONG NativeSize;

    if (Size < FIELD_OFFSET(FILE_RENAME_INFO, FileName) ||
        Size < FIELD_OFFSET(FILE_RENAME_INFO, FileName) + Info->FileNameLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DosName.Buffer = Info->FileName;
    DosName.Length = DosName.MaximumLength = (USHORT)Info->FileNameLength;
    if (Info->RootDirectory)
    {
        /* Relative to that directory: as is */
        NtName = DosName;
    }
    else
    {
        /* The name is not necessarily terminated: copy it first */
        PWSTR Terminated = RtlAllocateHeap(RtlGetProcessHeap(), 0, Info->FileNameLength + sizeof(WCHAR));
        BOOLEAN Converted;

        if (!Terminated)
            return STATUS_NO_MEMORY;
        RtlCopyMemory(Terminated, Info->FileName, Info->FileNameLength);
        Terminated[Info->FileNameLength / sizeof(WCHAR)] = UNICODE_NULL;
        Converted = RtlDosPathNameToNtPathName_U(Terminated, &NtName, NULL, NULL);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Terminated);
        if (!Converted)
            return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    NativeSize = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + NtName.Length + sizeof(WCHAR);
    NativeInfo = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, NativeSize);
    if (NativeInfo)
    {
        /* Same layout: ReplaceIfExists (or the Flags of the Ex class), RootDirectory */
        RtlCopyMemory(NativeInfo, Info, FIELD_OFFSET(FILE_RENAME_INFO, FileNameLength));
        NativeInfo->FileNameLength = NtName.Length;
        RtlCopyMemory(NativeInfo->FileName, NtName.Buffer, NtName.Length);
        Status = NtSetInformationFile(hFile, &IoStatusBlock, NativeInfo, NativeSize, Class);
        RtlFreeHeap(RtlGetProcessHeap(), 0, NativeInfo);
    }
    else
    {
        Status = STATUS_NO_MEMORY;
    }

    if (!Info->RootDirectory)
        RtlFreeUnicodeString(&NtName);
    return Status;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetFileInformationByHandle(HANDLE hFile,
                           FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                           LPVOID lpFileInformation,
                           DWORD dwBufferSize)
{
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_INFORMATION_CLASS Class;
    NTSTATUS Status;

    switch (FileInformationClass)
    {
        case FileBasicInfo:          Class = FileBasicInformation; break;
        case FileDispositionInfo:    Class = FileDispositionInformation; break;
        case FileAllocationInfo:     Class = FileAllocationInformation; break;
        case FileEndOfFileInfo:      Class = FileEndOfFileInformation; break;
        case FileIoPriorityHintInfo: Class = FileIoPriorityHintInformation; break;
        case FileDispositionInfoEx:  Class = (FILE_INFORMATION_CLASS)FileDispositionInformationExClass; break;

        case FileRenameInfo:
        case FileRenameInfoEx:
            Status = SetRenameInformation(hFile, lpFileInformation, dwBufferSize,
                                          (FileInformationClass == FileRenameInfo) ?
                                          FileRenameInformation :
                                          (FILE_INFORMATION_CLASS)FileRenameInformationExClass);
            goto Done;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    Status = NtSetInformationFile(hFile, &IoStatusBlock, lpFileInformation, dwBufferSize, Class);

Done:
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
