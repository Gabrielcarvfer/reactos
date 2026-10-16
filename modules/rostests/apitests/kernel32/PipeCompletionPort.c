/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for overlapped named pipes on an I/O completion port
 *
 * libuv (Node.js: VS Code, Electron apps) puts its pipes on a completion port
 * with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS: a ReadFile that pends must still
 * post its packet when the data comes.
 */

#include "precomp.h"

#ifndef FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif

typedef BOOL (WINAPI *FN_SetFileCompletionNotificationModes)(HANDLE, UCHAR);

static FN_SetFileCompletionNotificationModes pSetFileCompletionNotificationModes;

static
VOID
Test_Pipe(BOOL Skip)
{
    WCHAR PipeName[64];
    HANDLE Port, Server, Client;
    OVERLAPPED Overlapped;
    LPOVERLAPPED Done;
    ULONG_PTR Key;
    CHAR Buffer[16];
    DWORD Bytes;
    BOOL Ret;

    StringCchPrintfW(PipeName, _countof(PipeName), L"\\\\.\\pipe\\PipeCompletionPort_%lu_%d",
                     GetCurrentProcessId(), Skip);

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    if (!Port)
        return;

    Server = CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipe failed with %lu\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
    {
        CloseHandle(Port);
        return;
    }
    ok(CreateIoCompletionPort(Server, Port, 1, 0) == Port, "Associating the pipe failed with %lu\n", GetLastError());
    if (Skip)
    {
        ok(pSetFileCompletionNotificationModes(Server, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS),
           "SetFileCompletionNotificationModes failed with %lu\n", GetLastError());
    }

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Ret = ConnectNamedPipe(Server, &Overlapped);
    ok(!Ret && GetLastError() == ERROR_IO_PENDING, "ConnectNamedPipe returned %d, error %lu, expected pending\n",
       Ret, GetLastError());

    Client = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "Opening the pipe failed with %lu\n", GetLastError());

    Done = NULL;
    Ret = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 2000);
    ok(Ret, "No connect packet, error %lu\n", GetLastError());
    ok(Done == &Overlapped, "Connect packet has OVERLAPPED %p, expected %p\n", Done, &Overlapped);

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Ret = ReadFile(Server, Buffer, sizeof(Buffer), &Bytes, &Overlapped);
    ok(!Ret && GetLastError() == ERROR_IO_PENDING, "ReadFile returned %d, error %lu, expected pending\n",
       Ret, GetLastError());

    if (Client != INVALID_HANDLE_VALUE)
    {
        ok(WriteFile(Client, "hello", 5, &Bytes, NULL), "WriteFile failed with %lu\n", GetLastError());
        Done = NULL;
        Bytes = 0;
        Ret = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 2000);
        ok(Ret, "No read packet when the data came, error %lu\n", GetLastError());
        ok(Done == &Overlapped, "Read packet has OVERLAPPED %p, expected %p\n", Done, &Overlapped);
        ok(Bytes == 5, "Read packet has %lu bytes, expected 5\n", Bytes);
        CloseHandle(Client);
    }

    CloseHandle(Server);
    /* Let the cancelled read (if any) complete before the port goes away */
    GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 100);
    CloseHandle(Port);
}

START_TEST(PipeCompletionPort)
{
    pSetFileCompletionNotificationModes = (FN_SetFileCompletionNotificationModes)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileCompletionNotificationModes");

    Test_Pipe(FALSE);
    if (!pSetFileCompletionNotificationModes)
        skip("No SetFileCompletionNotificationModes\n");
    else
        Test_Pipe(TRUE);
}
