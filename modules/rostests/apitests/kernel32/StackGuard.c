/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for the guard pages below thread stacks
 *
 * Windows x64 keeps 8 KB of guard pages below the committed part of a stack,
 * and code relies on it: it probes its stack every 8 KB (the dav1d AV1
 * decoder used by web browsers does). With a single guard page, such a probe
 * skipped the guard page and crashed.
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#ifdef _WIN64
static LONG ProbeResult;

static DWORD WINAPI
ProbeThread(PVOID Param)
{
    volatile char *Top = (volatile char *)&Param;
    ULONG Offset;
    char Sum = 0;

    UNREFERENCED_PARAMETER(Param);

    _SEH2_TRY
    {
        /* Like dav1d's PROBE_STACK on Win64: one read every 8 KB */
        for (Offset = 8192; Offset <= 128 * 1024; Offset += 8192)
            Sum += Top[-(LONG)Offset];
        ProbeResult = 1;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ProbeResult = -(LONG)_SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Sum;
}
#endif

START_TEST(StackGuard)
{
#ifdef _WIN64
    HANDLE Thread;

    /* A new thread starts with a small committed stack */
    Thread = CreateThread(NULL, 1024 * 1024, ProbeThread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
    ok(ProbeResult == 1, "Probing the stack every 8 KB failed (0x%lx)\n", -ProbeResult);
#else
    skip("The 8 KB stack guard is x64 only\n");
#endif
}
