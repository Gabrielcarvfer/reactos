/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     A DLL whose DllMain fails DLL_PROCESS_ATTACH (for DllInitFail)
 */

#include <windef.h>
#include <winbase.h>

static void Count(LPCWSTR pszName)
{
    WCHAR szValue[16];
    DWORD dwCount = 0;
    if (GetEnvironmentVariableW(pszName, szValue, ARRAYSIZE(szValue)))
        dwCount = wcstoul(szValue, NULL, 10);
    wsprintfW(szValue, L"%lu", dwCount + 1);
    SetEnvironmentVariableW(pszName, szValue);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        Count(L"INIT_FAIL_DLL_ATTACH");
        return FALSE;
    }
    if (dwReason == DLL_PROCESS_DETACH)
        Count(L"INIT_FAIL_DLL_DETACH");
    return TRUE;
}
