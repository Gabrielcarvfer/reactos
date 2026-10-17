/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test polling sockets through \Device\Afd like mio/tokio, libuv and wepoll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "ws2_32.h"
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>

/*
 * Rust's mio (tokio: RustDesk), libuv and wepoll do not use select(): they ask
 * for the base socket handle (SIO_BASE_HANDLE), open their own \Device\Afd
 * handle, tie it to a completion port and send IOCTL_AFD_POLL on it, with
 * Windows' AFD_POLL_INFO layout. On 64-bit that structure has a ULONG
 * Exclusive, so the handles start at offset 16. ReactOS used to answer
 * SIO_BASE_HANDLE with an error and had a pointer-sized Exclusive field, so
 * such programs could not use sockets at all. Loopback only.
 */

#ifndef SIO_BASE_HANDLE
#define SIO_BASE_HANDLE _WSAIOR(IOC_WS2, 34)
#endif

#define IOCTL_AFD_POLL_ 0x00012024
#define AFD_POLL_RECEIVE_      0x0001
#define AFD_POLL_SEND_         0x0004
#define AFD_POLL_ACCEPT_       0x0080
#define AFD_POLL_CONNECT_FAIL_ 0x0100

/* What these programs declare themselves */
typedef struct
{
    HANDLE Handle;
    ULONG Events;
    NTSTATUS Status;
} POLL_HANDLE_INFO;

typedef struct
{
    LARGE_INTEGER Timeout;
    ULONG NumberOfHandles;
    ULONG Exclusive;
    POLL_HANDLE_INFO Handles[1];
} POLL_INFO;

C_ASSERT(FIELD_OFFSET(POLL_INFO, Handles) == 16);

typedef NTSTATUS (NTAPI *PFN_NtCancelIoFileEx)(HANDLE, PIO_STATUS_BLOCK, PIO_STATUS_BLOCK);

static HANDLE AfdHandle, Port;

static NTSTATUS StartPoll(SOCKET Socket, ULONG Events, POLL_INFO *Info, PIO_STATUS_BLOCK Iosb)
{
    SOCKET Base = INVALID_SOCKET;
    DWORD Returned = 0;
    int Result;

    Result = WSAIoctl(Socket, SIO_BASE_HANDLE, NULL, 0, &Base, sizeof(Base), &Returned, NULL, NULL);
    ok(Result == 0, "SIO_BASE_HANDLE failed: %d\n", WSAGetLastError());
    ok(Base == Socket, "Base socket %p, expected %p\n", (PVOID)Base, (PVOID)Socket);
    ok(Returned == sizeof(Base), "Returned %lu\n", Returned);

    ZeroMemory(Info, sizeof(*Info));
    Info->Timeout.QuadPart = MAXLONGLONG;
    Info->NumberOfHandles = 1;
    Info->Handles[0].Handle = (HANDLE)Socket;
    Info->Handles[0].Events = Events;
    Iosb->Status = STATUS_PENDING;
    return NtDeviceIoControlFile(AfdHandle, NULL, NULL, Iosb, Iosb, IOCTL_AFD_POLL_,
                                 Info, sizeof(*Info), Info, sizeof(*Info));
}

static BOOL WaitPoll(PIO_STATUS_BLOCK Iosb, DWORD Milliseconds)
{
    DWORD Bytes = 0;
    ULONG_PTR Key = 0;
    LPOVERLAPPED Overlapped = NULL;
    BOOL Result;

    Result = GetQueuedCompletionStatus(Port, &Bytes, &Key, &Overlapped, Milliseconds);
    ok(Result || Overlapped, "No completion packet: %lu\n", GetLastError());
    ok(Overlapped == (LPOVERLAPPED)Iosb, "Completion %p, expected %p\n", Overlapped, Iosb);
    return Overlapped == (LPOVERLAPPED)Iosb;
}

