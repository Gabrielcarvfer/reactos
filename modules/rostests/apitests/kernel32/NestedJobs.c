/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for nested job objects (Windows 8+)
 *
 * Chromium's base::LaunchProcess puts each child in a job of its own and kills
 * the child when AssignProcessToJobObject fails, also when the launcher itself
 * runs in a job. That needs nested jobs:
 *   - unnested use (Windows 7 behaviour) still works
 *   - a process in job A can be assigned to job B: B nests in A, the process
 *     is in both, IsProcessInJob is TRUE for both
 *   - a grandchild created by it inherits B (and so is in A too)
 *   - TerminateJobObject(A) and kill-on-close of A kill the processes of B
 *   - invalid hierarchies are refused with ERROR_ACCESS_DENIED
 *   - an active process limit of A applies to processes joining B
 */

#include "precomp.h"

static WCHAR SelfPath[MAX_PATH];

/* Starts "kernel32_apitest NestedJobs <Mode>" suspended. Breaks away from a
   job we may be in (a test harness), if that job allows it. */
static BOOL
StartChild(
    _In_ PCWSTR Mode,
    _Out_ PROCESS_INFORMATION *ProcessInfo)
{
    WCHAR CommandLine[MAX_PATH + 64];
    STARTUPINFOW StartupInfo;

    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" NestedJobs %ls", SelfPath, Mode);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    /* No standard handles: the children's output is of no use */
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;

    if (CreateProcessW(SelfPath, CommandLine, NULL, NULL, FALSE,
                       CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL,
                       &StartupInfo, ProcessInfo))
        return TRUE;
    if (CreateProcessW(SelfPath, CommandLine, NULL, NULL, FALSE,
                       CREATE_SUSPENDED, NULL, NULL, &StartupInfo, ProcessInfo))
        return TRUE;
    ok(0, "CreateProcess (%ls) failed with %lu\n", Mode, GetLastError());
    ZeroMemory(ProcessInfo, sizeof(*ProcessInfo));
    return FALSE;
}

static VOID
FinishChild(
    _Inout_ PROCESS_INFORMATION *ProcessInfo)
{
    if (!ProcessInfo->hProcess)
        return;
    TerminateProcess(ProcessInfo->hProcess, 0);
    WaitForSingleObject(ProcessInfo->hProcess, 5000);
    CloseHandle(ProcessInfo->hThread);
    CloseHandle(ProcessInfo->hProcess);
    ZeroMemory(ProcessInfo, sizeof(*ProcessInfo));
}

static BOOL
InJob(
    _In_ HANDLE hProcess,
    _In_opt_ HANDLE hJob)
{
    BOOL Result = FALSE;

    if (!IsProcessInJob(hProcess, hJob, &Result))
    {
        trace("IsProcessInJob failed with %lu\n", GetLastError());
        return FALSE;
    }
    return Result;
}

static BOOL
Assign(
    _In_ HANDLE hJob,
    _In_ HANDLE hProcess,
    _Out_ PDWORD Error)
{
    BOOL Success;

    SetLastError(0);
    Success = AssignProcessToJobObject(hJob, hProcess);
    *Error = Success ? 0 : GetLastError();
    return Success;
}

/* Exit code after waiting up to 5 s, STILL_ACTIVE if it still runs */
static DWORD
ExitCodeOf(
    _In_ HANDLE hProcess)
{
    DWORD Code = STILL_ACTIVE;

    WaitForSingleObject(hProcess, 5000);
    GetExitCodeProcess(hProcess, &Code);
    return Code;
}

/* Process IDs of a job (including its nested jobs) */
static DWORD
JobPids(
    _In_ HANDLE hJob,
    _Out_writes_(Max) PULONG_PTR Pids,
    _In_ DWORD Max)
{
    struct
    {
        JOBOBJECT_BASIC_PROCESS_ID_LIST List;
        ULONG_PTR More[15];
    } Buffer;
    DWORD i;

    ZeroMemory(&Buffer, sizeof(Buffer));
    if (!QueryInformationJobObject(hJob, JobObjectBasicProcessIdList, &Buffer, sizeof(Buffer), NULL))
    {
        trace("QueryInformationJobObject(JobObjectBasicProcessIdList) failed with %lu\n", GetLastError());
        return 0;
    }
    for (i = 0; i < Buffer.List.NumberOfProcessIdsInList && i < Max; i++)
        Pids[i] = Buffer.List.ProcessIdList[i];
    return Buffer.List.NumberOfProcessIdsInList;
}

