/*
 * PROJECT:     ReactOS service stubs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Services that exist on Windows, which ReactOS doesn't implement yet:
 *              they start and run, doing nothing (like wuauserv).
 *              The IP Helper (iphlpsvc), the Network List Service (netprofm)
 *              and the WinHTTP Web Proxy Auto-Discovery Service
 *              (WinHttpAutoProxySvc): Tailscale's installer checks that the
 *              services its own service depends on exist.
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winsvc.h>

#define NDEBUG
#include <debug.h>

static SERVICE_STATUS_HANDLE ServiceStatusHandle;
static SERVICE_STATUS ServiceStatus;
static HANDLE hStopEvent;

static VOID
UpdateServiceStatus(DWORD dwState)
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    ServiceStatus.dwCurrentState = dwState;
    ServiceStatus.dwControlsAccepted = 0;
    ServiceStatus.dwWin32ExitCode = 0;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 0;
    ServiceStatus.dwWaitHint = (dwState == SERVICE_STOP_PENDING) ? 1000 : 0;
    if (dwState == SERVICE_RUNNING)
        ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
}

static DWORD WINAPI
ServiceControlHandler(DWORD dwControl,
                      DWORD dwEventType,
                      LPVOID lpEventData,
                      LPVOID lpContext)
{
    switch (dwControl)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            UpdateServiceStatus(SERVICE_STOP_PENDING);
            SetEvent(hStopEvent);
            return ERROR_SUCCESS;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(ServiceStatusHandle, &ServiceStatus);
            return ERROR_SUCCESS;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

VOID WINAPI
ServiceMain(DWORD argc, LPWSTR *argv)
{
    /* (svchost passes the name of the service as the first argument) */
    ServiceStatusHandle = RegisterServiceCtrlHandlerExW(argc > 0 ? argv[0] : L"",
                                                        ServiceControlHandler,
                                                        NULL);
    if (!ServiceStatusHandle)
    {
        DPRINT1("RegisterServiceCtrlHandlerExW() failed (Error %lu)\n", GetLastError());
        return;
    }

    hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (hStopEvent)
    {
        UpdateServiceStatus(SERVICE_RUNNING);
        WaitForSingleObject(hStopEvent, INFINITE);
        CloseHandle(hStopEvent);
    }

    UpdateServiceStatus(SERVICE_STOPPED);
}

BOOL WINAPI
DllMain(HINSTANCE hinstDLL,
        DWORD fdwReason,
        LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hinstDLL);
    return TRUE;
}
