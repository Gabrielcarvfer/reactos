/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for GetDynamicTimeZoneInformation (Vista+)
 */

#include "precomp.h"
#include <timezoneapi.h>

typedef DWORD (WINAPI *PGETDTZI)(PDYNAMIC_TIME_ZONE_INFORMATION);

START_TEST(GetDynamicTimeZoneInformation)
{
    PGETDTZI pGetDtzi = (PGETDTZI)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetDynamicTimeZoneInformation");
    DYNAMIC_TIME_ZONE_INFORMATION Dynamic;
    TIME_ZONE_INFORMATION Classic;
    DWORD Ret, Ret2;

    if (!pGetDtzi)
    {
        skip("GetDynamicTimeZoneInformation is not exported (Vista+)\n");
        return;
    }

    memset(&Dynamic, 0xcc, sizeof(Dynamic));
    Ret = pGetDtzi(&Dynamic);
    Ret2 = GetTimeZoneInformation(&Classic);
    ok(Ret != TIME_ZONE_ID_INVALID, "Failed: %lu\n", GetLastError());
    ok(Ret == Ret2, "Returned %lu, GetTimeZoneInformation %lu\n", Ret, Ret2);
    ok(Dynamic.Bias == Classic.Bias, "Bias %ld, expected %ld\n", Dynamic.Bias, Classic.Bias);
    ok(!wcscmp(Dynamic.StandardName, Classic.StandardName), "StandardName %S, expected %S\n",
       Dynamic.StandardName, Classic.StandardName);
    ok(Dynamic.TimeZoneKeyName[0] != UNICODE_NULL, "No time zone key name\n");
    ok(wcslen(Dynamic.TimeZoneKeyName) < ARRAYSIZE(Dynamic.TimeZoneKeyName), "Key name not terminated\n");
}
