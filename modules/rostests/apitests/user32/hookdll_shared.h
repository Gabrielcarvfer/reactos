/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Data shared between GlobalKeyboardHook and its hook DLL
 */

#pragma once

#define HOOKDLL_SHARED_NAME L"user32_apitest_hookdll_shared"

typedef struct _HOOKDLL_SHARED
{
    DWORD InstallerPid;
    DWORD ChildPid;
    LONG RefuseOtherProcesses;
    LONG Calls;
    LONG CallsInChild;
} HOOKDLL_SHARED, *PHOOKDLL_SHARED;
