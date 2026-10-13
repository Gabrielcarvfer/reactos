/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for ShellExecCmdLine with programs registered under App Paths
 */

#include "shelltest.h"
#include <shlwapi.h>

#ifndef SECL_NO_UI
    #define SECL_NO_UI          0x2
    #define SECL_ALLOW_NONEXE   0x20
#endif

typedef HRESULT (WINAPI *SHELLEXECCMDLINE)(HWND, LPCWSTR, LPCWSTR, INT, LPVOID, DWORD);

/* The Run dialog starts "iexplore <url>" like this: the program is found under
 * HKLM\...\App Paths even though it is not in the PATH, and gets the arguments. */
START_TEST(ShellExecCmdLineAppPaths)
{
    static const WCHAR szKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\rostest_apppath.exe";
    SHELLEXECCMDLINE pShellExecCmdLine;
    WCHAR szCmd[MAX_PATH];
    HKEY hKey;
    HMODULE hShell32 = GetModuleHandleW(L"shell32.dll");
    HRESULT hr;

    /* Exported by ordinal only, since Vista (and ReactOS builds with Vista+ exports).
     * ReactOS builds with older exports give that ordinal to RealShellExecuteA. */
    pShellExecCmdLine = (SHELLEXECCMDLINE)GetProcAddress(hShell32, (LPCSTR)(INT_PTR)265);
    if (!pShellExecCmdLine || (FARPROC)pShellExecCmdLine == GetProcAddress(hShell32, "RealShellExecuteA"))
    {
        skip("ShellExecCmdLine is not exported\n");
        return;
    }

    GetSystemDirectoryW(szCmd, _countof(szCmd));
    PathAppendW(szCmd, L"cmd.exe");
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, szKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        skip("Cannot create an App Paths key (not an administrator?)\n");
        return;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)szCmd, (DWORD)(wcslen(szCmd) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);

    hr = pShellExecCmdLine(NULL, L"rostest_apppath.exe /c exit", NULL, SW_HIDE, NULL,
                           SECL_NO_UI | SECL_ALLOW_NONEXE);
    ok(hr == S_OK, "App Paths program with arguments: 0x%lx\n", hr);

    hr = pShellExecCmdLine(NULL, L"rostest_apppath /c exit", NULL, SW_HIDE, NULL,
                           SECL_NO_UI | SECL_ALLOW_NONEXE);
    ok(hr == S_OK, "App Paths program without .exe, with arguments: 0x%lx\n", hr);

    hr = pShellExecCmdLine(NULL, L"rostest_no_such_program /c exit", NULL, SW_HIDE, NULL,
                           SECL_NO_UI | SECL_ALLOW_NONEXE);
    ok(FAILED(hr), "Unknown program: 0x%lx, expected a failure\n", hr);

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, szKey);
}