START_TEST(AfdPoll)
{
    PFN_NtCancelIoFileEx pNtCancelIoFileEx;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\Afd\\Mio");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb, CancelIosb;
    POLL_INFO Info;
    NTSTATUS Status;
    WSADATA WsaData;
    SOCKET Udp, Listener, Client;
    struct sockaddr_in Address;
    int Length;
    u_long One = 1;
    char Buffer[4];

    pNtCancelIoFileEx = (PFN_NtCancelIoFileEx)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCancelIoFileEx");

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&AfdHandle, SYNCHRONIZE, &ObjectAttributes, &Iosb, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        WSACleanup();
        return;
    }
    Port = CreateIoCompletionPort(AfdHandle, NULL, 0, 0);
    ok(Port != NULL, "CreateIoCompletionPort failed: %lu\n", GetLastError());

    /* UDP: sendable at once */
    Udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Udp != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Udp, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    Length = sizeof(Address);
    getsockname(Udp, (struct sockaddr *)&Address, &Length);
    ioctlsocket(Udp, FIONBIO, &One);

    StartPoll(Udp, AFD_POLL_SEND_, &Info, &Iosb);
    if (WaitPoll(&Iosb, 5000))
    {
        ok_ntstatus(Iosb.Status, STATUS_SUCCESS);
        ok_long(Info.NumberOfHandles, 1);
        ok(Info.Handles[0].Handle == (HANDLE)Udp, "Handle %p\n", Info.Handles[0].Handle);
        ok(Info.Handles[0].Events & AFD_POLL_SEND_, "Events 0x%lx\n", Info.Handles[0].Events);
    }

    /* UDP: readable once a datagram arrives */
    Status = StartPoll(Udp, AFD_POLL_RECEIVE_, &Info, &Iosb);
    ok_ntstatus(Status, STATUS_PENDING);
    ok(sendto(Udp, "x", 1, 0, (struct sockaddr *)&Address, sizeof(Address)) == 1, "sendto failed: %d\n", WSAGetLastError());
    if (WaitPoll(&Iosb, 5000))
    {
        ok_ntstatus(Iosb.Status, STATUS_SUCCESS);
        ok(Info.Handles[0].Events & AFD_POLL_RECEIVE_, "Events 0x%lx\n", Info.Handles[0].Events);
    }
    recv(Udp, Buffer, sizeof(Buffer), 0);

    /* A pending poll can be cancelled */
    if (pNtCancelIoFileEx)
    {
        Status = StartPoll(Udp, AFD_POLL_RECEIVE_, &Info, &Iosb);
        ok_ntstatus(Status, STATUS_PENDING);
        pNtCancelIoFileEx(AfdHandle, &Iosb, &CancelIosb);
        if (WaitPoll(&Iosb, 5000))
            ok_ntstatus(Iosb.Status, STATUS_CANCELLED);
    }
    else
    {
        skip("NtCancelIoFileEx is not available\n");
    }

    /* TCP: a non-blocking connect becomes sendable, the listener acceptable */
    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Address.sin_port = 0;
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 5) == 0, "listen failed: %d\n", WSAGetLastError());
    Length = sizeof(Address);
    getsockname(Listener, (struct sockaddr *)&Address, &Length);
    ioctlsocket(Listener, FIONBIO, &One);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(Client, FIONBIO, &One);
    connect(Client, (struct sockaddr *)&Address, sizeof(Address));

    StartPoll(Client, AFD_POLL_SEND_ | AFD_POLL_CONNECT_FAIL_, &Info, &Iosb);
    if (WaitPoll(&Iosb, 5000))
    {
        ok_ntstatus(Iosb.Status, STATUS_SUCCESS);
        ok((Info.Handles[0].Events & (AFD_POLL_SEND_ | AFD_POLL_CONNECT_FAIL_)) == AFD_POLL_SEND_,
           "Events 0x%lx\n", Info.Handles[0].Events);
    }
    StartPoll(Listener, AFD_POLL_ACCEPT_, &Info, &Iosb);
    if (WaitPoll(&Iosb, 5000))
    {
        ok_ntstatus(Iosb.Status, STATUS_SUCCESS);
        ok(Info.Handles[0].Events & AFD_POLL_ACCEPT_, "Events 0x%lx\n", Info.Handles[0].Events);
    }

    closesocket(Client);
    closesocket(Listener);
    closesocket(Udp);
    CloseHandle(Port);
    NtClose(AfdHandle);
    WSACleanup();
}