static DWORD
ActiveProcesses(
    _In_ HANDLE hJob)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Info;

    if (!QueryInformationJobObject(hJob, JobObjectBasicAccountingInformation, &Info, sizeof(Info), NULL))
        return (DWORD)-1;
    return Info.ActiveProcesses;
}

/* Waits up to 5 s for the job to hold Count processes */
static DWORD
WaitPids(
    _In_ HANDLE hJob,
    _In_ DWORD Count,
    _Out_writes_(Max) PULONG_PTR Pids,
    _In_ DWORD Max)
{
    DWORD n = 0, i;

    for (i = 0; i < 50; i++)
    {
        n = JobPids(hJob, Pids, Max);
        if (n >= Count)
            break;
        Sleep(100);
    }
    return n;
}

static VOID
Test_Unnested(VOID)
{
    PROCESS_INFORMATION pi;
    HANDLE hJob, hOther;
    DWORD Error;

    hJob = CreateJobObjectW(NULL, NULL);
    hOther = CreateJobObjectW(NULL, NULL);
    ok(hJob != NULL && hOther != NULL, "CreateJobObject failed with %lu\n", GetLastError());
    if (hJob && hOther && StartChild(L"sleep", &pi))
    {
        ok(Assign(hJob, pi.hProcess, &Error), "AssignProcessToJobObject failed with %lu\n", Error);
        ok(InJob(pi.hProcess, hJob), "IsProcessInJob(child, job) is FALSE\n");
        ok(!InJob(pi.hProcess, hOther), "IsProcessInJob(child, unrelated job) is TRUE\n");
        ok(InJob(pi.hProcess, NULL), "IsProcessInJob(child, NULL) is FALSE\n");
        ok(Assign(hJob, pi.hProcess, &Error), "Assigning to the same job again failed with %lu\n", Error);
        ok(ActiveProcesses(hJob) == 1, "ActiveProcesses %lu, expected 1\n", ActiveProcesses(hJob));
        ResumeThread(pi.hThread);
        ok(TerminateJobObject(hJob, 42), "TerminateJobObject failed with %lu\n", GetLastError());
        Error = ExitCodeOf(pi.hProcess);
        ok(Error == 42, "Child exit code %lu, expected 42 (from TerminateJobObject)\n", Error);
        ok(WaitForSingleObject(hJob, 0) == WAIT_OBJECT_0, "Terminated job is not signaled\n");
        FinishChild(&pi);
    }
    if (hOther) CloseHandle(hOther);
    if (hJob) CloseHandle(hJob);
}

