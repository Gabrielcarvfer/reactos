/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the steps of a TCP connection
 *
 * The steps programs (installers that download, browsers) take to open a TCP
 * connection, each checked on its own against a loopback server: a socket
 * bound to port 0 before connect, a blocking connect with send / recv, and a
 * non-blocking connect completed with select.
 */

#include "ws2_32.h"

static
SOCKET
OpenSocket(
    _In_ PCSTR How)
{
    SOCKET Socket = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    ok(Socket != INVALID_SOCKET, "%s: WSASocket failed: %d\n", How, WSAGetLastError());
    return Socket;
}

/* Accepts the pending connection of the listener (waits up to 5 seconds) */
static
SOCKET
AcceptConnection(
    _In_ SOCKET Listener,
    _In_ PCSTR How)
{
    fd_set ReadSet;
    struct timeval Timeout = { 5, 0 };
    SOCKET Socket;
    int Result;

    FD_ZERO(&ReadSet);
    FD_SET(Listener, &ReadSet);
    Result = select(0, &ReadSet, NULL, NULL, &Timeout);
    ok(Result == 1, "%s: the listener is not readable: %d (error %d)\n", How, Result, WSAGetLastError());
    if (Result != 1)
        return INVALID_SOCKET;
    Socket = accept(Listener, NULL, NULL);
    ok(Socket != INVALID_SOCKET, "%s: accept failed: %d\n", How, WSAGetLastError());
    return Socket;
}

START_TEST(TcpConnect)
{
    static const char Request[] = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    static const char Reply[] = "HTTP/1.0 200 OK\r\n\r\n";
    WSADATA WsaData;
    SOCKET Listener, Socket, Server;
    struct sockaddr_in Address, Bound;
    int Length, Result;
    char Buffer[64];
    u_long NonBlocking = 1;
    fd_set WriteSet, ExceptSet;
    struct timeval Timeout = { 10, 0 };

    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        skip("WSAStartup failed\n");
        return;
    }

    /* The loopback server */
    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }
    RtlZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed: %d\n", WSAGetLastError());
    Length = sizeof(Address);
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed: %d\n", WSAGetLastError());

    /* Bound to port 0, then connect */
    Socket = OpenSocket("bind");
    if (Socket != INVALID_SOCKET)
    {
        RtlZeroMemory(&Bound, sizeof(Bound));
        Bound.sin_family = AF_INET;
        ok(bind(Socket, (struct sockaddr *)&Bound, sizeof(Bound)) == 0, "bind: bind to port 0 failed: %d\n", WSAGetLastError());
        Length = sizeof(Bound);
        Result = getsockname(Socket, (struct sockaddr *)&Bound, &Length);
        ok(Result == 0, "bind: getsockname failed: %d\n", WSAGetLastError());
        ok(Length == sizeof(Bound), "bind: getsockname length %d\n", Length);
        ok(Bound.sin_port != 0, "bind: no port was chosen\n");
        ok(connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind: connect failed: %d\n", WSAGetLastError());
        Server = AcceptConnection(Listener, "bind");
        if (Server != INVALID_SOCKET)
            closesocket(Server);
        closesocket(Socket);
    }

    /* Blocking connect, send, recv */
    Socket = OpenSocket("blocking");
    if (Socket != INVALID_SOCKET)
    {
        ok(connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0, "blocking: connect failed: %d\n", WSAGetLastError());
        Result = send(Socket, Request, sizeof(Request) - 1, 0);
        ok(Result == sizeof(Request) - 1, "blocking: send returned %d (error %d)\n", Result, WSAGetLastError());
        Server = AcceptConnection(Listener, "blocking");
        if (Server != INVALID_SOCKET)
        {
            Result = recv(Server, Buffer, sizeof(Buffer), 0);
            ok(Result == sizeof(Request) - 1, "blocking: the server got %d bytes (error %d)\n", Result, WSAGetLastError());
            ok(send(Server, Reply, sizeof(Reply) - 1, 0) == sizeof(Reply) - 1, "blocking: the reply failed: %d\n", WSAGetLastError());
            Result = recv(Socket, Buffer, sizeof(Buffer) - 1, 0);
            ok(Result == sizeof(Reply) - 1, "blocking: recv returned %d (error %d)\n", Result, WSAGetLastError());
            if (Result > 0)
            {
                Buffer[Result] = 0;
                ok(!strncmp(Buffer, "HTTP/1.0 200", 12), "blocking: received '%s'\n", Buffer);
            }
            closesocket(Server);
        }
        closesocket(Socket);
    }

    /* Non-blocking connect, completed with select */
    Socket = OpenSocket("non-blocking");
    if (Socket != INVALID_SOCKET)
    {
        ok(ioctlsocket(Socket, FIONBIO, &NonBlocking) == 0, "non-blocking: FIONBIO failed: %d\n", WSAGetLastError());
        Result = connect(Socket, (struct sockaddr *)&Address, sizeof(Address));
        ok(Result == 0 || WSAGetLastError() == WSAEWOULDBLOCK,
           "non-blocking: connect returned %d (error %d)\n", Result, WSAGetLastError());
        FD_ZERO(&WriteSet);
        FD_SET(Socket, &WriteSet);
        FD_ZERO(&ExceptSet);
        FD_SET(Socket, &ExceptSet);
        Result = select(0, NULL, &WriteSet, &ExceptSet, &Timeout);
        ok(Result == 1, "non-blocking: select returned %d (error %d)\n", Result, WSAGetLastError());
        ok(FD_ISSET(Socket, &WriteSet), "non-blocking: the socket is not writable\n");
        ok(!FD_ISSET(Socket, &ExceptSet), "non-blocking: the connection failed\n");
        Server = AcceptConnection(Listener, "non-blocking");
        if (Server != INVALID_SOCKET)
            closesocket(Server);
        closesocket(Socket);
    }

    closesocket(Listener);
    WSACleanup();
}
