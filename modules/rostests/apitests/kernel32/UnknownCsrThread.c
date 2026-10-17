/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test Win32 calls from threads that kernel32 did not create
 *
 * Threads made with RtlCreateUserThread (ntdll's thread pool, .NET, remote
 * threads) are not known to CSRSS. Windows still serves their CSR calls:
 * CreateThread from such a thread must give a thread that runs and exits
 * normally. Paint.NET's setup (.NET 9) failed Thread.Start with an
 * OutOfMemoryException: CSRSS refused BasepCreateThread with
 * STATUS_ILLEGAL_FUNCTION and kernel32 killed the new thread.
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PFN_RTL_CREATE_USER_THREAD)(HANDLE, PSECURITY_DESCRIPTOR, BOOLEAN, ULONG,
                                                     SIZE_T, SIZE_T, PVOID, PVOID, PHANDLE, PCLIENT_ID);
typedef VOID (NTAPI *PFN_RTL_EXIT_USER_THREAD)(NTSTATUS);
typedef VOID (CALLBACK *PFN_SIMPLE_CALLBACK)(PVOID, PVOID);
typedef BOOL (WINAPI *PFN_TRY_SUBMIT_THREADPOOL_CALLBACK)(PFN_SIMPLE_CALLBACK, PVOID, PVOID);

static PFN_RTL_CREATE_USER_THREAD pRtlCreateUserThread;
static PFN_RTL_EXIT_USER_THREAD pRtlExitUserThread;

#define INNER_EXIT_CODE 0x1234
#define NESTED_EXIT_CODE 0x5678

typedef struct _UNKNOWN_THREAD_RESULT
{
    BOOL Created;
    DWORD CreateError;
    DWORD WaitResult;
    DWORD ExitCode;
    BOOL NestedOk;
    BOOL ShutdownParamsOk;
    DWORD ShutdownError;
} UNKNOWN_THREAD_RESULT, *PUNKNOWN_THREAD_RESULT;

static
DWORD
WINAPI
NestedThread(LPVOID Parameter)
{
    return NESTED_EXIT_CODE;
}

static
DWORD
WINAPI
InnerThread(LPVOID Parameter)
{
    PUNKNOWN_THREAD_RESULT Result = Parameter;
    DWORD ExitCode = 0;
    HANDLE Thread;

    /* A normal thread made by the new thread must work too */
    Thread = CreateThread(NULL, 0, NestedThread, NULL, 0, NULL);
    if (Thread)
    {
        if (WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0 &&
            GetExitCodeThread(Thread, &ExitCode))
        {
            Result->NestedOk = (ExitCode == NESTED_EXIT_CODE);
        }
        CloseHandle(Thread);
    }
    return INNER_EXIT_CODE;
}

/* Runs the checks: called on a thread that CSRSS does not know */
static
VOID
CheckFromUnknownThread(PUNKNOWN_THREAD_RESULT Result)
{
    HANDLE Thread;
    DWORD Level, Flags;

    Thread = CreateThread(NULL, 0, InnerThread, Result, 0, NULL);
    Result->Created = (Thread != NULL);
    Result->CreateError = GetLastError();
    if (Thread)
    {
        Result->WaitResult = WaitForSingleObject(Thread, 10000);
        GetExitCodeThread(Thread, &Result->ExitCode);
        CloseHandle(Thread);
    }

    /* Another BASESRV call that needs a CSR thread */
    SetLastError(0xdeadbeef);
    Result->ShutdownParamsOk = GetProcessShutdownParameters(&Level, &Flags);
    Result->ShutdownError = GetLastError();
}

static
NTSTATUS
NTAPI
RtlThreadProc(PVOID Parameter)
{
    CheckFromUnknownThread(Parameter);
    pRtlExitUserThread(STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

static
VOID
CALLBACK
PoolCallback(PVOID Instance, PVOID Parameter)
{
    CheckFromUnknownThread(Parameter);
}

static
VOID
CheckResult(PUNKNOWN_THREAD_RESULT Result, PCSTR Where)
{
    ok(Result->Created, "%s: CreateThread failed with %lu\n", Where, Result->CreateError);
    ok(Result->WaitResult == WAIT_OBJECT_0, "%s: wait returned %lu\n", Where, Result->WaitResult);
    ok(Result->ExitCode == INNER_EXIT_CODE, "%s: exit code 0x%lx\n", Where, Result->ExitCode);
    ok(Result->NestedOk, "%s: the nested thread did not run\n", Where);
    ok(Result->ShutdownParamsOk, "%s: GetProcessShutdownParameters failed with %lu\n",
       Where, Result->ShutdownError);
}

START_TEST(UnknownCsrThread)
{
    static UNKNOWN_THREAD_RESULT Result;
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_TRY_SUBMIT_THREADPOOL_CALLBACK pTrySubmitThreadpoolCallback;
    HANDLE Thread = NULL;
    NTSTATUS Status;
    int i;

    pRtlCreateUserThread = (PFN_RTL_CREATE_USER_THREAD)GetProcAddress(Ntdll, "RtlCreateUserThread");
    pRtlExitUserThread = (PFN_RTL_EXIT_USER_THREAD)GetProcAddress(Ntdll, "RtlExitUserThread");
    if (!pRtlCreateUserThread || !pRtlExitUserThread)
    {
        skip("RtlCreateUserThread or RtlExitUserThread missing\n");
        return;
    }

    /* Several rounds: thread IDs get reused */
    for (i = 0; i < 3; i++)
    {
        ZeroMemory(&Result, sizeof(Result));
        Status = pRtlCreateUserThread(GetCurrentProcess(), NULL, FALSE, 0, 0, 0,
                                      RtlThreadProc, &Result, &Thread, NULL);
        ok(Status == STATUS_SUCCESS, "RtlCreateUserThread returned 0x%lx\n", Status);
        if (Status != STATUS_SUCCESS)
            return;
        ok(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0, "RtlCreateUserThread thread did not end\n");
        CloseHandle(Thread);
        CheckResult(&Result, "RtlCreateUserThread");
    }

    /* A Vista thread pool worker */
    pTrySubmitThreadpoolCallback = (PFN_TRY_SUBMIT_THREADPOOL_CALLBACK)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TrySubmitThreadpoolCallback");
    if (!pTrySubmitThreadpoolCallback)
    {
        skip("TrySubmitThreadpoolCallback missing\n");
        return;
    }
    ZeroMemory(&Result, sizeof(Result));
    if (pTrySubmitThreadpoolCallback(PoolCallback, &Result, NULL))
    {
        for (i = 0; i < 300 && !Result.Created && !Result.CreateError; i++)
            Sleep(100);
        /* Let the checks finish */
        for (i = 0; i < 300 && !Result.ShutdownError && !Result.ShutdownParamsOk; i++)
            Sleep(100);
        CheckResult(&Result, "thread pool");
    }
    else
    {
        ok(FALSE, "TrySubmitThreadpoolCallback failed with %lu\n", GetLastError());
    }
}
