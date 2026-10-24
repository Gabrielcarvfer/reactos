/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for services every Windows has, that installers check
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Tailscale's MSI (custom action CheckServiceDependencies) fails with
 * ERROR_SERVICE_DOES_NOT_EXIST unless the services its service depends on
 * exist and can start.
 */

#include "precomp.h"

START_TEST(WindowsServices)
{
    static const LPCWSTR Names[] = { L"Dnscache", L"iphlpsvc", L"netprofm", L"WinHttpAutoProxySvc" };
    SC_HANDLE hScm, hService;
    BYTE Buffer[8192];
    LPQUERY_SERVICE_CONFIGW Config = (LPQUERY_SERVICE_CONFIGW)Buffer;
    SERVICE_STATUS Status;
    DWORD Needed, i;

    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    ok(hScm != NULL, "OpenSCManagerW failed: %lu\n", GetLastError());
    if (!hScm)
        return;

    for (i = 0; i < _countof(Names); i++)
    {
        hService = OpenServiceW(hScm, Names[i], SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        ok(hService != NULL, "%ls: OpenServiceW failed: %lu\n", Names[i], GetLastError());
        if (!hService)
            continue;

        ok(QueryServiceConfigW(hService, Config, sizeof(Buffer), &Needed),
           "%ls: QueryServiceConfigW failed: %lu\n", Names[i], GetLastError());
        ok(Config->dwStartType != SERVICE_DISABLED, "%ls is disabled\n", Names[i]);
        ok(Config->dwServiceType & SERVICE_WIN32, "%ls: type 0x%lx\n", Names[i], Config->dwServiceType);
        ok(QueryServiceStatus(hService, &Status), "%ls: QueryServiceStatus failed: %lu\n", Names[i], GetLastError());

        CloseServiceHandle(hService);
    }

    CloseServiceHandle(hScm);
}
