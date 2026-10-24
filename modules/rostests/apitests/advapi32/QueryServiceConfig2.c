/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Test for QueryServiceConfig2A/W
 * PROGRAMMER:      Hermès BÉLUSCA - MAÏTO
 */

#include "precomp.h"

#define TESTING_SERVICEW     L"Spooler"
#define TESTING_SERVICEA      "Spooler"

/*
 * Taken from base/system/services/config.c and adapted.
 */
static DWORD
RegReadStringW(HKEY   hKey,
               LPWSTR lpValueName,
               LPWSTR *lpValue)
{
    DWORD dwError;
    DWORD dwSize;
    DWORD dwType;

    *lpValue = NULL;

    dwSize  = 0;
    dwError = RegQueryValueExW(hKey,
                               lpValueName,
                               0,
                               &dwType,
                               NULL,
                               &dwSize);
    if (dwError != ERROR_SUCCESS)
        return dwError;

    *lpValue = HeapAlloc(GetProcessHeap(), 0, dwSize);
    if (*lpValue == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    dwError = RegQueryValueExW(hKey,
                               lpValueName,
                               0,
                               &dwType,
                               (LPBYTE)*lpValue,
                               &dwSize);
    if (dwError != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, *lpValue);
        *lpValue = NULL;
    }

    return dwError;
}

static DWORD
RegReadStringA(HKEY  hKey,
               LPSTR lpValueName,
               LPSTR *lpValue)
{
    DWORD dwError;
    DWORD dwSize;
    DWORD dwType;

    *lpValue = NULL;

    dwSize  = 0;
    dwError = RegQueryValueExA(hKey,
                               lpValueName,
                               0,
                               &dwType,
                               NULL,
                               &dwSize);
    if (dwError != ERROR_SUCCESS)
        return dwError;

    *lpValue = HeapAlloc(GetProcessHeap(), 0, dwSize);
    if (*lpValue == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    dwError = RegQueryValueExA(hKey,
                               lpValueName,
                               0,
                               &dwType,
                               (LPBYTE)*lpValue,
                               &dwSize);
    if (dwError != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, *lpValue);
        *lpValue = NULL;
    }

    return dwError;
}


