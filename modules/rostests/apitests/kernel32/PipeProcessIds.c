/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for GetNamedPipeServerProcessId and GetNamedPipeClientProcessId
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Chromium's IPC (mojo) checks the process at the other end of its pipes:
 * GetNamedPipeServerProcessId failed with ERROR_NOT_FOUND, so Edge's
 * WebView2 never connected to its host (CreateCoreWebView2Controller
 * failed with 0x80080005; TeamViewer stayed on "Starting TeamViewer...").
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GET_PIPE_PID)(HANDLE, PULONG);

START_TEST(PipeProcessIds)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PFN_GET_PIPE_PID pServerPid = (PFN_GET_PIPE_PID)GetProcAddress(Kernel32, "GetNamedPipeServerProcessId");
    PFN_GET_PIPE_PID pClientPid = (PFN_GET_PIPE_PID)GetProcAddress(Kernel32, "GetNamedPipeClientProcessId");
    PFN_GET_PIPE_PID pServerSession = (PFN_GET_PIPE_PID)GetProcAddress(Kernel32, "GetNamedPipeServerSessionId");
    PFN_GET_PIPE_PID pClientSession = (PFN_GET_PIPE_PID)GetProcAddress(Kernel32, "GetNamedPipeClientSessionId");
    WCHAR Name[64];
    HANDLE Server, Client;
    ULONG Id, Session = 0;
    BOOL Ret;

    if (!pServerPid || !pClientPid)
    {
        skip("No pipe process ids (before Vista)\n");
        return;
    }
    ProcessIdToSessionId(GetCurrentProcessId(), &Session);

    _snwprintf(Name, _countof(Name) - 1, L"\\\\.\\pipe\\rostest_pipeids_%lu", GetCurrentProcessId());
    Name[_countof(Name) - 1] = 0;
    Server = CreateNamedPipeW(Name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipe failed: %lu\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE)
        return;

    /* Before a client connects, the server end knows its own process */
    Id = 0xdeadbeef;
    Ret = pServerPid(Server, &Id);
    ok(Ret && Id == GetCurrentProcessId(), "Server pid (listening): %d %lu, error %lu\n", Ret, Id, GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pClientPid(Server, &Id);
    ok(!Ret && GetLastError() == ERROR_NOT_FOUND, "Client pid (listening): %d, error %lu\n", Ret, GetLastError());

    Client = CreateFileW(Name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (Client != INVALID_HANDLE_VALUE)
    {
        /* Both ends answer both questions */
        Id = 0xdeadbeef;
        Ret = pServerPid(Client, &Id);
        ok(Ret && Id == GetCurrentProcessId(), "Server pid from the client: %d %lu, error %lu\n", Ret, Id, GetLastError());
        Id = 0xdeadbeef;
        Ret = pServerPid(Server, &Id);
        ok(Ret && Id == GetCurrentProcessId(), "Server pid from the server: %d %lu, error %lu\n", Ret, Id, GetLastError());
        Id = 0xdeadbeef;
        Ret = pClientPid(Server, &Id);
        ok(Ret && Id == GetCurrentProcessId(), "Client pid from the server: %d %lu, error %lu\n", Ret, Id, GetLastError());
        Id = 0xdeadbeef;
        Ret = pClientPid(Client, &Id);
        ok(Ret && Id == GetCurrentProcessId(), "Client pid from the client: %d %lu, error %lu\n", Ret, Id, GetLastError());
        if (pServerSession)
        {
            Id = 0xdeadbeef;
            Ret = pServerSession(Client, &Id);
            ok(Ret && Id == Session, "Server session: %d %lu (session %lu), error %lu\n", Ret, Id, Session, GetLastError());
        }
        if (pClientSession)
        {
            Id = 0xdeadbeef;
            Ret = pClientSession(Server, &Id);
            ok(Ret && Id == Session, "Client session: %d %lu (session %lu), error %lu\n", Ret, Id, Session, GetLastError());
        }
        CloseHandle(Client);
    }
    CloseHandle(Server);
}