static VOID
Test_Nested(VOID)
{
    PROCESS_INFORMATION pi;
    HANDLE hA, hB, hGrandchild = NULL;
    ULONG_PTR Pids[16];
    DWORD Error, n, i;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    if (!hA || !hB || !StartChild(L"spawn", &pi))
    {
        skip("Cannot set up the nested job test\n");
        if (hA) CloseHandle(hA);
        if (hB) CloseHandle(hB);
        return;
    }
    ok(Assign(hA, pi.hProcess, &Error), "Assign child to A failed with %lu\n", Error);
    ok(Assign(hB, pi.hProcess, &Error), "Assign child (in A) to B failed with %lu\n", Error);
    ok(InJob(pi.hProcess, hA), "IsProcessInJob(child, A) is FALSE\n");
    ok(InJob(pi.hProcess, hB), "IsProcessInJob(child, B) is FALSE\n");
    ok(Assign(hB, pi.hProcess, &Error), "Assigning to B again failed with %lu\n", Error);
    Error = 0;
    ok(!Assign(hA, pi.hProcess, &Error) && Error == ERROR_ACCESS_DENIED,
       "Assigning back to the outer job A: error %lu, expected ERROR_ACCESS_DENIED\n", Error);
    ok(InJob(pi.hProcess, hB), "Child no longer in B\n");

    ResumeThread(pi.hThread);
    n = WaitPids(hB, 2, Pids, _countof(Pids));
    ok(n == 2, "B lists %lu processes, expected 2 (child and grandchild)\n", n);
    for (i = 0; i < n && i < _countof(Pids); i++)
    {
        if (Pids[i] != pi.dwProcessId && !hGrandchild)
            hGrandchild = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, (DWORD)Pids[i]);
    }
    ok(hGrandchild != NULL, "Grandchild not found or not opened\n");
    if (hGrandchild)
    {
        ok(InJob(hGrandchild, hB), "IsProcessInJob(grandchild, B) is FALSE\n");
        ok(InJob(hGrandchild, hA), "IsProcessInJob(grandchild, A) is FALSE\n");
    }
    n = JobPids(hA, Pids, _countof(Pids));
    ok(n == 2, "A lists %lu processes, expected the 2 of B\n", n);
    ok(ActiveProcesses(hA) == 2, "A ActiveProcesses %lu, expected 2\n", ActiveProcesses(hA));
    ok(ActiveProcesses(hB) == 2, "B ActiveProcesses %lu, expected 2\n", ActiveProcesses(hB));

    ok(TerminateJobObject(hA, 77), "TerminateJobObject(A) failed with %lu\n", GetLastError());
    Error = ExitCodeOf(pi.hProcess);
    ok(Error == 77, "Child (in B) exit code %lu, expected 77 from TerminateJobObject(A)\n", Error);
    if (hGrandchild)
    {
        Error = ExitCodeOf(hGrandchild);
        ok(Error == 77, "Grandchild (in B) exit code %lu, expected 77 from TerminateJobObject(A)\n", Error);
        TerminateProcess(hGrandchild, 0);
        CloseHandle(hGrandchild);
    }
    ok(ActiveProcesses(hA) == 0, "A ActiveProcesses %lu, expected 0\n", ActiveProcesses(hA));
    ok(ActiveProcesses(hB) == 0, "B ActiveProcesses %lu, expected 0\n", ActiveProcesses(hB));
    /* Whatever happened, nothing of B may survive */
    TerminateJobObject(hB, 1);
    FinishChild(&pi);
    CloseHandle(hB);
    CloseHandle(hA);
}

static VOID
Test_TerminateInner(VOID)
{
    PROCESS_INFORMATION p1, p2;
    HANDLE hA, hB;
    DWORD Error;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&p1, sizeof(p1));
    ZeroMemory(&p2, sizeof(p2));
    if (!hA || !hB || !StartChild(L"sleep", &p1) || !StartChild(L"sleep", &p2))
    {
        skip("Cannot set up the inner job test\n");
        goto Cleanup;
    }
    ok(Assign(hA, p1.hProcess, &Error), "Assign p1 to A failed with %lu\n", Error);
    ok(Assign(hA, p2.hProcess, &Error), "Assign p2 to A failed with %lu\n", Error);
    ok(Assign(hB, p2.hProcess, &Error), "Assign p2 to B (nested in A) failed with %lu\n", Error);
    ok(ActiveProcesses(hA) == 2, "A ActiveProcesses %lu, expected 2\n", ActiveProcesses(hA));
    ResumeThread(p1.hThread);
    ResumeThread(p2.hThread);
    ok(TerminateJobObject(hB, 5), "TerminateJobObject(B) failed with %lu\n", GetLastError());
    Error = ExitCodeOf(p2.hProcess);
    ok(Error == 5, "p2 (in B) exit code %lu, expected 5\n", Error);
    ok(WaitForSingleObject(p1.hProcess, 500) == WAIT_TIMEOUT, "p1 (only in A) ended too\n");
    ok(ActiveProcesses(hA) == 1, "A ActiveProcesses %lu, expected 1\n", ActiveProcesses(hA));

Cleanup:
    FinishChild(&p1);
    FinishChild(&p2);
    if (hB) CloseHandle(hB);
    if (hA) CloseHandle(hA);
}

