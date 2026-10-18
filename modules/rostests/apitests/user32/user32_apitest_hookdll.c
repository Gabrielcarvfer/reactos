/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Hook DLL for the GlobalKeyboardHook test
 */

#include <windef.h>
#include <winbase.h>
#include <winuser.h>

#include "hookdll_shared.h"

static HANDLE s_hMap;
static PHOOKDLL_SHARED s_pShared;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        s_hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, HOOKDLL_SHARED_NAME);
        if (!s_hMap)
            return FALSE;
        s_pShared = MapViewOfFile(s_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*s_pShared));
        if (!s_pShared)
            return FALSE;
        /* A hook DLL that cannot be loaded in the process getting the input */
        if (s_pShared->RefuseOtherProcesses && GetCurrentProcessId() != s_pShared->InstallerPid)
            return FALSE;
    }
    else if (dwReason == DLL_PROCESS_DETACH && s_pShared)
    {
        UnmapViewOfFile(s_pShared);
        CloseHandle(s_hMap);
    }
    return TRUE;
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_pShared)
    {
        InterlockedIncrement(&s_pShared->Calls);
        if (GetCurrentProcessId() == s_pShared->ChildPid)
            InterlockedIncrement(&s_pShared->CallsInChild);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