static int QueryConfig2W(SC_HANDLE hService, LPCWSTR serviceName, DWORD dwInfoLevel)
{
    int    iRet  = 0;
    LONG   lRet  = 0;
    DWORD  dwRet = 0;
    BOOL   bError = FALSE;
    DWORD  dwRequiredSize = 0;
    LPBYTE lpBuffer = NULL;

    WCHAR keyName[256];
    HKEY hKey = NULL;
    DWORD dwType = 0;

    /* Get the needed size */
    SetLastError(0xdeadbeef);
    bError = QueryServiceConfig2W(hService,
                                  dwInfoLevel,
                                  NULL,
                                  0,
                                  &dwRequiredSize);
    ok(bError == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "(bError, GetLastError()) = (%u, 0x%08lx), expected (FALSE, 0x%08lx)\n", bError, GetLastError(), (DWORD)ERROR_INSUFFICIENT_BUFFER);
    ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");
    if (dwRequiredSize == 0)
    {
        skip("Required size is null; cannot proceed with QueryConfig2W --> %lu test\n", dwInfoLevel);
        return 1;
    }

    /* Allocate memory */
    lpBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwRequiredSize);
    if (lpBuffer == NULL)
    {
        skip("Cannot allocate %lu bytes of memory\n", dwRequiredSize);
        return 2;
    }

    /* Get the actual value */
    SetLastError(0xdeadbeef);
    bError = QueryServiceConfig2W(hService,
                                  dwInfoLevel,
                                  lpBuffer,
                                  dwRequiredSize,
                                  &dwRequiredSize);
    ok(bError, "bError = %u, expected TRUE\n", bError);
    if (bError == FALSE)
    {
        skip("QueryServiceConfig2W returned an error; cannot proceed with QueryConfig2W --> %lu test\n", dwInfoLevel);
        HeapFree(GetProcessHeap(), 0, lpBuffer);
        return 3;
    }

    /* Now we can compare the retrieved value with what it's actually stored in the registry */
    StringCbPrintfW(keyName, sizeof(keyName), L"System\\CurrentControlSet\\Services\\%s", serviceName);
    SetLastError(0xdeadbeef);
    lRet = RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyName, 0, KEY_QUERY_VALUE, &hKey);
    ok(lRet == ERROR_SUCCESS, "RegOpenKeyExW failed with 0x%08lx\n", lRet);
    if (lRet != ERROR_SUCCESS)
    {
        skip("No regkey; cannot proceed with QueryConfig2W --> %lu test\n", dwInfoLevel);
        HeapFree(GetProcessHeap(), 0, lpBuffer);
        return 4;
    }

    switch (dwInfoLevel)
    {
        case SERVICE_CONFIG_DESCRIPTION:
        {
            LPSERVICE_DESCRIPTIONW lpDescription = (LPSERVICE_DESCRIPTIONW)lpBuffer;
            LPWSTR lpszDescription = NULL;

            /* Retrieve the description via the registry */
            dwRet = RegReadStringW(hKey, L"Description", &lpszDescription);
            ok(dwRet == ERROR_SUCCESS, "RegReadStringW returned 0x%08lx\n", dwRet);
            ok(lpszDescription != NULL, "lpszDescription is null, expected non-null\n");

            /* Compare it with the description retrieved via QueryServiceConfig2 */
            if (lpszDescription)
                iRet = wcscmp(lpDescription->lpDescription, lpszDescription);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved descriptions are different !\n");


            /* Memory cleanup */
            HeapFree(GetProcessHeap(), 0, lpszDescription);

            break;
        }

        case SERVICE_CONFIG_FAILURE_ACTIONS:
        {
            LPSERVICE_FAILURE_ACTIONSW lpFailureActions1 = (LPSERVICE_FAILURE_ACTIONSW)lpBuffer;
            LPSERVICE_FAILURE_ACTIONSW lpFailureActions2 = NULL;
            LPWSTR lpRebootMessage  = NULL;
            LPWSTR lpFailureCommand = NULL;
            DWORD  i = 0;

            /* Retrieve the failure actions via the registry */
            lRet = RegQueryValueExW(hKey,
                                    L"FailureActions",
                                    NULL,
                                    &dwType,
                                    NULL,
                                    &dwRequiredSize);
            ok(lRet == ERROR_SUCCESS, "RegQueryValueExW returned 0x%08lx\n", lRet);
            ok(dwType == REG_BINARY, "dwType = %lu, expected REG_BINARY\n", dwType);
            ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");

            lpFailureActions2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwRequiredSize);
            if (lpFailureActions2 == NULL)
            {
                skip("Cannot allocate %lu bytes of memory\n", dwRequiredSize);
                break;
            }

            lRet = RegQueryValueExW(hKey,
                                    L"FailureActions",
                                    NULL,
                                    NULL,
                                    (LPBYTE)lpFailureActions2,
                                    &dwRequiredSize);
            ok(lRet == ERROR_SUCCESS, "RegQueryValueExW returned 0x%08lx\n", lRet);
            ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");

            /* Get the strings */
            RegReadStringW(hKey, L"FailureCommand", &lpFailureCommand);
            RegReadStringW(hKey, L"RebootMessage" , &lpRebootMessage );

            /* Check the values */
            ok(lpFailureActions1->dwResetPeriod == lpFailureActions2->dwResetPeriod, "lpFailureActions1->dwResetPeriod != lpFailureActions2->dwResetPeriod\n");
#ifndef _M_AMD64 // Fails on Win 2003 x64
            ok(lpFailureActions1->cActions == lpFailureActions2->cActions, "lpFailureActions1->cActions != lpFailureActions2->cActions\n");
#endif
            /* Compare the actions */
            if (lpFailureActions1->cActions == lpFailureActions2->cActions)
            {
                lpFailureActions2->lpsaActions = (lpFailureActions2->cActions > 0 ? (LPSC_ACTION)(lpFailureActions2 + 1) : NULL);

                if (lpFailureActions1->cActions > 0 &&
                    lpFailureActions1->lpsaActions != NULL)
                {
                    for (i = 0; i < lpFailureActions1->cActions; ++i)
                    {
                        ok(lpFailureActions1->lpsaActions[i].Type  == lpFailureActions2->lpsaActions[i].Type , "lpFailureActions1->lpsaActions[%lu].Type  != lpFailureActions2->lpsaActions[%lu].Type\n" , i, i);
                        ok(lpFailureActions1->lpsaActions[i].Delay == lpFailureActions2->lpsaActions[i].Delay, "lpFailureActions1->lpsaActions[%lu].Delay != lpFailureActions2->lpsaActions[%lu].Delay\n", i, i);
                    }
                }
            }

            /* TODO: retrieve the strings if they are in MUI format */

            /* Compare RebootMsg */
            if (lpFailureActions1->lpRebootMsg && lpRebootMessage)
                iRet = wcscmp(lpFailureActions1->lpRebootMsg, lpRebootMessage);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved reboot messages are different !\n");

            /* Compare Command */
            if (lpFailureActions1->lpCommand && lpFailureCommand)
                iRet = wcscmp(lpFailureActions1->lpCommand, lpFailureCommand);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved commands are different !\n");


            /* Memory cleanup */
            if (lpRebootMessage)
                HeapFree(GetProcessHeap(), 0, lpRebootMessage);

            if (lpFailureCommand)
                HeapFree(GetProcessHeap(), 0, lpFailureCommand);

            HeapFree(GetProcessHeap(), 0, lpFailureActions2);

            break;
        }

        default:
            skip("Unknown dwInfoLevel %lu, cannot proceed with QueryConfig2W --> %lu test\n", dwInfoLevel, dwInfoLevel);
            break;
    }

    RegCloseKey(hKey);

    HeapFree(GetProcessHeap(), 0, lpBuffer);

    return 0;
}

