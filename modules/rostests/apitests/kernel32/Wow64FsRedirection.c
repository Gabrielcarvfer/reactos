/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for Wow64DisableWow64FsRedirection / Wow64RevertWow64FsRedirection
 *
 * A 32-bit process on 64-bit Windows sees SysWOW64 when it opens files in
 * system32, unless it disables the redirection (installers do this to put
 * 64-bit files in place). Only meaningful in a WoW64 process.
 */

#include "precomp.h"

#define TEST_FILE_NAME L"kernel32_apitest_fsredir.tmp"

static BOOL
MakeFile(
    _In_ PCWSTR Path)
{
    HANDLE hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(hFile);
    return TRUE;
}

static BOOL
Exists(
    _In_ PCWSTR Path)
{
    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

START_TEST(Wow64FsRedirection)
{
    WCHAR WinDir[MAX_PATH], System32Path[MAX_PATH], SysWow64Path[MAX_PATH], SystemDir[MAX_PATH];
    PVOID Cookie = NULL, Cookie2 = NULL;
    BOOL IsWow64 = FALSE, Ret;

    if (!IsWow64Process(GetCurrentProcess(), &IsWow64) || !IsWow64)
    {
        skip("Not a WoW64 process\n");
        return;
    }

    GetWindowsDirectoryW(WinDir, _countof(WinDir));
    StringCbPrintfW(System32Path, sizeof(System32Path), L"%ls\\system32\\%ls", WinDir, TEST_FILE_NAME);
    StringCbPrintfW(SysWow64Path, sizeof(SysWow64Path), L"%ls\\SysWOW64\\%ls", WinDir, TEST_FILE_NAME);

    /* GetSystemDirectory gives SysWOW64 to a 32-bit process */
    GetSystemDirectoryW(SystemDir, _countof(SystemDir));
    ok(wcsstr(SystemDir, L"SysWOW64") != NULL || wcsstr(SystemDir, L"syswow64") != NULL,
       "GetSystemDirectory returned %ls\n", SystemDir);

    /* Redirected: a file created in system32 lands in SysWOW64 */
    if (!MakeFile(System32Path))
    {
        skip("Cannot create %ls (error %lu)\n", System32Path, GetLastError());
        return;
    }
    ok(Exists(SysWow64Path), "The file created in system32 is not in SysWOW64\n");

    Ret = Wow64DisableWow64FsRedirection(&Cookie);
    ok(Ret, "Wow64DisableWow64FsRedirection failed with %lu\n", GetLastError());
    if (Ret)
    {
        /* Not redirected: system32 is the real one, without the file */
        ok(!Exists(System32Path), "The redirected file is in the real system32\n");
        ok(MakeFile(System32Path), "Creating the file in the real system32 failed with %lu\n", GetLastError());
        ok(Exists(System32Path), "The file is not in the real system32\n");

        ok(Wow64RevertWow64FsRedirection(Cookie), "Wow64RevertWow64FsRedirection failed with %lu\n", GetLastError());
    }

    /* Redirected again: system32 shows SysWOW64's file; delete it */
    ok(Exists(System32Path), "After the revert, system32 does not show SysWOW64's file\n");
    ok(DeleteFileW(SysWow64Path), "Deleting %ls failed with %lu\n", SysWow64Path, GetLastError());
    ok(!Exists(System32Path), "After the revert, system32 still shows a file (not redirected?)\n");

    /* The real system32's file */
    if (Wow64DisableWow64FsRedirection(&Cookie2))
    {
        if (Ret)
            ok(Exists(System32Path), "The file in the real system32 is gone\n");
        DeleteFileW(System32Path);
        Wow64RevertWow64FsRedirection(Cookie2);
    }
}