static VOID
Test_KillOnClose(VOID)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    PROCESS_INFORMATION pi;
    HANDLE hA, hB;
    DWORD Error;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    if (!hA || !hB)
    {
        skip("CreateJobObject failed with %lu\n", GetLastError());
        if (hA) CloseHandle(hA);
        if (hB) CloseHandle(hB);
        return;
    }
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ok(SetInformationJobObject(hA, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)),
       "Setting KILL_ON_JOB_CLOSE on A failed with %lu\n", GetLastError());
    if (StartChild(L"sleep", &pi))
    {
        ok(Assign(hA, pi.hProcess, &Error), "Assign child to A failed with %lu\n", Error);
        ok(Assign(hB, pi.hProcess, &Error), "Assign child to B failed with %lu\n", Error);
        ResumeThread(pi.hThread);
        ok(WaitForSingleObject(pi.hProcess, 300) == WAIT_TIMEOUT, "Child does not run\n");
        CloseHandle(hA);
        hA = NULL;
        Error = ExitCodeOf(pi.hProcess);
        ok(Error != STILL_ACTIVE, "Closing A did not kill the child in B\n");
        FinishChild(&pi);
    }
    if (hA) CloseHandle(hA);
    CloseHandle(hB);
}

static VOID
Test_Invalid(VOID)
{
    PROCESS_INFORMATION p, q, r;
    HANDLE hA, hB, hC, hD;
    DWORD Error;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    hC = CreateJobObjectW(NULL, NULL);
    hD = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&p, sizeof(p));
    ZeroMemory(&q, sizeof(q));
    ZeroMemory(&r, sizeof(r));
    if (!hA || !hB || !hC || !hD ||
        !StartChild(L"sleep", &p) || !StartChild(L"sleep", &q) || !StartChild(L"sleep", &r))
    {
        skip("Cannot set up the invalid hierarchy test\n");
        goto Cleanup;
    }
    ok(Assign(hA, p.hProcess, &Error) && Assign(hB, p.hProcess, &Error),
       "p in A, then B failed with %lu\n", Error);
    ok(Assign(hC, q.hProcess, &Error), "q in C failed with %lu\n", Error);
    Error = 0;
    ok(!Assign(hB, q.hProcess, &Error) && Error == ERROR_ACCESS_DENIED,
       "q (in C) to B (nested in A): error %lu, expected ERROR_ACCESS_DENIED\n", Error);
    ok(InJob(q.hProcess, hC) && !InJob(q.hProcess, hB), "q did not stay in C only\n");
    ok(WaitForSingleObject(q.hProcess, 0) == WAIT_TIMEOUT, "q was killed by the refusal\n");
    ok(Assign(hD, r.hProcess, &Error), "r in D failed with %lu\n", Error);
    Error = 0;
    ok(!Assign(hD, p.hProcess, &Error) && Error == ERROR_ACCESS_DENIED,
       "p (in B) to the non-empty D: error %lu, expected ERROR_ACCESS_DENIED\n", Error);
    Error = 0;
    ok(!Assign(hC, p.hProcess, &Error) && Error == ERROR_ACCESS_DENIED,
       "p (in B) to the non-empty C: error %lu, expected ERROR_ACCESS_DENIED\n", Error);
    ok(InJob(p.hProcess, hB) && InJob(p.hProcess, hA) && !InJob(p.hProcess, hD),
       "p did not stay in B and A only\n");

Cleanup:
    FinishChild(&p);
    FinishChild(&q);
    FinishChild(&r);
    if (hA) CloseHandle(hA);
    if (hB) CloseHandle(hB);
    if (hC) CloseHandle(hC);
    if (hD) CloseHandle(hD);
}

