/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for cfgmgr32's CM_MapCrToWin32Err (Windows 8)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* NVDA 2023 calls it (through ctypes) while loading its braille drivers */

#include <apitest.h>
#include <winreg.h>
#include <cfgmgr32.h>

typedef DWORD (WINAPI *FN_CM_MapCrToWin32Err)(CONFIGRET, DWORD);

START_TEST(CM_MapCrToWin32Err)
{
    static const struct { CONFIGRET Cr; DWORD Error; } Tests[] =
    {
        { CR_SUCCESS, ERROR_SUCCESS },
        { CR_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY },
        { CR_INVALID_POINTER, ERROR_INVALID_USER_BUFFER },
        { CR_INVALID_FLAG, ERROR_INVALID_FLAGS },
        { CR_INVALID_DEVNODE, ERROR_INVALID_DATA },
        { CR_INVALID_DEVICE_ID, ERROR_INVALID_DATA },
        { CR_NO_SUCH_DEVNODE, ERROR_NOT_FOUND },
        { CR_NO_SUCH_VALUE, ERROR_NOT_FOUND },
        { CR_NO_SUCH_REGISTRY_KEY, ERROR_FILE_NOT_FOUND },
        { CR_ALREADY_SUCH_DEVNODE, ERROR_ALREADY_EXISTS },
        { CR_BUFFER_SMALL, ERROR_INSUFFICIENT_BUFFER },
        { CR_ACCESS_DENIED, ERROR_ACCESS_DENIED },
        { CR_CALL_NOT_IMPLEMENTED, ERROR_CALL_NOT_IMPLEMENTED },
        /* No Win32 equivalent: the default */
        { CR_DEFAULT, 0x1234 },
        { CR_NOT_DISABLEABLE, 0x1234 },
    };
    HMODULE hCfgMgr = LoadLibraryW(L"cfgmgr32.dll");
    FN_CM_MapCrToWin32Err pCM_MapCrToWin32Err;
    ULONG i;

    pCM_MapCrToWin32Err = (FN_CM_MapCrToWin32Err)GetProcAddress(hCfgMgr, "CM_MapCrToWin32Err");
    ok(pCM_MapCrToWin32Err != NULL, "CM_MapCrToWin32Err is missing\n");
    if (!pCM_MapCrToWin32Err)
        return;

    for (i = 0; i < _countof(Tests); i++)
    {
        DWORD Error = pCM_MapCrToWin32Err(Tests[i].Cr, 0x1234);
        ok(Error == Tests[i].Error, "CR 0x%lx: got %lu, expected %lu\n", Tests[i].Cr, Error, Tests[i].Error);
    }
}
