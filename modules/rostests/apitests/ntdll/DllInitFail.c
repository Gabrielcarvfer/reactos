/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     A DLL whose DllMain fails DLL_PROCESS_ATTACH is unloaded
 *
 * When DllMain returns FALSE for DLL_PROCESS_ATTACH during LoadLibrary,
 * LoadLibrary fails with ERROR_DLL_INIT_FAILED, the system calls DllMain
 * with DLL_PROCESS_DETACH and unloads the DLL: GetModuleHandle can't find
 * it afterwards, and loading it again runs DllMain again.
 */

#include "precomp.h"
#include <winuser.h>

static DWORD GetCount(LPCWSTR pszName)
{
    WCHAR szValue[16];
    if (!GetEnvironmentVariableW(pszName, szValue, _countof(szValue)))
        return 0;
    return wcstoul(szValue, NULL, 10);
}

static WCHAR s_szPath[MAX_PATH];
static ATOM (WINAPI *pRegisterClassW)(const WNDCLASSW *);
static HWND (WINAPI *pCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static BOOL (WINAPI *pDestroyWindow)(HWND);
static LRESULT (WINAPI *pDefWindowProcW)(HWND, UINT, WPARAM, LPARAM);
static HMODULE s_hModInCallback = (HMODULE)1;
static BOOL s_bStillLoadedInCallback = TRUE;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        /* From a user mode callback of win32k, like a hook DLL loaded in
           the thread that gets the input */
        s_hModInCallback = LoadLibraryExW(s_szPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        s_bStillLoadedInCallback = GetModuleHandleW(L"init_fail_dll.dll") != NULL;
    }
    return pDefWindowProcW(hwnd, uMsg, wParam, lParam);
}

START_TEST(DllInitFail)
{
    WNDCLASSW wc = { 0 };
    HMODULE hUser32;
    HWND hwnd;
    WCHAR *p;
    HMODULE hMod;
    DWORD dwError;

    SetEnvironmentVariableW(L"INIT_FAIL_DLL_ATTACH", NULL);
    SetEnvironmentVariableW(L"INIT_FAIL_DLL_DETACH", NULL);

    SetLastError(0xdeadbeef);
    hMod = LoadLibraryW(L"init_fail_dll.dll");
    dwError = GetLastError();
    ok(hMod == NULL, "LoadLibrary returned %p\n", hMod);
    ok(dwError == ERROR_DLL_INIT_FAILED, "error %lu\n", dwError);
    ok(GetCount(L"INIT_FAIL_DLL_ATTACH") == 1, "attach called %lu times\n", GetCount(L"INIT_FAIL_DLL_ATTACH"));
    ok(GetCount(L"INIT_FAIL_DLL_DETACH") == 1, "detach called %lu times\n", GetCount(L"INIT_FAIL_DLL_DETACH"));
    ok(GetModuleHandleW(L"init_fail_dll.dll") == NULL, "the DLL is still loaded\n");

    /* Again: DllMain runs again, and fails again */
    hMod = LoadLibraryW(L"init_fail_dll.dll");
    ok(hMod == NULL, "LoadLibrary returned %p\n", hMod);
    ok(GetCount(L"INIT_FAIL_DLL_ATTACH") == 2, "attach called %lu times\n", GetCount(L"INIT_FAIL_DLL_ATTACH"));
    ok(GetModuleHandleW(L"init_fail_dll.dll") == NULL, "the DLL is still loaded\n");

    /* With a full path and LOAD_WITH_ALTERED_SEARCH_PATH */
    GetModuleFileNameW(NULL, s_szPath, _countof(s_szPath));
    p = wcsrchr(s_szPath, L'\\');
    if (p) *(p + 1) = 0;
    wcscat(s_szPath, L"init_fail_dll.dll");
    hMod = LoadLibraryExW(s_szPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    ok(hMod == NULL, "LoadLibraryEx returned %p\n", hMod);
    ok(GetModuleHandleW(L"init_fail_dll.dll") == NULL, "the DLL is still loaded\n");
    ok(GetCount(L"INIT_FAIL_DLL_ATTACH") == 3, "attach called %lu times\n", GetCount(L"INIT_FAIL_DLL_ATTACH"));

    /* From a win32k callback (user32 loaded on demand: this test is about
       ntdll's loader, it doesn't import user32) */
    hUser32 = LoadLibraryW(L"user32.dll");
    pRegisterClassW = (void *)GetProcAddress(hUser32, "RegisterClassW");
    pCreateWindowExW = (void *)GetProcAddress(hUser32, "CreateWindowExW");
    pDestroyWindow = (void *)GetProcAddress(hUser32, "DestroyWindow");
    pDefWindowProcW = (void *)GetProcAddress(hUser32, "DefWindowProcW");
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DllInitFail";
    pRegisterClassW(&wc);
    hwnd = pCreateWindowExW(0, L"DllInitFail", L"", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);
    ok(s_hModInCallback == NULL, "LoadLibraryEx in a callback returned %p\n", s_hModInCallback);
    ok(!s_bStillLoadedInCallback, "the DLL is still loaded in the callback\n");
    ok(GetModuleHandleW(L"init_fail_dll.dll") == NULL, "the DLL is still loaded\n");
    ok(GetCount(L"INIT_FAIL_DLL_ATTACH") == 4, "attach called %lu times\n", GetCount(L"INIT_FAIL_DLL_ATTACH"));
    pDestroyWindow(hwnd);
}
