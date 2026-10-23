/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtQuerySystemInformation
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

static PVOID
QueryAll(SYSTEM_INFORMATION_CLASS Class)
{
    ULONG Size = 0x10000, Needed;
    PVOID Buffer = NULL;
    NTSTATUS Status;

    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
        if (!Buffer)
            return NULL;
        Status = NtQuerySystemInformation(Class, Buffer, Size, &Needed);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        Size = max(Size * 2, Needed + 0x1000);
    }
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return NULL;
    }
    return Buffer;
}

/* The handle lists tell the object type of each handle (tools find who
 * holds a process with it): two events share a type, a process, a thread
 * and an event each have their own */
static
VOID
TestHandleTypes(VOID)
{
    HANDLE hEvent1, hEvent2, hProcess, hThread;
    ULONG_PTR Pid = (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    PSYSTEM_HANDLE_INFORMATION Info;
    PSYSTEM_HANDLE_INFORMATION_EX InfoEx;
    UCHAR Types[4] = { 0 };
    ULONG_PTR i;
    int Pass;

    hEvent1 = CreateEventW(NULL, FALSE, FALSE, NULL);
    hEvent2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    ok(hEvent1 && hEvent2 && hProcess && hThread, "Could not open the handles\n");

    for (Pass = 0; Pass < 2; Pass++)
    {
        RtlZeroMemory(Types, sizeof(Types));
        if (Pass == 0)
        {
            Info = QueryAll(SystemHandleInformation);
            if (!Info)
                continue;
            for (i = 0; i < Info->NumberOfHandles; i++)
            {
                PSYSTEM_HANDLE_TABLE_ENTRY_INFO e = &Info->Handles[i];
                if (e->UniqueProcessId != (USHORT)Pid) continue;
                if (e->HandleValue == (USHORT)(ULONG_PTR)hEvent1) Types[0] = e->ObjectTypeIndex;
                if (e->HandleValue == (USHORT)(ULONG_PTR)hEvent2) Types[1] = e->ObjectTypeIndex;
                if (e->HandleValue == (USHORT)(ULONG_PTR)hProcess) Types[2] = e->ObjectTypeIndex;
                if (e->HandleValue == (USHORT)(ULONG_PTR)hThread) Types[3] = e->ObjectTypeIndex;
            }
            RtlFreeHeap(RtlGetProcessHeap(), 0, Info);
        }
        else
        {
            InfoEx = QueryAll(SystemExtendedHandleInformation);
            if (!InfoEx)
                continue;
            for (i = 0; i < InfoEx->NumberOfHandles; i++)
            {
                PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX e = &InfoEx->Handles[i];
                if (e->UniqueProcessId != Pid) continue;
                if (e->HandleValue == (ULONG_PTR)hEvent1) Types[0] = (UCHAR)e->ObjectTypeIndex;
                if (e->HandleValue == (ULONG_PTR)hEvent2) Types[1] = (UCHAR)e->ObjectTypeIndex;
                if (e->HandleValue == (ULONG_PTR)hProcess) Types[2] = (UCHAR)e->ObjectTypeIndex;
                if (e->HandleValue == (ULONG_PTR)hThread) Types[3] = (UCHAR)e->ObjectTypeIndex;
            }
            RtlFreeHeap(RtlGetProcessHeap(), 0, InfoEx);
        }

        ok(Types[0] != 0, "Pass %d: no type for the event\n", Pass);
        ok(Types[0] == Types[1], "Pass %d: events have types %u and %u\n", Pass, Types[0], Types[1]);
        ok(Types[2] != 0 && Types[2] != Types[0], "Pass %d: process type %u (event %u)\n", Pass, Types[2], Types[0]);
        ok(Types[3] != 0 && Types[3] != Types[0] && Types[3] != Types[2],
           "Pass %d: thread type %u (event %u, process %u)\n", Pass, Types[3], Types[0], Types[2]);
    }

    CloseHandle(hEvent1);
    CloseHandle(hEvent2);
    CloseHandle(hProcess);
    CloseHandle(hThread);
}

START_TEST(NtQuerySystemInformation)
{
    NTSTATUS Status;

    Status = NtQuerySystemInformation(0, NULL, 0, NULL);
    ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    Status = NtQuerySystemInformation(0x80000000, NULL, 0, NULL);
    ok_hex(Status, STATUS_INVALID_INFO_CLASS);

    TestHandleTypes();
}