static int QueryConfig2A(SC_HANDLE hService, LPCSTR serviceName, DWORD dwInfoLevel)
{
    int    iRet  = 0;
    LONG   lRet  = 0;
    DWORD  dwRet = 0;
    BOOL   bError = FALSE;
    DWORD  dwRequiredSize = 0;
    LPBYTE lpBuffer = NULL;

    CHAR keyName[256];
    HKEY hKey = NULL;
    DWORD dwType = 0;

    /* Get the needed size */
    SetLastError(0xdeadbeef);
    bError = QueryServiceConfig2A(hService,
                                  dwInfoLevel,
                                  NULL,
                                  0,
                                  &dwRequiredSize);
    ok(bError == FALSE && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "(bError, GetLastError()) = (%u, 0x%08lx), expected (FALSE, 0x%08lx)\n", bError, GetLastError(), (DWORD)ERROR_INSUFFICIENT_BUFFER);
    ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");
    if (dwRequiredSize == 0)
    {
        skip("Required size is null; cannot proceed with QueryConfig2A --> %lu test\n", dwInfoLevel);
        return 1;
    }

    /* Allocate memory */
    lpBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwRequiredSize);
    if (lpBuffer == NULL)
    {
        skip("Cannot allocate %lu bytes of memory\n", dwRequiredSize);
        return 2;
    }

    /* Get the actual value */
    SetLastError(0xdeadbeef);
    bError = QueryServiceConfig2A(hService,
                                  dwInfoLevel,
                                  lpBuffer,
                                  dwRequiredSize,
                                  &dwRequiredSize);
    ok(bError, "bError = %u, expected TRUE\n", bError);
    if (bError == FALSE)
    {
        skip("QueryServiceConfig2A returned an error; cannot proceed with QueryConfig2A --> %lu test\n", dwInfoLevel);
        HeapFree(GetProcessHeap(), 0, lpBuffer);
        return 3;
    }

    /* Now we can compare the retrieved value with what it's actually stored in the registry */
    StringCbPrintfA(keyName, sizeof(keyName), "System\\CurrentControlSet\\Services\\%s", serviceName);
    SetLastError(0xdeadbeef);
    lRet = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyName, 0, KEY_QUERY_VALUE, &hKey);
    ok(lRet == ERROR_SUCCESS, "RegOpenKeyExA failed with 0x%08lx\n", lRet);
    if (lRet != ERROR_SUCCESS)
    {
        skip("No regkey; cannot proceed with QueryConfig2A --> %lu test\n", dwInfoLevel);
        HeapFree(GetProcessHeap(), 0, lpBuffer);
        return 4;
    }

    switch (dwInfoLevel)
    {
        case SERVICE_CONFIG_DESCRIPTION:
        {
            LPSERVICE_DESCRIPTIONA lpDescription = (LPSERVICE_DESCRIPTIONA)lpBuffer;
            LPSTR lpszDescription = NULL;

            /* Retrieve the description via the registry */
            dwRet = RegReadStringA(hKey, "Description", &lpszDescription);
            ok(dwRet == ERROR_SUCCESS, "RegReadStringA returned 0x%08lx\n", dwRet);
            ok(lpszDescription != NULL, "lpszDescription is null, expected non-null\n");

            /* Compare it with the description retrieved via QueryServiceConfig2 */
            if (lpszDescription)
                iRet = strcmp(lpDescription->lpDescription, lpszDescription);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved descriptions are different !\n");


            /* Memory cleanup */
            HeapFree(GetProcessHeap(), 0, lpszDescription);

            break;
        }

        case SERVICE_CONFIG_FAILURE_ACTIONS:
        {
            LPSERVICE_FAILURE_ACTIONSA lpFailureActions1 = (LPSERVICE_FAILURE_ACTIONSA)lpBuffer;
            LPSERVICE_FAILURE_ACTIONSA lpFailureActions2 = NULL;
            LPSTR lpRebootMessage  = NULL;
            LPSTR lpFailureCommand = NULL;
            DWORD i = 0;

            /* Retrieve the failure actions via the registry */
            lRet = RegQueryValueExA(hKey,
                                    "FailureActions",
                                    NULL,
                                    &dwType,
                                    NULL,
                                    &dwRequiredSize);
            ok(lRet == ERROR_SUCCESS, "RegQueryValueExA returned 0x%08lx\n", lRet);
            ok(dwType == REG_BINARY, "dwType = %lu, expected REG_BINARY\n", dwType);
            ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");

            lpFailureActions2 = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwRequiredSize);
            if (lpFailureActions2 == NULL)
            {
                skip("Cannot allocate %lu bytes of memory\n", dwRequiredSize);
                break;
            }

            lRet = RegQueryValueExA(hKey,
                                    "FailureActions",
                                    NULL,
                                    NULL,
                                    (LPBYTE)lpFailureActions2,
                                    &dwRequiredSize);
            ok(lRet == ERROR_SUCCESS, "RegQueryValueExA returned 0x%08lx\n", lRet);
            ok(dwRequiredSize != 0, "dwRequiredSize is zero, expected non-zero\n");

            /* Get the strings */
            RegReadStringA(hKey, "FailureCommand", &lpFailureCommand);
            RegReadStringA(hKey, "RebootMessage" , &lpRebootMessage );

            /* Check the values */
            ok(lpFailureActions1->dwResetPeriod == lpFailureActions2->dwResetPeriod, "lpFailureActions1->dwResetPeriod != lpFailureActions2->dwResetPeriod\n");
#ifndef _M_AMD64 // Fails on Win 2003 x64
            ok(lpFailureActions1->cActions == lpFailureActions2->cActions, "lpFailureActions1->cActions != lpFailureActions2->cActions\n");
#endif

            /* Compare the actions */
            if (lpFailureActions1->cActions == lpFailureActions2->cActions)
            {
                lpFailureActions2->lpsaActions = (lpFailureActions2->cActions > 0 ? (LPSC_ACTION)(lpFailureActions2 + 1) : NULL);

                if (lpFailureActions1->cActions > 0 &&
                    lpFailureActions1->lpsaActions != NULL)
                {
                    for (i = 0; i < lpFailureActions1->cActions; ++i)
                    {
                        ok(lpFailureActions1->lpsaActions[i].Type  == lpFailureActions2->lpsaActions[i].Type , "lpFailureActions1->lpsaActions[%lu].Type  != lpFailureActions2->lpsaActions[%lu].Type\n" , i, i);
                        ok(lpFailureActions1->lpsaActions[i].Delay == lpFailureActions2->lpsaActions[i].Delay, "lpFailureActions1->lpsaActions[%lu].Delay != lpFailureActions2->lpsaActions[%lu].Delay\n", i, i);
                    }
                }
            }

            /* TODO: retrieve the strings if they are in MUI format */

            /* Compare RebootMsg */
            if (lpFailureActions1->lpRebootMsg && lpRebootMessage)
                iRet = strcmp(lpFailureActions1->lpRebootMsg, lpRebootMessage);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved reboot messages are different !\n");

            /* Compare Command */
            if (lpFailureActions1->lpCommand && lpFailureCommand)
                iRet = strcmp(lpFailureActions1->lpCommand, lpFailureCommand);
            else
                iRet = 0;

            ok(iRet == 0, "Retrieved commands are different !\n");


            /* Memory cleanup */
            if (lpRebootMessage)
                HeapFree(GetProcessHeap(), 0, lpRebootMessage);

            if (lpFailureCommand)
                HeapFree(GetProcessHeap(), 0, lpFailureCommand);

            HeapFree(GetProcessHeap(), 0, lpFailureActions2);

            break;
        }

        default:
            skip("Unknown dwInfoLevel %lu, cannot proceed with QueryConfig2A --> %lu test\n", dwInfoLevel, dwInfoLevel);
            break;
    }

    RegCloseKey(hKey);

    HeapFree(GetProcessHeap(), 0, lpBuffer);

    return 0;
}


