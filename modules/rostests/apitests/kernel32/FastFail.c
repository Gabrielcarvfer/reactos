/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for __fastfail (int 0x29)
 *
 * Chromium (and the MSVC runtime) end a process with __fastfail when they
 * find a fatal error. The process must end at once with
 * STATUS_STACK_BUFFER_OVERRUN (0xC0000409), not crash in some other way.
 */

#include "precomp.h"
#include <intrin.h>

#ifndef STATUS_STACK_BUFFER_OVERRUN
#define STATUS_STACK_BUFFER_OVERRUN ((NTSTATUS)0xC0000409)
#endif

START_TEST(FastFail)
{
    WCHAR FileName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD Wait, ExitCode = 0;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        /* FAST_FAIL_FATAL_APP_EXIT */
        __fastfail(7);
        /* Not reached */
        ok(0, "__fastfail returned\n");
        return;
    }

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" FastFail child", FileName);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    if (!CreateProcessW(FileName, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL,
                        &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        return;
    }

    Wait = WaitForSingleObject(ProcessInfo.hProcess, 30000);
    ok(Wait == WAIT_OBJECT_0, "The child did not end (wait %lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 1);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(ExitCode == (DWORD)STATUS_STACK_BUFFER_OVERRUN,
       "Child exit code 0x%08lx, expected 0xC0000409\n", ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}
