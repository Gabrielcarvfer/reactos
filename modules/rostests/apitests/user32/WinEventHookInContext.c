/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     An in-context WinEvent hook whose module can't be loaded in
 *              another process must not crash that process
 *
 * Chrome's GPU process crashed in user32 (User32CallEventProcFromKernel)
 * when it created its window: an in-context hook set by another process
 * pointed into a module the GPU process didn't have, and user32 called the
 * hooking process's address. Windows skips such hooks.
 */

#include "precomp.h"

#include <strsafe.h>

static void
RunChild(void)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"WinEventHookInContext child", WS_OVERLAPPEDWINDOW,
                                0, 0, 100, 100, NULL, NULL, NULL, NULL);
    MSG msg;
    DWORD dwEnd = GetTickCount() + 500;

    ShowWindow(hwnd, SW_SHOW);
    while ((LONG)(GetTickCount() - dwEnd) < 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(10);
    }
    DestroyWindow(hwnd);
    ExitProcess(0);
}

START_TEST(WinEventHookInContext)
{
    WCHAR szSystem[MAX_PATH], szTemp[MAX_PATH], szDll[MAX_PATH], szMoved[MAX_PATH];
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH * 2];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HMODULE hmod;
    FARPROC pfn;
    HWINEVENTHOOK hHook;
    DWORD dwExit = 0xDEADBEEF;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        RunChild();
        return;
    }

    /* A private copy of a small DLL, loaded here only, then renamed: no other
       process can load it by its name any more */
    GetSystemDirectoryW(szSystem, _countof(szSystem));
    GetTempPathW(_countof(szTemp), szTemp);
    StringCbPrintfW(szDll, sizeof(szDll), L"%lsevhook%lu.dll", szTemp, GetCurrentProcessId());
    StringCbPrintfW(szMoved, sizeof(szMoved), L"%lsevhook%lu.old", szTemp, GetCurrentProcessId());
    StringCbCatW(szSystem, sizeof(szSystem), L"\\msimg32.dll");
    ok(CopyFileW(szSystem, szDll, FALSE), "CopyFile failed with %lu\n", GetLastError());
    hmod = LoadLibraryW(szDll);
    ok(hmod != NULL, "LoadLibrary failed with %lu\n", GetLastError());
    if (!hmod)
        return;
    pfn = GetProcAddress(hmod, "AlphaBlend");
    ok(pfn != NULL, "GetProcAddress failed\n");
    ok(MoveFileExW(szDll, szMoved, MOVEFILE_REPLACE_EXISTING), "MoveFileEx failed with %lu\n", GetLastError());

    hHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, hmod, (WINEVENTPROC)pfn,
                            0, 0, WINEVENT_INCONTEXT | WINEVENT_SKIPOWNPROCESS);
    ok(hHook != NULL, "SetWinEventHook failed with %lu\n", GetLastError());

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCbPrintfW(szCmd, sizeof(szCmd), L"\"%ls\" WinEventHookInContext child", szExe);
    if (CreateProcessW(szExe, szCmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        ok(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, "the child didn't finish\n");
        GetExitCodeProcess(pi.hProcess, &dwExit);
        ok(dwExit == 0, "the child exited with 0x%lx (crashed in the event hook?)\n", dwExit);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        skip("CreateProcess failed with %lu\n", GetLastError());
    }

    if (hHook)
        UnhookWinEvent(hHook);
    FreeLibrary(hmod);
    DeleteFileW(szMoved);
}
