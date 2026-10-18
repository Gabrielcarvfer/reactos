/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for send/sendto
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include "ws2_32.h"

static
PVOID
AllocateReadOnly(
    _In_ SIZE_T SizeRequested)
{
    NTSTATUS Status;
    SIZE_T Size = PAGE_ROUND_UP(SizeRequested);
    PVOID VirtualMemory = NULL;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &VirtualMemory, 0, &Size, MEM_COMMIT, PAGE_READONLY);
    if (!NT_SUCCESS(Status))
        return NULL;

    return VirtualMemory;
}

static
VOID
FreeReadOnly(
    _In_ PVOID VirtualMemory)
{
    NTSTATUS Status;
    SIZE_T Size = 0;

    Status = NtFreeVirtualMemory(NtCurrentProcess(), &VirtualMemory, &Size, MEM_RELEASE);
    ok(Status == STATUS_SUCCESS, "Status = %lx\n", Status);
}

static
VOID
test_send(void)
{
    SOCKET sock;
    int ret;
    int error;
    PVOID buffer;
    ULONG bufferSize;
    struct sockaddr_in addr;

    bufferSize = 32;
    buffer = AllocateReadOnly(bufferSize);
    ok(buffer != NULL, "AllocateReadOnly failed\n");
    if (!buffer)
    {
        skip("No memory\n");
        return;
    }

    ret = send(0, NULL, 0, 0);
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "send returned %d\n", ret);
    ok(error == WSAENOTSOCK, "error = %d\n", error);

    ret = send(0, buffer, bufferSize, 0);
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "send returned %d\n", ret);
    ok(error == WSAENOTSOCK, "error = %d\n", error);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(sock != INVALID_SOCKET, "socket failed\n");
    if (sock == INVALID_SOCKET)
    {
        skip("No socket\n");
        FreeReadOnly(buffer);
        return;
    }

    ret = send(sock, NULL, 0, 0);
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "send returned %d\n", ret);
    ok(error == WSAENOTCONN, "error = %d\n", error);

    ret = send(sock, buffer, bufferSize, 0);
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "send returned %d\n", ret);
    ok(error == WSAENOTCONN, "error = %d\n", error);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(53);
    ret = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
    error = WSAGetLastError();
    ok(ret == 0, "connect returned %d\n", ret);
    ok(error == 0, "error = %d\n", error);

    ret = send(sock, NULL, 0, 0);
    error = WSAGetLastError();
    ok(ret == 0, "send returned %d\n", ret);
    ok(error == 0, "error = %d\n", error);

    ret = send(sock, buffer, bufferSize, 0);
    error = WSAGetLastError();
    ok(ret == bufferSize, "send returned %d\n", ret);
    ok(error == 0, "error = %d\n", error);

    closesocket(sock);

    FreeReadOnly(buffer);
}

static
VOID
test_sendto(void)
{
    SOCKET sock;
    int ret;
    int error;
    PVOID buffer;
    ULONG bufferSize;
    struct sockaddr_in addr;

    bufferSize = 32;
    buffer = AllocateReadOnly(bufferSize);
    ok(buffer != NULL, "AllocateReadOnly failed\n");
    if (!buffer)
    {
        skip("No memory\n");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(53);

    ret = sendto(0, NULL, 0, 0, (const struct sockaddr *)&addr, sizeof(addr));
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "sendto returned %d\n", ret);
    ok(error == WSAENOTSOCK, "error = %d\n", error);

    ret = sendto(0, buffer, bufferSize, 0, (const struct sockaddr *)&addr, sizeof(addr));
    error = WSAGetLastError();
    ok(ret == SOCKET_ERROR, "sendto returned %d\n", ret);
    ok(error == WSAENOTSOCK, "error = %d\n", error);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(sock != INVALID_SOCKET, "socket failed\n");
    if (sock == INVALID_SOCKET)
    {
        skip("No socket\n");
        FreeReadOnly(buffer);
        return;
    }

    ret = sendto(sock, NULL, 0, 0, (const struct sockaddr *)&addr, sizeof(addr));
    error = WSAGetLastError();
    ok(ret == 0, "sendto returned %d\n", ret);
    ok(error == 0, "error = %d\n", error);

    ret = sendto(sock, buffer, bufferSize, 0, (const struct sockaddr *)&addr, sizeof(addr));
    error = WSAGetLastError();
    ok(ret == bufferSize, "sendto returned %d\n", ret);
    ok(error == 0, "error = %d\n", error);

    closesocket(sock);

    FreeReadOnly(buffer);
}

