/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Test for many console windows created and closed at once
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Programs that each get a new console, started and ending in parallel (an
 * installer running commands, RunOnceEx): every one must start and end.
 * ReactOS's console server lost the requests to create or destroy the
 * window of a console, and the program froze while starting or ending:
 * - the console input thread dispatched all the queued thread messages
 *   while closing a window, dropping the other consoles' requests;
 * - a new console could pick the input thread that was quitting (its last
 *   window just closed), and its request died with that thread.
 */

#include "precomp.h"

#define THREADS     4
#define PROCESSES   60      /* per thread */
#define WAIT_MS     30000

static LONG Started, Ended, Stuck, Failed;

static DWORD WINAPI
StartProcesses(LPVOID Param)
{
    WCHAR CmdLine[MAX_PATH + 32];
    WCHAR SysDir[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ULONG i;

    GetSystemDirectoryW(SysDir, ARRAYSIZE(SysDir));
    for (i = 0; i < PROCESSES; i++)
    {
        StringCchPrintfW(CmdLine, ARRAYSIZE(CmdLine), L"\"%s\\cmd.exe\" /c exit", SysDir);
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = 7; /* SW_SHOWMINNOACTIVE */
        if (!CreateProcessW(NULL, CmdLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                            NULL, NULL, &si, &pi))
        {
            InterlockedIncrement(&Failed);
            continue;
        }
        InterlockedIncrement(&Started);
        if (WaitForSingleObject(pi.hProcess, WAIT_MS) == WAIT_OBJECT_0)
        {
            InterlockedIncrement(&Ended);
        }
        else
        {
            InterlockedIncrement(&Stuck);
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return 0;
}

START_TEST(ConsoleWindows)
{
    HANDLE Threads[THREADS];
    ULONG i;

    for (i = 0; i < THREADS; i++)
    {
        Threads[i] = CreateThread(NULL, 0, StartProcesses, NULL, 0, NULL);
        ok(Threads[i] != NULL, "CreateThread failed: %lu\n", GetLastError());
    }
    for (i = 0; i < THREADS; i++)
    {
        if (!Threads[i])
            continue;
        WaitForSingleObject(Threads[i], INFINITE);
        CloseHandle(Threads[i]);
    }

    ok_long(Failed, 0);
    ok_long(Started, THREADS * PROCESSES);
    ok_long(Stuck, 0);
    ok_long(Ended, Started);
}
