/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for console control handlers (GenerateConsoleCtrlEvent)
 *
 * Ctrl+C and Ctrl+Break: the console server starts the program's handler in
 * a new thread of the program (written for 32-bit console programs on 64-bit
 * ReactOS). The checks run in a child with a console of its own, so the
 * events reach no other process; its results come back through a file.
 */

#include "precomp.h"

#ifndef SW_SHOWMINNOACTIVE
#define SW_SHOWMINNOACTIVE 7
#endif

static HANDLE hHandled;
static volatile LONG HandledType = -1;

static BOOL WINAPI
CtrlHandler(
    _In_ DWORD Type)
{
    HandledType = (LONG)Type;
    SetEvent(hHandled);
    /* Handled: do not exit */
    return TRUE;
}

static VOID
Child(VOID)
{
    DWORD Wait;

    hHandled = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(hHandled != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (!hHandled)
        return;
    /* In case Ctrl+C is ignored by inheritance */
    ok(SetConsoleCtrlHandler(NULL, FALSE), "SetConsoleCtrlHandler(NULL, FALSE) failed with %lu\n", GetLastError());
    ok(SetConsoleCtrlHandler(CtrlHandler, TRUE), "SetConsoleCtrlHandler failed with %lu\n", GetLastError());

    HandledType = -1;
    ok(GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0), "GenerateConsoleCtrlEvent(CTRL_C_EVENT) failed with %lu\n", GetLastError());
    Wait = WaitForSingleObject(hHandled, 5000);
    ok(Wait == WAIT_OBJECT_0, "The Ctrl+C handler did not run (wait %lu)\n", Wait);
    ok(HandledType == CTRL_C_EVENT, "Handler got type %ld, expected CTRL_C_EVENT\n", HandledType);

    HandledType = -1;
    ok(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0), "GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT) failed with %lu\n", GetLastError());
    Wait = WaitForSingleObject(hHandled, 5000);
    ok(Wait == WAIT_OBJECT_0, "The Ctrl+Break handler did not run (wait %lu)\n", Wait);
    ok(HandledType == CTRL_BREAK_EVENT, "Handler got type %ld, expected CTRL_BREAK_EVENT\n", HandledType);

    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    CloseHandle(hHandled);
}

/* Runs "kernel32_apitest <Test> child" in a new console, its output in a
   temporary file; each failure it reports becomes a failure here */
static VOID
RunChildInNewConsole(
    _In_ PCWSTR Test)
{
    SECURITY_ATTRIBUTES Inheritable = { sizeof(Inheritable), NULL, TRUE };
    WCHAR FileName[MAX_PATH], CommandLine[MAX_PATH + 64], TempDir[MAX_PATH], TempFile[MAX_PATH];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    DWORD Wait, ExitCode = 0, Size, Read = 0, Failures = 0;
    HANDLE hOutput;
    PCHAR Buffer, Line, Next;

    GetTempPathW(_countof(TempDir), TempDir);
    if (!GetTempFileNameW(TempDir, L"ccc", 0, TempFile))
    {
        skip("GetTempFileName failed with %lu\n", GetLastError());
        return;
    }
    hOutput = CreateFileW(TempFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &Inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hOutput == INVALID_HANDLE_VALUE)
    {
        skip("Cannot create %ls (error %lu)\n", TempFile, GetLastError());
        DeleteFileW(TempFile);
        return;
    }

    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" %ls child", FileName, Test);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    StartupInfo.wShowWindow = SW_SHOWMINNOACTIVE;
    StartupInfo.hStdOutput = hOutput;
    StartupInfo.hStdError = hOutput;
    if (!CreateProcessW(FileName, CommandLine, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL,
                        &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
        CloseHandle(hOutput);
        DeleteFileW(TempFile);
        return;
    }
    Wait = WaitForSingleObject(ProcessInfo.hProcess, 60000);
    ok(Wait == WAIT_OBJECT_0, "The child did not end (wait %lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateProcess(ProcessInfo.hProcess, 255);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);

    Size = GetFileSize(hOutput, NULL);
    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size + 1);
    if (Buffer && Size != INVALID_FILE_SIZE)
    {
        SetFilePointer(hOutput, 0, NULL, FILE_BEGIN);
        ReadFile(hOutput, Buffer, Size, &Read, NULL);
        for (Line = Buffer; Line && *Line; Line = Next)
        {
            Next = strchr(Line, '\n');
            if (Next)
                *Next++ = 0;
            if (*Line && Line[strlen(Line) - 1] == '\r')
                Line[strlen(Line) - 1] = 0;
            if (strstr(Line, "Test failed"))
            {
                ok(0, "child: %s\n", Line);
                Failures++;
            }
            else if (strstr(Line, "Tests skipped"))
            {
                skip("child: %s\n", Line);
            }
        }
    }
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
    ok(ExitCode == Failures, "Child exit code %lu, but %lu failures reported\n", ExitCode, Failures);
    CloseHandle(hOutput);
    DeleteFileW(TempFile);
}

START_TEST(ConsoleCtrlEvent)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        Child();
        return;
    }
    RunChildInNewConsole(L"ConsoleCtrlEvent");
}