typedef struct _LARGE_SEND_READER
{
    SOCKET Socket;
    ULONG Received;
    BOOL DataOk;
} LARGE_SEND_READER, *PLARGE_SEND_READER;

static
DWORD
WINAPI
LargeSendReader(
    _In_ PVOID Context)
{
    PLARGE_SEND_READER Reader = Context;
    static UCHAR Buffer[64 * 1024];
    int ret, i;

    Reader->DataOk = TRUE;
    while ((ret = recv(Reader->Socket, (char *)Buffer, sizeof(Buffer), 0)) > 0)
    {
        for (i = 0; i < ret; i++)
        {
            if (Buffer[i] != (UCHAR)((Reader->Received + i) * 7))
                Reader->DataOk = FALSE;
        }
        Reader->Received += ret;
    }
    return 0;
}

/* A blocking send() on a connected stream socket sends all of the buffer,
 * however much bigger than the socket's send buffer it is (ReactOS returned
 * the size of its 8 KB send buffer) */
static
VOID
test_send_large(void)
{
    const ULONG Size = 4 * 1024 * 1024;
    SOCKET Listener, Client, Server;
    struct sockaddr_in Address;
    int AddressLength = sizeof(Address);
    LARGE_SEND_READER Reader = { 0 };
    HANDLE Thread;
    PUCHAR Data;
    ULONG i;
    int ret;

    Data = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Data)
    {
        skip("No memory\n");
        return;
    }
    for (i = 0; i < Size; i++)
        Data[i] = (UCHAR)(i * 7);

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = inet_addr("127.0.0.1");
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &AddressLength) == 0, "getsockname failed: %d\n", WSAGetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ret = connect(Client, (struct sockaddr *)&Address, sizeof(Address));
    ok(ret == 0, "connect failed: %d\n", WSAGetLastError());
    Server = accept(Listener, NULL, NULL);
    ok(Server != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());
    if (ret != 0 || Server == INVALID_SOCKET)
    {
        skip("No connection\n");
        goto cleanup;
    }

    Reader.Socket = Server;
    Thread = CreateThread(NULL, 0, LargeSendReader, &Reader, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto cleanup;

    /* One call sends it all */
    ret = send(Client, (const char *)Data, Size, 0);
    ok(ret == (int)Size, "send returned %d (error %d), expected %lu\n", ret, WSAGetLastError(), Size);

    /* Then the rest of a stream sent in pieces follows in order */
    for (i = 0; i < 4; i++)
    {
        ret = send(Client, (const char *)Data + Size - Size / 4 * (4 - i), Size / 4, 0);
        ok(ret == (int)(Size / 4), "send %lu returned %d (error %d)\n", i, ret, WSAGetLastError());
    }

    ok(shutdown(Client, SD_SEND) == 0, "shutdown failed: %d\n", WSAGetLastError());
    ok(WaitForSingleObject(Thread, 60000) == WAIT_OBJECT_0, "reader did not finish\n");
    CloseHandle(Thread);

    ok(Reader.Received == 2 * Size, "received %lu bytes, expected %lu\n", Reader.Received, 2 * Size);
    ok(Reader.DataOk, "received data differs from the data sent\n");

cleanup:
    if (Server != INVALID_SOCKET) closesocket(Server);
    closesocket(Client);
    closesocket(Listener);
    HeapFree(GetProcessHeap(), 0, Data);
}

