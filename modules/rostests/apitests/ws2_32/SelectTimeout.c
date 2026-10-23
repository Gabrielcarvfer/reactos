/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     select() whose timeout expires while its socket gets data
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * AFD completed such a select twice when the timeout's DPC ran on another
 * processor while the data arrived: the second completion copied the result
 * again into msafd's poll buffer, freed by then, and corrupted the heap
 * (Chrome's updater, heap.c:1230 "*FreeSize == NextEntry->PreviousSize").
 * Several threads select with short timeouts while another one sends to
 * their sockets all the time, so that timeouts and data meet; the heap must
 * stay valid and every select must give a sane result.
 */

#include "ws2_32.h"

#define WORKERS 4
#define SECONDS 15

typedef struct
{
    SOCKET Reader, Writer;
    LONG Selects, Ready, Timeouts, Errors, HeapCorrupt;
} PAIR;

static PAIR Pairs[WORKERS];
static volatile LONG Stop;

static BOOL MakePair(PAIR *Pair)
{
    struct sockaddr_in addr = { 0 };
    int len = sizeof(addr);
    SOCKET listener;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return FALSE;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) ||
        getsockname(listener, (struct sockaddr *)&addr, &len) ||
        listen(listener, 1))
    {
        closesocket(listener);
        return FALSE;
    }
    Pair->Writer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(Pair->Writer, (struct sockaddr *)&addr, sizeof(addr)))
    {
        closesocket(listener);
        return FALSE;
    }
    Pair->Reader = accept(listener, NULL, NULL);
    closesocket(listener);
    return Pair->Reader != INVALID_SOCKET;
}

static DWORD WINAPI Worker(PVOID Param)
{
    PAIR *Pair = Param;
    struct timeval tv = { 0, 1000 };
    char buf[256];
    fd_set rd;
    u_long avail;
    int ret;

    while (!Stop)
    {
        FD_ZERO(&rd);
        FD_SET(Pair->Reader, &rd);
        ret = select(0, &rd, NULL, NULL, &tv);
        Pair->Selects++;
        if (ret == 0)
            Pair->Timeouts++;
        else if (ret == 1 && FD_ISSET(Pair->Reader, &rd))
        {
            Pair->Ready++;
            if (!ioctlsocket(Pair->Reader, FIONREAD, &avail) && avail)
                recv(Pair->Reader, buf, min(avail, sizeof(buf)), 0);
        }
        else
            Pair->Errors++;

        if (!(Pair->Selects % 64) && !HeapValidate(GetProcessHeap(), 0, NULL))
        {
            Pair->HeapCorrupt++;
            break;
        }
    }
    return 0;
}

START_TEST(SelectTimeout)
{
    HANDLE Threads[WORKERS];
    SYSTEM_INFO si;
    DWORD Start;
    WSADATA wsa;
    ULONG i;
    LONG Selects = 0, Ready = 0, Timeouts = 0;

    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors < 2)
        trace("1 processor: the race needs two\n");

    ok(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "WSAStartup failed\n");
    for (i = 0; i < WORKERS; i++)
    {
        if (!MakePair(&Pairs[i]))
        {
            skip("no loopback connection: %d\n", WSAGetLastError());
            WSACleanup();
            return;
        }
    }
    for (i = 0; i < WORKERS; i++)
        Threads[i] = CreateThread(NULL, 0, Worker, &Pairs[i], 0, NULL);

    /* Data comes all the time, at about the select timeout's pace */
    Start = GetTickCount();
    while (GetTickCount() - Start < SECONDS * 1000)
    {
        for (i = 0; i < WORKERS; i++)
            send(Pairs[i].Writer, "x", 1, 0);
        Sleep(1);
    }
    Stop = TRUE;
    WaitForMultipleObjects(WORKERS, Threads, TRUE, 30000);

    for (i = 0; i < WORKERS; i++)
    {
        CloseHandle(Threads[i]);
        ok(Pairs[i].HeapCorrupt == 0, "worker %lu: the heap was corrupted after %ld selects\n", i, Pairs[i].Selects);
        ok(Pairs[i].Errors == 0, "worker %lu: %ld selects failed or gave a wrong set\n", i, Pairs[i].Errors);
        Selects += Pairs[i].Selects;
        Ready += Pairs[i].Ready;
        Timeouts += Pairs[i].Timeouts;
        closesocket(Pairs[i].Reader);
        closesocket(Pairs[i].Writer);
    }
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "the heap is corrupted\n");
    ok(Ready > 0 && Timeouts > 0, "%ld ready, %ld timed out: no race\n", Ready, Timeouts);
    trace("%ld selects: %ld ready, %ld timed out\n", Selects, Ready, Timeouts);
    WSACleanup();
}
