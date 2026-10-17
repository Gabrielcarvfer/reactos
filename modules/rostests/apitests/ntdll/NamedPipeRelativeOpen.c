/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test opening named pipes relative to the named pipe root directory
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * Cygwin/MSYS2 (Git for Windows' bash) open a handle to \Device\NamedPipe\
 * and then create and open their pipes by a name relative to it. npfs took
 * the root directory's CCB for the DCB in a relative open of an existing pipe
 * and hit an assertion (Dcb->NodeType == NPFS_NTC_ROOT_DCB) in debug builds.
 */

static
NTSTATUS
OpenRoot(PHANDLE Root)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\NamedPipe\\");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtOpenFile(Root, GENERIC_READ | SYNCHRONIZE, &Attributes, &Iosb,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
}

START_TEST(NamedPipeRelativeOpen)
{
    WCHAR Buffer[64];
    UNICODE_STRING Name, Missing = RTL_CONSTANT_STRING(L"rostest-no-such-pipe");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Timeout;
    HANDLE Root, Server = NULL, Client = NULL, Missed = NULL;
    NTSTATUS Status;
    CHAR Data[8] = "pipe!", Read[8] = { 0 };

    Status = OpenRoot(&Root);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    _snwprintf(Buffer, _countof(Buffer), L"rostest-relative-pipe-%lu", GetCurrentProcessId());
    RtlInitUnicodeString(&Name, Buffer);

    /* Server end, relative to the root (like Cygwin's fhandler_pipe) */
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, Root, NULL);
    Timeout.QuadPart = -500000;
    Status = NtCreateNamedPipeFile(&Server, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &Attributes, &Iosb,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT,
                                   FILE_PIPE_BYTE_STREAM_TYPE, FILE_PIPE_BYTE_STREAM_MODE, FILE_PIPE_QUEUE_OPERATION,
                                   1, 4096, 4096, &Timeout);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Root);
        return;
    }

    /* Client end of the existing pipe, relative to the root too */
    Status = NtOpenFile(&Client, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &Attributes, &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtWriteFile(Client, NULL, NULL, NULL, &Iosb, Data, sizeof(Data), NULL, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtReadFile(Server, NULL, NULL, NULL, &Iosb, Read, sizeof(Read), NULL, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        ok(Iosb.Information == sizeof(Data), "read %Iu bytes\n", Iosb.Information);
        ok(!memcmp(Read, Data, sizeof(Data)), "read '%.8s'\n", Read);
        NtClose(Client);
    }

    /* A pipe that does not exist */
    InitializeObjectAttributes(&Attributes, &Missing, OBJ_CASE_INSENSITIVE, Root, NULL);
    Status = NtOpenFile(&Missed, GENERIC_READ | SYNCHRONIZE, &Attributes, &Iosb,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok_ntstatus(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
        NtClose(Missed);

    NtClose(Server);
    NtClose(Root);
}
