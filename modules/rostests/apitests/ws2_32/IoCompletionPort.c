/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for sockets on an I/O completion port
 *
 * Python's asyncio (proactor event loop) uses sockets this way: AcceptEx on a
 * listener, ConnectEx from a socket bound to port 0, then WSASend / WSARecv,
 * all through a completion port. Each completion must come back with its own
 * OVERLAPPED and a success status. Loopback only.
 */

#include "ws2_32.h"
#include <mswsock.h>

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif

typedef struct _IOCP_MODE
{
    BOOL NonBlocking;
    BOOL AcceptLate;
    BOOL UseEvents;
    DWORD PortThreads;
    int Protocol;
    DWORD SocketFlags;
} IOCP_MODE, *PIOCP_MODE;

/* Waits for one completion; returns its OVERLAPPED (or NULL) */
static
LPOVERLAPPED
WaitCompletion(
    _In_ HANDLE Port,
    _In_ DWORD Timeout,
    _Out_ PDWORD Bytes,
    _Out_ PDWORD Error)
{
    ULONG_PTR Key;
    LPOVERLAPPED Overlapped = NULL;
    BOOL Success;

    *Bytes = 0;
    Success = GetQueuedCompletionStatus(Port, Bytes, &Key, &Overlapped, Timeout);
    *Error = Success ? 0 : GetLastError();
    return Overlapped;
}

static
HANDLE
NewEvent(
    _In_ PIOCP_MODE Mode)
{
    return Mode->UseEvents ? CreateEventW(NULL, TRUE, FALSE, NULL) : NULL;
}

static
VOID
CloseEvent(
    _In_opt_ HANDLE Event)
{
    if (Event)
        CloseHandle(Event);
}

static
BOOL
StartAccept(
    _In_ LPFN_ACCEPTEX pAcceptEx,
    _In_ SOCKET Listener,
    _In_ SOCKET Accepted,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ DWORD BufferSize,
    _In_ LPOVERLAPPED Overlapped)
{
    DWORD Bytes;
    DWORD AddressLength = sizeof(struct sockaddr_in) + 16;

    ok(BufferSize >= 2 * AddressLength, "Buffer too small\n");
    if (!pAcceptEx(Listener, Accepted, Buffer, 0, AddressLength, AddressLength, &Bytes, Overlapped))
    {
        ok(WSAGetLastError() == ERROR_IO_PENDING, "AcceptEx failed: %d\n", WSAGetLastError());
        return WSAGetLastError() == ERROR_IO_PENDING;
    }
    return TRUE;
}