static void Test_QueryServiceConfig2W(void)
{
    SC_HANDLE hScm     = NULL;
    SC_HANDLE hService = NULL;

    SetLastError(0xdeadbeef);
    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    ok(hScm != NULL, "Failed to open service manager, error=0x%08lx\n", GetLastError());
    if (!hScm)
    {
        skip("No service control manager; cannot proceed with QueryServiceConfig2W test\n");
        goto cleanup;
    }

    ok_err(ERROR_SUCCESS);

    SetLastError(0xdeadbeef);
    hService = OpenServiceW(hScm, TESTING_SERVICEW, SERVICE_QUERY_CONFIG);
    ok(hService != NULL, "Failed to open service handle, error=0x%08lx\n", GetLastError());
    if (!hService)
    {
        skip("Service not found; cannot proceed with QueryServiceConfig2W test\n");
        goto cleanup;
    }

    ok_err(ERROR_SUCCESS);

    if (QueryConfig2W(hService, TESTING_SERVICEW, SERVICE_CONFIG_DESCRIPTION) != 0)
        goto cleanup;

    if (QueryConfig2W(hService, TESTING_SERVICEW, SERVICE_CONFIG_FAILURE_ACTIONS) != 0)
        goto cleanup;

cleanup:
    if (hService)
        CloseServiceHandle(hService);

    if (hScm)
        CloseServiceHandle(hScm);
}

