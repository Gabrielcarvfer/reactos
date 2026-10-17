/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that both ends of a TCP connection agree on their names
 *
 * Python 3.12+ builds socket.socketpair() on Windows from a loopback
 * connection and checks that each end's getsockname() is the other end's
 * getpeername(); asyncio's ProactorEventLoop needs it. The accepted socket's
 * peer port came back byte-swapped (tcpip took lwIP's host-order port).
 */

#include "ws2_32.h"

static void TestNames(BOOL NonBlocking)
{
    SOCKET Listener, Client, Server;
    SOCKADDR_IN Addr, ListenName, ServerName, ServerPeer, ClientName, ClientPeer, AcceptAddr;
    int Len, Ret;
    u_long Mode;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ZeroMemory(&Addr, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (PSOCKADDR)&Addr, sizeof(Addr)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, SOMAXCONN) == 0, "listen failed: %d\n", WSAGetLastError());
    Len = sizeof(ListenName);
    ok(getsockname(Listener, (PSOCKADDR)&ListenName, &Len) == 0, "getsockname failed: %d\n", WSAGetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    Mode = NonBlocking;
    ioctlsocket(Client, FIONBIO, &Mode);
    Ret = connect(Client, (PSOCKADDR)&ListenName, sizeof(ListenName));
    if (NonBlocking)
        ok(Ret == 0 || WSAGetLastError() == WSAEWOULDBLOCK, "connect failed: %d\n", WSAGetLastError());
    else
        ok(Ret == 0, "connect failed: %d\n", WSAGetLastError());
    Mode = 0;
    ioctlsocket(Client, FIONBIO, &Mode);

    Len = sizeof(AcceptAddr);
    ZeroMemory(&AcceptAddr, sizeof(AcceptAddr));
    Server = accept(Listener, (PSOCKADDR)&AcceptAddr, &Len);
    ok(Server != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());

    ZeroMemory(&ServerName, sizeof(ServerName));
    ZeroMemory(&ServerPeer, sizeof(ServerPeer));
    ZeroMemory(&ClientName, sizeof(ClientName));
    ZeroMemory(&ClientPeer, sizeof(ClientPeer));
    Len = sizeof(ServerName);
    ok(getsockname(Server, (PSOCKADDR)&ServerName, &Len) == 0, "getsockname failed: %d\n", WSAGetLastError());
    Len = sizeof(ServerPeer);
    ok(getpeername(Server, (PSOCKADDR)&ServerPeer, &Len) == 0, "getpeername failed: %d\n", WSAGetLastError());
    Len = sizeof(ClientName);
    ok(getsockname(Client, (PSOCKADDR)&ClientName, &Len) == 0, "getsockname failed: %d\n", WSAGetLastError());
    Len = sizeof(ClientPeer);
    ok(getpeername(Client, (PSOCKADDR)&ClientPeer, &Len) == 0, "getpeername failed: %d\n", WSAGetLastError());

    ok(ServerName.sin_addr.s_addr == ClientPeer.sin_addr.s_addr, "server address %lx, client peer %lx\n",
       ServerName.sin_addr.s_addr, ClientPeer.sin_addr.s_addr);
    ok(ServerName.sin_port == ClientPeer.sin_port, "server port %u, client peer port %u\n",
       ntohs(ServerName.sin_port), ntohs(ClientPeer.sin_port));
    ok(ClientName.sin_addr.s_addr == htonl(INADDR_LOOPBACK), "client address %lx\n", ClientName.sin_addr.s_addr);
    ok(ClientName.sin_addr.s_addr == ServerPeer.sin_addr.s_addr, "client address %lx, server peer %lx\n",
       ClientName.sin_addr.s_addr, ServerPeer.sin_addr.s_addr);
    ok(ClientName.sin_port == ServerPeer.sin_port, "client port %u, server peer port %u\n",
       ntohs(ClientName.sin_port), ntohs(ServerPeer.sin_port));
    ok(ClientName.sin_port == AcceptAddr.sin_port, "client port %u, accept() returned port %u\n",
       ntohs(ClientName.sin_port), ntohs(AcceptAddr.sin_port));

    closesocket(Server);
    closesocket(Client);
    closesocket(Listener);
}

START_TEST(SocketNames)
{
    WSADATA WsaData;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");
    TestNames(FALSE);
    TestNames(TRUE);
    WSACleanup();
}
