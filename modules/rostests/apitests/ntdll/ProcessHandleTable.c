/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for NtQueryInformationProcess(ProcessHandleTable)
 *
 * Chromium's sandbox (GetCurrentProcessHandles) lists the handles of its
 * process with information class 58 (ProcessHandleTable, Windows 8+). The
 * list must hold every handle of the process, and a too small buffer must
 * give STATUS_INFO_LENGTH_MISMATCH with the needed length.
 */

#include "precomp.h"

#define ProcessHandleTableClass 58

START_TEST(ProcessHandleTable)
{
    ULONG List[4096];
    ULONG Small[1];
    ULONG Length, Count, i;
    BOOLEAN Found = FALSE;
    HANDLE Event;
    NTSTATUS Status;

    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
    ok(Status == STATUS_SUCCESS, "NtCreateEvent failed: 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    Count = 0;
    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessHandleCount, &Count, sizeof(Count), NULL);
    ok(Status == STATUS_SUCCESS, "ProcessHandleCount query failed: 0x%lx\n", Status);

    Length = 0;
    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessHandleTableClass, List, sizeof(List), &Length);
    ok(Status == STATUS_SUCCESS, "ProcessHandleTable query failed: 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(Length % sizeof(ULONG) == 0, "Length %lu is not a multiple of %u\n", Length, (unsigned)sizeof(ULONG));
        ok(Length / sizeof(ULONG) == Count,
           "The table has %lu handles, ProcessHandleCount says %lu\n", Length / (ULONG)sizeof(ULONG), Count);
        for (i = 0; i < Length / sizeof(ULONG) && i < _countof(List); i++)
        {
            if (List[i] == HandleToUlong(Event))
                Found = TRUE;
        }
        ok(Found, "The new event handle %p is not in the table\n", Event);
    }

    /* Too small buffer: length mismatch, and the needed length */
    Length = 0;
    Status = NtQueryInformationProcess(NtCurrentProcess(), ProcessHandleTableClass, Small, sizeof(Small), &Length);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "Small buffer: status 0x%lx, expected STATUS_INFO_LENGTH_MISMATCH\n", Status);
    ok(Length >= 2 * sizeof(ULONG), "Small buffer: needed length %lu, expected at least %u\n", Length, (unsigned)(2 * sizeof(ULONG)));

    NtClose(Event);
}