static
VOID
RunMode(
    _In_ PIOCP_MODE Mode)
{
    SOCKET Listener, Accepted, Client;
    struct sockaddr_in Address, Any;
    int Length = sizeof(Address), i;
    HANDLE Port;
    LPFN_ACCEPTEX pAcceptEx = NULL;
    LPFN_CONNECTEX pConnectEx = NULL;
    GUID AcceptGuid = WSAID_ACCEPTEX, ConnectGuid = WSAID_CONNECTEX;
    DWORD Bytes, Error, Flags = 0;
    OVERLAPPED OvAccept, OvConnect, OvSend, OvRecv;
    LPOVERLAPPED Done;
    CHAR AcceptBuffer[2 * (sizeof(struct sockaddr_in) + 16)];
    CHAR Data[16];
    CHAR Hello[] = "hello";
    CHAR X[] = "x";
    WSABUF Buffer;
    BOOL GotAccept = FALSE, GotConnect = FALSE, GotSend = FALSE, GotRecv = FALSE;
    u_long One = 1;

    trace("%s sockets, AcceptEx %s ConnectEx, %s, port threads 0x%lx, protocol %d, flags 0x%lx\n",
          Mode->NonBlocking ? "non-blocking" : "blocking", Mode->AcceptLate ? "after" : "before",
          Mode->UseEvents ? "events" : "no events", Mode->PortThreads, Mode->Protocol, Mode->SocketFlags);

    RtlZeroMemory(&OvAccept, sizeof(OvAccept));
    RtlZeroMemory(&OvConnect, sizeof(OvConnect));
    RtlZeroMemory(&OvSend, sizeof(OvSend));
    RtlZeroMemory(&OvRecv, sizeof(OvRecv));
    RtlZeroMemory(Data, sizeof(Data));

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, Mode->PortThreads);
    ok(Port != NULL, "CreateIoCompletionPort failed: %lu\n", GetLastError());
    if (!Port)
        return;

    Listener = WSASocketW(AF_INET, SOCK_STREAM, Mode->Protocol, NULL, 0, Mode->SocketFlags);
    Accepted = WSASocketW(AF_INET, SOCK_STREAM, Mode->Protocol, NULL, 0, Mode->SocketFlags);
    Client = WSASocketW(AF_INET, SOCK_STREAM, Mode->Protocol, NULL, 0, Mode->SocketFlags);
    ok(Listener != INVALID_SOCKET && Accepted != INVALID_SOCKET && Client != INVALID_SOCKET,
       "WSASocket failed: %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET || Accepted == INVALID_SOCKET || Client == INVALID_SOCKET)
        goto Cleanup;

    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed: %d\n", WSAGetLastError());
    ok(CreateIoCompletionPort((HANDLE)Listener, Port, 1, 0) == Port, "Associating the listener failed: %lu\n", GetLastError());

    WSAIoctl(Listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &AcceptGuid, sizeof(AcceptGuid),
             &pAcceptEx, sizeof(pAcceptEx), &Bytes, NULL, NULL);
    WSAIoctl(Listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &ConnectGuid, sizeof(ConnectGuid),
             &pConnectEx, sizeof(pConnectEx), &Bytes, NULL, NULL);
    ok(pAcceptEx != NULL, "No AcceptEx pointer\n");
    ok(pConnectEx != NULL, "No ConnectEx pointer\n");
    if (!pAcceptEx || !pConnectEx)
        goto Cleanup;

    /* The server: AcceptEx (as Python: non-blocking sockets) */
    if (Mode->NonBlocking)
    {
        ioctlsocket(Listener, FIONBIO, &One);
        ioctlsocket(Accepted, FIONBIO, &One);
    }
    if (!Mode->UseEvents)
        CreateIoCompletionPort((HANDLE)Accepted, Port, 3, 0);
    OvAccept.hEvent = NewEvent(Mode);
    if (!Mode->AcceptLate)
        StartAccept(pAcceptEx, Listener, Accepted, AcceptBuffer, sizeof(AcceptBuffer), &OvAccept);

    /* The client: bound to port 0, then ConnectEx */
    if (Mode->NonBlocking)
        ioctlsocket(Client, FIONBIO, &One);
    RtlZeroMemory(&Any, sizeof(Any));
    Any.sin_family = AF_INET;
    ok(bind(Client, (struct sockaddr *)&Any, sizeof(Any)) == 0, "Binding the client to port 0 failed: %d\n", WSAGetLastError());
    CreateIoCompletionPort((HANDLE)Client, Port, 2, 0);
    OvConnect.hEvent = NewEvent(Mode);
    if (!pConnectEx(Client, (struct sockaddr *)&Address, sizeof(Address), NULL, 0, NULL, &OvConnect))
        ok(WSAGetLastError() == ERROR_IO_PENDING, "ConnectEx failed: %d\n", WSAGetLastError());
    if (Mode->AcceptLate)
    {
        Sleep(500);
        StartAccept(pAcceptEx, Listener, Accepted, AcceptBuffer, sizeof(AcceptBuffer), &OvAccept);
    }

    /* Both completions, in any order */
    for (i = 0; i < 2; i++)
    {
        Done = WaitCompletion(Port, 5000, &Bytes, &Error);
        if (Done == &OvAccept)
        {
            GotAccept = TRUE;
            ok(Error == 0, "AcceptEx completion status %lu\n", Error);
        }
        else if (Done == &OvConnect)
        {
            GotConnect = TRUE;
            ok(Error == 0, "ConnectEx completion status %lu\n", Error);
        }
        else
        {
            ok(0, "Completion %d: OVERLAPPED %p, error %lu\n", i, Done, Error);
        }
    }
    ok(GotAccept, "AcceptEx did not complete on the port with its OVERLAPPED\n");
    ok(GotConnect, "ConnectEx did not complete on the port with its OVERLAPPED\n");

    ok(setsockopt(Client, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0,
       "SO_UPDATE_CONNECT_CONTEXT failed: %d\n", WSAGetLastError());
    ok(setsockopt(Accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&Listener, sizeof(Listener)) == 0,
       "SO_UPDATE_ACCEPT_CONTEXT failed: %d\n", WSAGetLastError());
    if (Mode->UseEvents)
        CreateIoCompletionPort((HANDLE)Accepted, Port, 3, 0);

    /* Data both ways through the port */
    OvRecv.hEvent = NewEvent(Mode);
    Buffer.buf = Data;
    Buffer.len = sizeof(Data);
    if (WSARecv(Accepted, &Buffer, 1, NULL, &Flags, &OvRecv, NULL) == SOCKET_ERROR)
        ok(WSAGetLastError() == WSA_IO_PENDING, "WSARecv failed: %d\n", WSAGetLastError());
    OvSend.hEvent = NewEvent(Mode);
    Buffer.buf = Hello;
    Buffer.len = 5;
    if (WSASend(Client, &Buffer, 1, NULL, 0, &OvSend, NULL) == SOCKET_ERROR)
        ok(WSAGetLastError() == WSA_IO_PENDING, "WSASend failed: %d\n", WSAGetLastError());

    for (i = 0; i < 2; i++)
    {
        Done = WaitCompletion(Port, 5000, &Bytes, &Error);
        if (Done == &OvRecv)
        {
            GotRecv = TRUE;
            ok(Error == 0 && Bytes == 5 && !memcmp(Data, "hello", 5),
               "WSARecv: error %lu, %lu bytes, '%.5s'\n", Error, Bytes, Data);
        }
        else if (Done == &OvSend)
        {
            GotSend = TRUE;
            ok(Error == 0 && Bytes == 5, "WSASend: error %lu, %lu bytes\n", Error, Bytes);
        }
        else
        {
            ok(0, "Completion %d: OVERLAPPED %p, error %lu\n", i, Done, Error);
        }
    }
    ok(GotRecv, "WSARecv did not complete on the port\n");
    ok(GotSend, "WSASend did not complete on the port\n");

    /* An event with its low bit set: no completion packet */
    {
        OVERLAPPED Overlapped;
        HANDLE Event = CreateEventW(NULL, TRUE, FALSE, NULL);

        RtlZeroMemory(&Overlapped, sizeof(Overlapped));
        Overlapped.hEvent = (HANDLE)((ULONG_PTR)Event | 1);
        Buffer.buf = X;
        Buffer.len = 1;
        WSASend(Client, &Buffer, 1, NULL, 0, &Overlapped, NULL);
        WaitForSingleObject(Event, 2000);
        Done = WaitCompletion(Port, 1000, &Bytes, &Error);
        ok(Done == NULL && Error == WAIT_TIMEOUT,
           "Low bit of hEvent: got a completion (OVERLAPPED %p, error %lu)\n", Done, Error);
        CloseHandle(Event);
    }

Cleanup:
    if (Client != INVALID_SOCKET)
        closesocket(Client);
    if (Accepted != INVALID_SOCKET)
        closesocket(Accepted);
    if (Listener != INVALID_SOCKET)
        closesocket(Listener);
    /* Pending operations are cancelled by the closesocket calls above:
     * collect their packets before the OVERLAPPEDs go out of scope */
    while (WaitCompletion(Port, 200, &Bytes, &Error) != NULL)
        ;
    CloseEvent(OvAccept.hEvent);
    CloseEvent(OvConnect.hEvent);
    CloseEvent(OvSend.hEvent);
    CloseEvent(OvRecv.hEvent);
    CloseHandle(Port);
}

/* accept() on a non-blocking listener with nothing waiting: WSAEWOULDBLOCK */
static
VOID
TestEmptyAccept(VOID)
{
    SOCKET Listener, Socket;
    struct sockaddr_in Address;
    u_long One = 1;
    fd_set ReadSet;
    struct timeval Timeout = { 0, 0 };
    int Result;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;
    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed: %d\n", WSAGetLastError());
    ioctlsocket(Listener, FIONBIO, &One);
    FD_ZERO(&ReadSet);
    FD_SET(Listener, &ReadSet);
    Result = select(0, &ReadSet, NULL, NULL, &Timeout);
    ok(Result == 0, "select: %d (error %d), expected not readable\n", Result, WSAGetLastError());
    Socket = accept(Listener, NULL, NULL);
    ok(Socket == INVALID_SOCKET && WSAGetLastError() == WSAEWOULDBLOCK,
       "accept: socket %p, error %d, expected WSAEWOULDBLOCK\n", (PVOID)Socket, WSAGetLastError());
    if (Socket != INVALID_SOCKET)
        closesocket(Socket);
    closesocket(Listener);
}

START_TEST(IoCompletionPort)
{
    static IOCP_MODE Modes[] =
    {
        /* NonBlocking, AcceptLate, UseEvents, PortThreads, Protocol, SocketFlags */
        { FALSE, FALSE, TRUE,  0,        IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        { TRUE,  FALSE, TRUE,  0,        IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        { TRUE,  TRUE,  TRUE,  0,        IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        /* As Python's asyncio: no events, a port for "any number" of threads */
        { TRUE,  FALSE, FALSE, INFINITE, IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        { TRUE,  FALSE, TRUE,  INFINITE, IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        { TRUE,  FALSE, FALSE, 0,        IPPROTO_TCP, WSA_FLAG_OVERLAPPED },
        /* As CPython's socket.socket(): protocol 0, no handle inheritance */
        { TRUE,  FALSE, FALSE, INFINITE, 0,           WSA_FLAG_OVERLAPPED },
        { TRUE,  FALSE, FALSE, INFINITE, 0,           WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT },
    };
    WSADATA WsaData;
    ULONG i;

    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        skip("WSAStartup failed\n");
        return;
    }

    TestEmptyAccept();
    for (i = 0; i < _countof(Modes); i++)
        RunMode(&Modes[i]);

    WSACleanup();
}
