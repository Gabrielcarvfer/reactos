/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     A pending recvfrom that is cancelled must not leak pool
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * When a thread ends, its pending I/O is cancelled. afd.sys's cancel routine
 * unlocked the buffers of recv/send requests, but not of recvfrom/sendto
 * (datagram) ones: their buffer array (tag 'AfWb') and MDLs were never freed
 * (found by KLSAN after Electron programs exited).
 */

#include "ws2_32.h"
#include <ndk/exfuncs.h>

/* The pool bytes in use with this tag (-1: unknown) */
static LONGLONG
GetPoolTagUsage(const char *Tag)
{
    PSYSTEM_POOLTAG_INFORMATION Info;
    ULONG Size = 256 * 1024, Length, i;
    LONGLONG Used = -1;
    NTSTATUS Status;

    Info = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Info)
        return -1;
    Status = NtQuerySystemInformation(SystemPoolTagInformation, Info, Size, &Length);
    if (NT_SUCCESS(Status))
    {
        Used = 0;
        for (i = 0; i < Info->Count; i++)
        {
            if (!memcmp(Info->TagInfo[i].Tag, Tag, 4))
            {
                Used = (LONGLONG)Info->TagInfo[i].PagedUsed + (LONGLONG)Info->TagInfo[i].NonPagedUsed;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Info);
    return Used;
}

typedef struct _RECV_CONTEXT
{
    SOCKET Socket;
    CHAR Buffer[64];
    WSABUF WsaBuf;
    SOCKADDR_IN From;
    INT FromLength;
    WSAOVERLAPPED Overlapped;
    INT Result;
    INT Error;
} RECV_CONTEXT, *PRECV_CONTEXT;

/* Starts a recvfrom that waits, and ends: the thread's I/O is cancelled */
static DWORD WINAPI
RecvFromThread(LPVOID Param)
{
    PRECV_CONTEXT Context = Param;
    DWORD Received = 0, Flags = 0;

    Context->WsaBuf.buf = Context->Buffer;
    Context->WsaBuf.len = sizeof(Context->Buffer);
    Context->FromLength = sizeof(Context->From);
    Context->Result = WSARecvFrom(Context->Socket, &Context->WsaBuf, 1, &Received, &Flags,
                                  (SOCKADDR *)&Context->From, &Context->FromLength,
                                  &Context->Overlapped, NULL);
    Context->Error = WSAGetLastError();
    return 0;
}

START_TEST(RecvFromCancel)
{
    WSADATA WsaData;
    RECV_CONTEXT Context;
    SOCKADDR_IN Address;
    HANDLE Thread;
    LONGLONG Before, After;
    int i, Pending = 0;
    const int Rounds = 200;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    /* (a first round, for anything that is set up only once) */
    for (i = -1; i < Rounds; i++)
    {
        if (i == 0)
            Before = GetPoolTagUsage("AfWb");

        ZeroMemory(&Context, sizeof(Context));
        Context.Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ok(Context.Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
        if (Context.Socket == INVALID_SOCKET)
            break;
        ZeroMemory(&Address, sizeof(Address));
        Address.sin_family = AF_INET;
        Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ok(bind(Context.Socket, (SOCKADDR *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
        Context.Overlapped.hEvent = WSACreateEvent();

        Thread = CreateThread(NULL, 0, RecvFromThread, &Context, 0, NULL);
        ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Thread)
        {
            WaitForSingleObject(Thread, INFINITE);
            CloseHandle(Thread);
        }
        if (Context.Result == SOCKET_ERROR && Context.Error == WSA_IO_PENDING)
            Pending++;
        /* (cancelled with the thread) */
        WaitForSingleObject(Context.Overlapped.hEvent, 5000);
        closesocket(Context.Socket);
        WSACloseEvent(Context.Overlapped.hEvent);
    }
    After = GetPoolTagUsage("AfWb");

    ok(Pending == Rounds + 1, "%d of the recvfrom calls were pending\n", Pending);
    if (Before < 0 || After < 0)
        skip("No pool tag information\n");
    else
        ok(After - Before < 1024, "Pool tag 'AfWb' grew by %I64d bytes over %d cancelled recvfrom calls\n",
           After - Before, Rounds);

    WSACleanup();
}
