/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     A global WH_KEYBOARD hook runs in the thread that gets the
 *              input, and input keeps flowing when its DLL can't be loaded
 */

#include "precomp.h"
#include "hookdll_shared.h"

#include <strsafe.h>

static const WCHAR s_Text[] = L"Hello2.js HELLO2.JS ~!@#$%^&*()_+{}|:\"<>? end";

static void SendKey(WORD wVk, BOOL bUp)
{
    INPUT input = { INPUT_KEYBOARD };
    input.ki.wVk = wVk;
    input.ki.wScan = (WORD)MapVirtualKeyW(wVk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = bUp ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(input));
}

/* The child: type s_Text into its own window. Exit code 0 when every
 * character arrived and Shift is up again. */
static int RunChild(void)
{
    WCHAR szGot[128];
    int nGot = 0, i, nExpected = 0;
    HWND hwnd;
    MSG msg;
    DWORD dwStart;

    hwnd = CreateWindowW(L"EDIT", L"GlobalKeyboardHook child", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         100, 100, 400, 200, NULL, NULL, NULL, NULL);
    if (!hwnd)
        return 10;
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);

    for (i = 0; s_Text[i]; i++)
    {
        SHORT vks = VkKeyScanW(s_Text[i]);
        BOOL bShift = (vks >> 8) & 1;
        if (vks == -1)
            continue;
        nExpected++;
        if (bShift) SendKey(VK_LSHIFT, FALSE);
        SendKey(LOBYTE(vks), FALSE);
        SendKey(LOBYTE(vks), TRUE);
        if (bShift) SendKey(VK_LSHIFT, TRUE);
    }

    dwStart = GetTickCount();
    while (GetTickCount() - dwStart < 5000)
    {
        if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            Sleep(10);
            continue;
        }
        if (msg.message == WM_CHAR && nGot < (int)_countof(szGot) - 1)
            szGot[nGot++] = (WCHAR)msg.wParam;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    szGot[nGot] = 0;
    DestroyWindow(hwnd);
    if (nGot != nExpected || wcsncmp(szGot, s_Text, nGot))
        return 1;
    if ((GetKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000))
        return 2;
    return 0;
}

static void RunHookTest(PHOOKDLL_SHARED pShared, BOOL bRefuse)
{
    WCHAR szExe[MAX_PATH], szCmd[MAX_PATH + 64], szDll[MAX_PATH], *p;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HMODULE hDll;
    HOOKPROC pfnProc;
    HHOOK hHook;
    DWORD dwExit = 99;
    MSG msg;

    GetModuleFileNameW(NULL, szExe, _countof(szExe));
    StringCbCopyW(szDll, sizeof(szDll), szExe);
    p = wcsrchr(szDll, L'\\');
    if (p) *(p + 1) = 0; else szDll[0] = 0;
    StringCbCatW(szDll, sizeof(szDll), L"user32_apitest_hookdll.dll");

    pShared->InstallerPid = GetCurrentProcessId();
    pShared->RefuseOtherProcesses = bRefuse;
    pShared->Calls = pShared->CallsInChild = 0;

    hDll = LoadLibraryW(szDll);
    ok(hDll != NULL, "LoadLibrary(%S) failed: %lu\n", szDll, GetLastError());
    if (!hDll)
        return;
    pfnProc = (HOOKPROC)GetProcAddress(hDll, "KeyboardProc");
    hHook = SetWindowsHookExW(WH_KEYBOARD, pfnProc, hDll, 0);
    ok(hHook != NULL, "SetWindowsHookEx failed: %lu\n", GetLastError());

    StringCbPrintfW(szCmd, sizeof(szCmd), L"\"%ls\" GlobalKeyboardHook child", szExe);
    if (CreateProcessW(NULL, szCmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        pShared->ChildPid = pi.dwProcessId;
        ResumeThread(pi.hThread);
        /* Keep pumping: hooks that can't run in context come here */
        while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 30000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }
        GetExitCodeProcess(pi.hProcess, &dwExit);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        ok(0, "CreateProcess failed: %lu\n", GetLastError());
    }

    ok(dwExit == 0, "child: %s (exit %lu)\n",
       dwExit == 1 ? "characters lost" : dwExit == 2 ? "Shift stuck" : "failed", dwExit);
    if (bRefuse)
    {
        ok(pShared->CallsInChild == 0, "hook ran %ld times in the child\n", pShared->CallsInChild);
    }
    else
    {
        ok(pShared->Calls > 0, "the hook never ran\n");
        ok(pShared->CallsInChild > 0 && pShared->CallsInChild == pShared->Calls,
           "hook ran %ld times, %ld of them in the child's process\n",
           pShared->Calls, pShared->CallsInChild);
    }

    if (hHook)
        UnhookWindowsHookEx(hHook);
    FreeLibrary(hDll);
}

START_TEST(GlobalKeyboardHook)
{
    HANDLE hMap;
    PHOOKDLL_SHARED pShared;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
        ExitProcess(RunChild());

    hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                              sizeof(HOOKDLL_SHARED), HOOKDLL_SHARED_NAME);
    ok(hMap != NULL, "CreateFileMapping failed: %lu\n", GetLastError());
    if (!hMap)
        return;
    pShared = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*pShared));

    /* The hook runs in the child, which got the keys */
    RunHookTest(pShared, FALSE);
    /* The hook DLL refuses to load in the child: input still works */
    RunHookTest(pShared, TRUE);

    UnmapViewOfFile(pShared);
    CloseHandle(hMap);
}
