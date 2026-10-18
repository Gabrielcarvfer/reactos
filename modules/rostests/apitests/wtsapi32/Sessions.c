/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test WTSEnumerateSessions, WTSQuerySessionInformation and WTSQueryUserToken
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <wtsapi32.h>

/*
 * WTSEnumerateSessions listed no sessions and WTSQuerySessionInformation
 * gave the caller's name, not the session user's: a service (AnyDesk) found
 * no user to start its desktop process for. The tests run in the logged-on
 * user's console session.
 */
START_TEST(Sessions)
{
    WTS_SESSION_INFOW *Sessions;
    DWORD Count = 0, i, Console = WTSGetActiveConsoleSessionId(), Size, Length;
    WCHAR *UserName, OwnName[256];
    BOOL Found = FALSE;
    HANDLE Token;

    ok(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Sessions, &Count),
       "WTSEnumerateSessionsW failed: %lu\n", GetLastError());
    ok(Count > 0, "No sessions\n");
    for (i = 0; i < Count; i++)
    {
        if (Sessions[i].SessionId != Console) continue;
        Found = TRUE;
        ok(Sessions[i].State == WTSActive, "Console session state %d\n", Sessions[i].State);
        ok(Sessions[i].pWinStationName && !wcscmp(Sessions[i].pWinStationName, L"Console"),
           "Console session name %S\n", Sessions[i].pWinStationName);
    }
    ok(Found, "The console session %lu is not listed\n", Console);
    WTSFreeMemory(Sessions);

    /* The console's user is the user running the tests */
    Length = ARRAYSIZE(OwnName);
    GetUserNameW(OwnName, &Length);
    UserName = NULL;
    ok(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, Console, WTSUserName, &UserName, &Size),
       "WTSQuerySessionInformationW(WTSUserName) failed: %lu\n", GetLastError());
    ok(UserName && !_wcsicmp(UserName, OwnName), "User name %S, expected %S\n", UserName, OwnName);
    WTSFreeMemory(UserName);

    /* Only LocalSystem (SeTcbPrivilege) gets a user's token */
    Token = NULL;
    SetLastError(0xdeadbeef);
    ok(!WTSQueryUserToken(Console, &Token), "WTSQueryUserToken succeeded\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "WTSQueryUserToken error %lu\n", GetLastError());
}
