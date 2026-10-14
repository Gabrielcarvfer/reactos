/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for FindFirstFileExW flags
 *
 * Chromium lists directories with FindExInfoBasic and FIND_FIRST_EX_LARGE_FETCH
 * (Windows 7); rejecting the flag broke its databases (IndexedDB...).
 */

#include "precomp.h"

#ifndef FIND_FIRST_EX_LARGE_FETCH
#define FIND_FIRST_EX_LARGE_FETCH 0x00000002
#endif
#ifndef FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY
#define FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY 0x00000004
#endif

static void
Test_Flags(DWORD Flags, BOOL Expected)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    GetWindowsDirectoryW(Pattern, ARRAYSIZE(Pattern));
    wcscat(Pattern, L"\\*");
    Find = FindFirstFileExW(Pattern, FindExInfoBasic, &Data, FindExSearchNameMatch, NULL, Flags);
    if (Expected)
    {
        ok(Find != INVALID_HANDLE_VALUE, "Flags 0x%lx: failed with %lu\n", Flags, GetLastError());
        if (Find != INVALID_HANDLE_VALUE)
        {
            ok(Data.cFileName[0] != UNICODE_NULL, "Flags 0x%lx: no file name\n", Flags);
            FindClose(Find);
        }
    }
    else
    {
        ok(Find == INVALID_HANDLE_VALUE, "Flags 0x%lx: succeeded\n", Flags);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Flags 0x%lx: error %lu\n", Flags, GetLastError());
        if (Find != INVALID_HANDLE_VALUE)
            FindClose(Find);
    }
}

START_TEST(FindFirstFileEx)
{
    Test_Flags(0, TRUE);
    Test_Flags(FIND_FIRST_EX_LARGE_FETCH, TRUE);
    Test_Flags(FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY, TRUE);
    Test_Flags(FIND_FIRST_EX_LARGE_FETCH | FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY, TRUE);
    Test_Flags(0x80000000, FALSE);
}
