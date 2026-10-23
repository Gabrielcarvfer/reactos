/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for InitiateShutdownW/A
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Open-Shell's StartMenuDLL.dll imports InitiateShutdownW and failed to
 * load without it. The test never shuts the machine down: it only asks a
 * computer that does not exist, which must fail with an error code.
 */

#include "precomp.h"

typedef DWORD (WINAPI *PFN_SHUTDOWN_W)(LPWSTR, LPWSTR, DWORD, DWORD, DWORD);
typedef DWORD (WINAPI *PFN_SHUTDOWN_A)(LPSTR, LPSTR, DWORD, DWORD, DWORD);

#define TEST_SHUTDOWN_RESTART 0x4

START_TEST(InitiateShutdown)
{
    HMODULE Advapi32 = GetModuleHandleW(L"advapi32.dll");
    PFN_SHUTDOWN_W pInitiateShutdownW = (PFN_SHUTDOWN_W)GetProcAddress(Advapi32, "InitiateShutdownW");
    PFN_SHUTDOWN_A pInitiateShutdownA = (PFN_SHUTDOWN_A)GetProcAddress(Advapi32, "InitiateShutdownA");
    WCHAR MachineW[] = L"\\\\reactos-no-such-host";
    CHAR MachineA[] = "\\\\reactos-no-such-host";
    DWORD Error;

    if (!pInitiateShutdownW || !pInitiateShutdownA)
    {
        skip("InitiateShutdown is missing (before Vista)\n");
        return;
    }

    Error = pInitiateShutdownW(MachineW, NULL, 30, TEST_SHUTDOWN_RESTART, 0);
    ok(Error != ERROR_SUCCESS, "InitiateShutdownW on a missing computer succeeded\n");
    Error = pInitiateShutdownA(MachineA, NULL, 30, TEST_SHUTDOWN_RESTART, 0);
    ok(Error != ERROR_SUCCESS, "InitiateShutdownA on a missing computer succeeded\n");
}