static VOID
Test_ActiveLimit(VOID)
{
    JOBOBJECT_BASIC_LIMIT_INFORMATION Limits;
    PROCESS_INFORMATION p1, p2, p3;
    HANDLE hA, hB;
    DWORD Error;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&p1, sizeof(p1));
    ZeroMemory(&p2, sizeof(p2));
    ZeroMemory(&p3, sizeof(p3));
    if (!hA || !hB)
    {
        skip("CreateJobObject failed with %lu\n", GetLastError());
        goto Cleanup;
    }
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    Limits.ActiveProcessLimit = 1;
    ok(SetInformationJobObject(hA, JobObjectBasicLimitInformation, &Limits, sizeof(Limits)),
       "Setting ActiveProcessLimit 1 on A failed with %lu\n", GetLastError());
    if (!StartChild(L"sleep", &p1) || !StartChild(L"sleep", &p2) || !StartChild(L"sleep", &p3))
        goto Cleanup;
    ok(Assign(hA, p1.hProcess, &Error), "p1 to A failed with %lu\n", Error);
    Error = 0;
    ok(!Assign(hA, p2.hProcess, &Error) && Error == ERROR_NOT_ENOUGH_QUOTA,
       "p2 to A (limit reached): error %lu, expected ERROR_NOT_ENOUGH_QUOTA\n", Error);
    ok(Assign(hB, p1.hProcess, &Error), "p1 to B (nested in A, must not count twice) failed with %lu\n", Error);
    ok(ActiveProcesses(hA) == 1, "A ActiveProcesses %lu, expected 1\n", ActiveProcesses(hA));
    Error = 0;
    ok(!Assign(hB, p3.hProcess, &Error) && Error == ERROR_NOT_ENOUGH_QUOTA,
       "p3 to B (limit of A): error %lu, expected ERROR_NOT_ENOUGH_QUOTA\n", Error);
    ok(ActiveProcesses(hB) == 1, "B ActiveProcesses %lu, expected 1\n", ActiveProcesses(hB));

Cleanup:
    FinishChild(&p1);
    FinishChild(&p2);
    FinishChild(&p3);
    if (hB) CloseHandle(hB);
    if (hA) CloseHandle(hA);
}

static VOID
Test_SilentBreakaway(VOID)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    PROCESS_INFORMATION pi;
    ULONG_PTR Pids[16];
    HANDLE hA, hB;
    DWORD Error, n;

    hA = CreateJobObjectW(NULL, NULL);
    hB = CreateJobObjectW(NULL, NULL);
    if (!hA || !hB)
    {
        skip("CreateJobObject failed with %lu\n", GetLastError());
        if (hA) CloseHandle(hA);
        if (hB) CloseHandle(hB);
        return;
    }
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    ok(SetInformationJobObject(hB, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)),
       "Allowing silent breakaway on B failed with %lu\n", GetLastError());
    if (StartChild(L"spawn", &pi))
    {
        ok(Assign(hA, pi.hProcess, &Error) && Assign(hB, pi.hProcess, &Error),
           "Child in A, then B failed with %lu\n", Error);
        ResumeThread(pi.hThread);
        n = WaitPids(hA, 2, Pids, _countof(Pids));
        ok(n == 2, "A has %lu processes, expected 2 (the grandchild broke away from B into A)\n", n);
        n = JobPids(hB, Pids, _countof(Pids));
        ok(n == 1, "B has %lu processes, expected only the child\n", n);
        TerminateJobObject(hA, 1);
        TerminateJobObject(hB, 1);
        ExitCodeOf(pi.hProcess);
        FinishChild(&pi);
    }
    CloseHandle(hB);
    CloseHandle(hA);
}

START_TEST(NestedJobs)
{
    BOOL InAJob = FALSE;
    char **argv;
    int argc;

    GetModuleFileNameW(NULL, SelfPath, _countof(SelfPath));
    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "sleep"))
    {
        Sleep(60000);
        return;
    }
    if (argc >= 3 && !strcmp(argv[2], "spawn"))
    {
        PROCESS_INFORMATION pi;
        STARTUPINFOW StartupInfo;
        WCHAR CommandLine[MAX_PATH + 64];

        ZeroMemory(&StartupInfo, sizeof(StartupInfo));
        StartupInfo.cb = sizeof(StartupInfo);
        StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" NestedJobs sleep", SelfPath);
        if (CreateProcessW(SelfPath, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        Sleep(60000);
        return;
    }

    IsProcessInJob(GetCurrentProcess(), NULL, &InAJob);
    trace("The test process %s in a job\n", InAJob ? "is" : "is not");

    Test_Unnested();
    Test_Nested();
    Test_TerminateInner();
    Test_KillOnClose();
    Test_Invalid();
    Test_ActiveLimit();
    Test_SilentBreakaway();
}
