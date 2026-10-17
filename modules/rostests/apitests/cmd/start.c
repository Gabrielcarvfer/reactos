/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that cmd's START passes a program path with spaces quoted
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * START built the child's command line from the unquoted full path. A program
 * in "C:\Program Files\..." then got argv[0] = "C:\Program" and the rest of
 * its path as its first argument (Krita 6 tried to open "Files\Krita").
 * The child here is a copy of this test in a folder with a space, run as
 * "start_child": that test only runs if argv[1] is right, and it writes a
 * marker file next to itself.
 */

START_TEST(start_child)
{
    WCHAR Path[MAX_PATH], *Slash;
    HANDLE File;

    /* (Only as the child of the "start" test) */
    if (!GetEnvironmentVariableW(L"CMD_APITEST_START_CHILD", Path, ARRAYSIZE(Path)))
        return;
    GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path));
    Slash = wcsrchr(Path, L'\\');
    if (!Slash)
        return;
    wcscpy(Slash + 1, L"started.txt");
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
}

START_TEST(start)
{
    WCHAR Dir[MAX_PATH], Self[MAX_PATH], Child[MAX_PATH], Marker[MAX_PATH];
    WCHAR CmdLine[3 * MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD Wait;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    wcscat(Dir, L"start test");
    CreateDirectoryW(Dir, NULL);
    _snwprintf(Child, ARRAYSIZE(Child), L"%s\\cmd_apitest.exe", Dir);
    _snwprintf(Marker, ARRAYSIZE(Marker), L"%s\\started.txt", Dir);
    DeleteFileW(Marker);

    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    if (!CopyFileW(Self, Child, FALSE))
    {
        skip("CopyFile failed: %lu\n", GetLastError());
        return;
    }

    SetEnvironmentVariableW(L"CMD_APITEST_START_CHILD", L"1");
    _snwprintf(CmdLine, ARRAYSIZE(CmdLine), L"cmd /c start \"\" /wait \"%s\" start_child", Child);
    ok(CreateProcessW(NULL, CmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi),
       "CreateProcess failed: %lu\n", GetLastError());
    if (pi.hProcess)
    {
        Wait = WaitForSingleObject(pi.hProcess, 30000);
        ok(Wait == WAIT_OBJECT_0, "Wait = %lu\n", Wait);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    SetEnvironmentVariableW(L"CMD_APITEST_START_CHILD", NULL);

    ok(GetFileAttributesW(Marker) != INVALID_FILE_ATTRIBUTES,
       "The child did not get \"start_child\" as its first argument\n");

    DeleteFileW(Marker);
    DeleteFileW(Child);
    RemoveDirectoryW(Dir);
}