/* A receiver that does not read stops the sender: non-blocking sends end
 * with WSAEWOULDBLOCK once the receive window and the buffers are full
 * (ReactOS opened the window as soon as data arrived and queued all of it),
 * and everything sent arrives in order once the receiver reads */
static
VOID
test_send_flow_control(void)
{
    const ULONG Chunk = 64 * 1024, Limit = 64 * 1024 * 1024;
    SOCKET Listener, Client, Server;
    struct sockaddr_in Address;
    int AddressLength = sizeof(Address);
    u_long NonBlocking = 1;
    PUCHAR Data, Buffer;
    ULONG Sent = 0, Received = 0, i;
    BOOL DataOk = TRUE;
    int ret, error = 0;

    Data = HeapAlloc(GetProcessHeap(), 0, Chunk);
    Buffer = HeapAlloc(GetProcessHeap(), 0, Chunk);
    if (!Data || !Buffer)
    {
        skip("No memory\n");
        goto free;
    }

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = inet_addr("127.0.0.1");
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &AddressLength) == 0, "getsockname failed: %d\n", WSAGetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ret = connect(Client, (struct sockaddr *)&Address, sizeof(Address));
    ok(ret == 0, "connect failed: %d\n", WSAGetLastError());
    Server = accept(Listener, NULL, NULL);
    ok(Server != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());
    if (ret != 0 || Server == INVALID_SOCKET)
    {
        skip("No connection\n");
        goto cleanup;
    }

    ok(ioctlsocket(Client, FIONBIO, &NonBlocking) == 0, "ioctlsocket failed: %d\n", WSAGetLastError());

    /* Send until the stack refuses more (give the data time to travel) */
    while (Sent < Limit)
    {
        for (i = 0; i < Chunk; i++)
            Data[i] = (UCHAR)((Sent + i) * 7);
        ret = send(Client, (const char *)Data, Chunk, 0);
        if (ret == SOCKET_ERROR)
        {
            error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK)
                break;
            Sleep(200);
            ret = send(Client, (const char *)Data, Chunk, 0);
            if (ret == SOCKET_ERROR)
            {
                error = WSAGetLastError();
                break;
            }
            error = 0;
        }
        Sent += ret;
        /* (a partial send: the rest of this chunk is not resent) */
        if ((ULONG)ret != Chunk)
        {
            Sleep(50);
        }
    }
    ok(error == WSAEWOULDBLOCK, "send stopped with error %d after %lu bytes\n", error, Sent);
    ok(Sent < Limit, "%lu bytes sent to a receiver that does not read\n", Sent);
    trace("%lu bytes sent before the receiver read\n", Sent);

    /* Now read it all back */
    while (Received < Sent)
    {
        ret = recv(Server, (char *)Buffer, Chunk, 0);
        if (ret <= 0)
            break;
        for (i = 0; i < (ULONG)ret; i++)
        {
            if (Buffer[i] != (UCHAR)((Received + i) * 7))
                DataOk = FALSE;
        }
        Received += ret;
    }
    ok(Received == Sent, "received %lu bytes, sent %lu (last recv returned %d, error %d)\n",
       Received, Sent, ret, WSAGetLastError());
    ok(DataOk, "received data differs from the data sent\n");

cleanup:
    if (Server != INVALID_SOCKET) closesocket(Server);
    closesocket(Client);
    closesocket(Listener);
free:
    if (Data) HeapFree(GetProcessHeap(), 0, Data);
    if (Buffer) HeapFree(GetProcessHeap(), 0, Buffer);
}

START_TEST(send)
{
    int ret;
    WSADATA wsad;

    ret = WSAStartup(MAKEWORD(2, 2), &wsad);
    ok(ret == 0, "WSAStartup failed with %d\n", ret);
    test_send();
    test_sendto();
    test_send_large();
    test_send_flow_control();
    WSACleanup();
}
