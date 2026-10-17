/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for schtasks /create with /tr (no XML file)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * TeamViewer's installer registers a task with
 * "schtasks /create /tn NAME /tr COMMAND /sc ONSTART /ru SYSTEM /f";
 * schtasks only took /xml and failed with "Unsupported argument /TR".
 */

#include <apitest.h>
#define COBJMACROS
#include <objbase.h>
#include <initguid.h>
#include <taskschd.h>

#define TASK_NAME L"ReactOS schtasks apitest"

static DWORD RunSchtasks(const WCHAR *Args)
{
    WCHAR CommandLine[1024], System[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD ExitCode = 0xdeadbeef;

    GetSystemDirectoryW(System, MAX_PATH);
    _snwprintf(CommandLine, 1023, L"\"%s\\schtasks.exe\" %s", System, Args);
    CommandLine[1023] = 0;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        ok(0, "CreateProcess(%ls) failed: %lu\n", CommandLine, GetLastError());
        return ExitCode;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &ExitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ExitCode;
}

/* Whether the task is registered (through the Task Scheduler API) */
static BOOL TaskExists(void)
{
    ITaskService *Service = NULL;
    ITaskFolder *Root = NULL;
    IRegisteredTask *Task = NULL;
    VARIANT Empty;
    BSTR Name;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void **)&Service);
    ok(hr == S_OK, "CoCreateInstance(TaskScheduler): 0x%08lx\n", hr);
    if (FAILED(hr))
        return FALSE;
    V_VT(&Empty) = VT_EMPTY;
    hr = ITaskService_Connect(Service, Empty, Empty, Empty, Empty);
    ok(hr == S_OK, "Connect: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
        hr = ITaskService_GetFolder(Service, NULL, &Root);
    if (SUCCEEDED(hr))
    {
        Name = SysAllocString(TASK_NAME);
        hr = ITaskFolder_GetTask(Root, Name, &Task);
        SysFreeString(Name);
        if (Task)
            IRegisteredTask_Release(Task);
        ITaskFolder_Release(Root);
    }
    ITaskService_Release(Service);
    return hr == S_OK;
}

START_TEST(CreateTr)
{
    DWORD Ret;

    CoInitialize(NULL);

    /* (a leftover from an earlier run) */
    RunSchtasks(L"/delete /tn \"" TASK_NAME L"\" /f");

    /* TeamViewer's command */
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr \"\\\"C:\\Program Files\\Test\\test.exe\\\" --arg 1\" "
                      L"/sc ONSTART /ru SYSTEM /f");
    ok(Ret == 0, "create /tr /sc onstart: %lu\n", Ret);
    ok(TaskExists(), "the task was not registered\n");

    /* Without /f an existing task is an error */
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc ONLOGON");
    ok(Ret != 0, "create over an existing task: %lu\n", Ret);

    /* With /f it is replaced; other schedules */
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc DAILY /mo 2 /st 10:30 /rl HIGHEST /f");
    ok(Ret == 0, "create /sc daily: %lu\n", Ret);
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc WEEKLY /d MON,FRI /st 08:00 /f");
    ok(Ret == 0, "create /sc weekly: %lu\n", Ret);
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc MINUTE /mo 15 /f");
    ok(Ret == 0, "create /sc minute: %lu\n", Ret);
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc ONCE /st 23:59 /sd 12/31/2030 /f");
    ok(Ret == 0, "create /sc once: %lu\n", Ret);

    /* Bad schedules */
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc SOMETIMES /f");
    ok(Ret != 0, "create /sc sometimes: %lu\n", Ret);
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /f");
    ok(Ret != 0, "create without /sc: %lu\n", Ret);
    Ret = RunSchtasks(L"/create /tn \"" TASK_NAME L"\" /tr notepad.exe /sc DAILY /st 25:00 /f");
    ok(Ret != 0, "create with a bad /st: %lu\n", Ret);

    Ret = RunSchtasks(L"/delete /tn \"" TASK_NAME L"\" /f");
    ok(Ret == 0, "delete: %lu\n", Ret);
    ok(!TaskExists(), "the task is still registered\n");

    CoUninitialize();
}