static void Test_QueryServiceConfig2A(void)
{
    SC_HANDLE hScm     = NULL;
    SC_HANDLE hService = NULL;

    SetLastError(0xdeadbeef);
    hScm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    ok(hScm != NULL, "Failed to open service manager, error=0x%08lx\n", GetLastError());
    if (!hScm)
    {
        skip("No service control manager; cannot proceed with QueryServiceConfig2A test\n");
        goto cleanup;
    }

    ok_err(ERROR_SUCCESS);

    SetLastError(0xdeadbeef);
    hService = OpenServiceA(hScm, TESTING_SERVICEA, SERVICE_QUERY_CONFIG);
    ok(hService != NULL, "Failed to open service handle, error=0x%08lx\n", GetLastError());
    if (!hService)
    {
        skip("Service not found; cannot proceed with QueryServiceConfig2A test\n");
        goto cleanup;
    }

    ok_err(ERROR_SUCCESS);

    if (QueryConfig2A(hService, TESTING_SERVICEA, SERVICE_CONFIG_DESCRIPTION) != 0)
        goto cleanup;

    if (QueryConfig2A(hService, TESTING_SERVICEA, SERVICE_CONFIG_FAILURE_ACTIONS) != 0)
        goto cleanup;

cleanup:
    if (hService)
        CloseServiceHandle(hService);

    if (hScm)
        CloseServiceHandle(hScm);
}


/* The levels of Vista and later, on a service every Windows has
 * (levels 3 to 5 return a structure of a single DWORD) */
