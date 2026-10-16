/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for FILE_SKIP_COMPLETION_PORT_ON_SUCCESS on sockets
 *
 * libuv (Node.js, VS Code) sets up its sockets this way: non-blocking, not
 * inheritable, on a completion port, with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS.
 * An I/O that succeeds at once must then not also post a completion packet,
 * while one that pends must post its packet. Loopback only.
 */

#include "ws2_32.h"

#ifndef FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#define FILE_SKIP_SET_EVENT_ON_HANDLE 0x2
#endif

typedef BOOL (WINAPI *PFN_SetFileCompletionNotificationModes)(HANDLE, UCHAR);

START_TEST(SkipCompletionPort)
{
    PFN_SetFileCompletionNotificationModes pSetFileCompletionNotificationModes;
    WSADATA WsaData;
    SOCKET Listener = INVALID_SOCKET, Client = INVALID_SOCKET, Accepted = INVALID_SOCKET;
    struct sockaddr_in Address;
    int Length = sizeof(Address), Result;
    u_long One = 1;
    HANDLE Port;
    WSABUF Buffer;
    DWORD Bytes, Flags;
    OVERLAPPED Overlapped;
    LPOVERLAPPED Done;
    ULONG_PTR Key;
    CHAR Data[16];

    pSetFileCompletionNotificationModes = (PFN_SetFileCompletionNotificationModes)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileCompletionNotificationModes");
    if (!pSetFileCompletionNotificationModes)
    {
        skip("SetFileCompletionNotificationModes is not available\n");
        return;
    }
    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        skip("WSAStartup failed\n");
        return;
    }

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(Port != NULL, "CreateIoCompletionPort failed: %lu\n", GetLastError());
    if (!Port)
        goto Cleanup;

    /* As uv__tcp_set_socket */
    Listener = socket(AF_INET, SOCK_STREAM, 0);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        goto Cleanup;
    ok(ioctlsocket(Listener, FIONBIO, &One) == 0, "FIONBIO failed: %d\n", WSAGetLastError());
    ok(SetHandleInformation((HANDLE)Listener, HANDLE_FLAG_INHERIT, 0), "SetHandleInformation failed: %lu\n", GetLastError());
    ok(CreateIoCompletionPort((HANDLE)Listener, Port, (ULONG_PTR)Listener, 0) == Port,
       "Associating the listener failed: %lu\n", GetLastError());
    ok(pSetFileCompletionNotificationModes((HANDLE)Listener,
                                           FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE),
       "SetFileCompletionNotificationModes on the listener failed: %lu\n", GetLastError());

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind 127.0.0.1:0 failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 511) == 0, "listen(511) failed: %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed: %d\n", WSAGetLastError());

    /* A client, and the accepted socket with the same modes */
    Client = socket(AF_INET, SOCK_STREAM, 0);
    ok(Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Client == INVALID_SOCKET)
        goto Cleanup;
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed: %d\n", WSAGetLastError());
    Sleep(200);
    Accepted = accept(Listener, NULL, NULL);
    ok(Accepted != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());
    if (Accepted == INVALID_SOCKET)
        goto Cleanup;
    ok(CreateIoCompletionPort((HANDLE)Accepted, Port, 7, 0) == Port, "Associating the socket failed: %lu\n", GetLastError());
    ok(pSetFileCompletionNotificationModes((HANDLE)Accepted, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS),
       "SetFileCompletionNotificationModes on the accepted socket failed: %lu\n", GetLastError());

    /* Data is already there: WSARecv succeeds at once, no packet may follow */
    ok(send(Client, "hello", 5, 0) == 5, "send failed: %d\n", WSAGetLastError());
    Sleep(200);
    RtlZeroMemory(&Overlapped, sizeof(Overlapped));
    Buffer.buf = Data;
    Buffer.len = sizeof(Data);
    Bytes = 0;
    Flags = 0;
    Result = WSARecv(Accepted, &Buffer, 1, &Bytes, &Flags, &Overlapped, NULL);
    ok(Result == 0 && Bytes == 5, "WSARecv did not complete at once: result %d, error %d, %lu bytes\n",
       Result, WSAGetLastError(), Bytes);
    if (Result != 0 && WSAGetLastError() == WSA_IO_PENDING)
    {
        /* Collect its packet so the next case starts clean */
        GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 3000);
    }
    else
    {
        Done = NULL;
        Result = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 500);
        ok(!Result && Done == NULL && GetLastError() == WAIT_TIMEOUT,
           "Got a completion packet for an I/O that succeeded at once: result %d, OVERLAPPED %p, error %lu\n",
           Result, Done, GetLastError());
    }

    /* Nothing there: WSARecv pends, then its packet comes */
    RtlZeroMemory(&Overlapped, sizeof(Overlapped));
    Buffer.buf = Data;
    Buffer.len = sizeof(Data);
    Flags = 0;
    Result = WSARecv(Accepted, &Buffer, 1, &Bytes, &Flags, &Overlapped, NULL);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING,
       "WSARecv did not pend: result %d, error %d\n", Result, WSAGetLastError());
    ok(send(Client, "again", 5, 0) == 5, "send failed: %d\n", WSAGetLastError());
    Done = NULL;
    Bytes = 0;
    Result = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 3000);
    ok(Result && Done == &Overlapped && Bytes == 5,
       "No completion packet for the pending WSARecv: result %d, OVERLAPPED %p, %lu bytes, error %lu\n",
       Result, Done, Bytes, Result ? 0 : GetLastError());

Cleanup:
    if (Accepted != INVALID_SOCKET)
        closesocket(Accepted);
    if (Client != INVALID_SOCKET)
        closesocket(Client);
    if (Listener != INVALID_SOCKET)
        closesocket(Listener);
    if (Port)
    {
        /* Collect the packets of cancelled operations */
        do
        {
            Done = NULL;
            GetQueuedCompletionStatus(Port, &Bytes, &Key, &Done, 200);
        } while (Done != NULL);
        CloseHandle(Port);
    }
    WSACleanup();
}