static void Test_QueryServiceConfig2W_Levels(void)
{
    static const struct { DWORD Level; DWORD MinSize; } Levels[] =
    {
        { SERVICE_CONFIG_DELAYED_AUTO_START_INFO, sizeof(DWORD) },
        { SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, sizeof(DWORD) },
        { SERVICE_CONFIG_SERVICE_SID_INFO, sizeof(DWORD) },
        { SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO, sizeof(SERVICE_REQUIRED_PRIVILEGES_INFOW) },
        { SERVICE_CONFIG_PRESHUTDOWN_INFO, sizeof(SERVICE_PRESHUTDOWN_INFO) },
        { SERVICE_CONFIG_TRIGGER_INFO, sizeof(SERVICE_TRIGGER_INFO) },
    };
    SC_HANDLE hScm, hService;
    BYTE Buffer[1024];
    DWORD Needed, i;
    BOOL Ret;

    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    ok(hScm != NULL, "OpenSCManagerW failed: %lu\n", GetLastError());
    if (!hScm)
        return;
    hService = OpenServiceW(hScm, L"EventLog", SERVICE_QUERY_CONFIG);
    ok(hService != NULL, "OpenServiceW failed: %lu\n", GetLastError());
    if (!hService)
    {
        CloseServiceHandle(hScm);
        return;
    }

    for (i = 0; i < _countof(Levels); i++)
    {
        /* Without a buffer: the size */
        Needed = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        Ret = QueryServiceConfig2W(hService, Levels[i].Level, NULL, 0, &Needed);
        ok(!Ret, "Level %lu: QueryServiceConfig2W succeeded\n", Levels[i].Level);
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Level %lu: error %lu\n", Levels[i].Level, GetLastError());
        ok(Needed >= Levels[i].MinSize && Needed <= sizeof(Buffer), "Level %lu: needed %lu\n", Levels[i].Level, Needed);
        if (Needed > sizeof(Buffer))
            continue;

        memset(Buffer, 0xcc, sizeof(Buffer));
        SetLastError(0xdeadbeef);
        Ret = QueryServiceConfig2W(hService, Levels[i].Level, Buffer, Needed, &Needed);
        ok(Ret, "Level %lu: QueryServiceConfig2W failed: %lu\n", Levels[i].Level, GetLastError());
        if (!Ret)
            continue;

        switch (Levels[i].Level)
        {
            case SERVICE_CONFIG_DELAYED_AUTO_START_INFO:
                ok(*(LPDWORD)Buffer == FALSE, "Delayed auto start\n");
                break;
            case SERVICE_CONFIG_FAILURE_ACTIONS_FLAG:
                ok(*(LPDWORD)Buffer <= TRUE, "Bad flag\n");
                break;
            case SERVICE_CONFIG_SERVICE_SID_INFO:
                ok(*(LPDWORD)Buffer <= SERVICE_SID_TYPE_RESTRICTED, "Bad SID type\n");
                break;
            case SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO:
            {
                LPWSTR p = ((LPSERVICE_REQUIRED_PRIVILEGES_INFOW)Buffer)->pmszRequiredPrivileges;
                ok(p == NULL || ((PBYTE)p >= Buffer && (PBYTE)p < Buffer + Needed), "Bad privileges pointer %p\n", p);
                break;
            }
            case SERVICE_CONFIG_PRESHUTDOWN_INFO:
                ok(((LPSERVICE_PRESHUTDOWN_INFO)Buffer)->dwPreshutdownTimeout != 0xcccccccc, "Timeout not set\n");
                break;
            case SERVICE_CONFIG_TRIGGER_INFO:
            {
                PSERVICE_TRIGGER_INFO Info = (PSERVICE_TRIGGER_INFO)Buffer;
                ok(Info->cTriggers == 0 || Info->pTriggers != NULL, "Bad triggers\n");
                ok(Info->pReserved == NULL, "pReserved %p\n", Info->pReserved);
                break;
            }
        }
    }

    /* Beyond the known levels */
    SetLastError(0xdeadbeef);
    Ret = QueryServiceConfig2W(hService, 0x1000, Buffer, sizeof(Buffer), &Needed);
    ok(!Ret && GetLastError() == ERROR_INVALID_LEVEL, "Level 0x1000: %d, error %lu\n", Ret, GetLastError());

    CloseServiceHandle(hService);
    CloseServiceHandle(hScm);
}

START_TEST(QueryServiceConfig2)
{
    Test_QueryServiceConfig2W_Levels();
    Test_QueryServiceConfig2W();
    Test_QueryServiceConfig2A();
}
