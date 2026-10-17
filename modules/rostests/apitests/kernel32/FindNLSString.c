/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for FindNLSString(Ex)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira
 */

#include "precomp.h"

#ifndef FIND_STARTSWITH
#define FIND_STARTSWITH 0x00100000
#define FIND_ENDSWITH   0x00200000
#define FIND_FROMSTART  0x00400000
#define FIND_FROMEND    0x00800000
#endif
#ifndef NORM_LINGUISTIC_CASING
#define NORM_LINGUISTIC_CASING 0x08000000
#endif

typedef INT (WINAPI *FN_FindNLSStringEx)(LPCWSTR, DWORD, LPCWSTR, INT, LPCWSTR, INT,
                                         LPINT, PVOID, LPVOID, LPARAM);
typedef INT (WINAPI *FN_FindNLSString)(LCID, DWORD, LPCWSTR, INT, LPCWSTR, INT, LPINT);

/* .NET's CompareInfo (IndexOf, StartsWith, EndsWith) calls FindNLSStringEx when
 * it runs without ICU; HandBrake died on the old stub */
START_TEST(FindNLSString)
{
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FN_FindNLSStringEx pFindNLSStringEx;
    FN_FindNLSString pFindNLSString;
    INT Ret, Found;

    pFindNLSStringEx = (FN_FindNLSStringEx)GetProcAddress(hKernel32, "FindNLSStringEx");
    pFindNLSString = (FN_FindNLSString)GetProcAddress(hKernel32, "FindNLSString");
    if (!pFindNLSStringEx || !pFindNLSString)
    {
        skip("FindNLSString(Ex) not available\n");
        return;
    }

    Found = -2;
    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART, L"Hello World", -1, L"World", -1, &Found, NULL, NULL, 0);
    ok(Ret == 6 && Found == 5, "FROMSTART: %d %d\n", Ret, Found);

    Found = -2;
    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART | NORM_IGNORECASE, L"Hello World", 11, L"wORLD", 5, &Found, NULL, NULL, 0);
    ok(Ret == 6 && Found == 5, "IGNORECASE: %d %d\n", Ret, Found);

    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART, L"Hello World", -1, L"wORLD", -1, NULL, NULL, NULL, 0);
    ok(Ret == -1, "case sensitive: %d\n", Ret);

    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART | NORM_LINGUISTIC_CASING, L"Hello World", -1, L"o", -1, NULL, NULL, NULL, 0);
    ok(Ret == 4, "LINGUISTIC_CASING: %d\n", Ret);

    Ret = pFindNLSStringEx(L"en-US", FIND_FROMEND, L"abcabc", -1, L"abc", -1, NULL, NULL, NULL, 0);
    ok(Ret == 3, "FROMEND: %d\n", Ret);

    Ret = pFindNLSStringEx(L"en-US", FIND_STARTSWITH, L"Hello World", -1, L"Hello", -1, NULL, NULL, NULL, 0);
    ok(Ret == 0, "STARTSWITH: %d\n", Ret);
    Ret = pFindNLSStringEx(L"en-US", FIND_STARTSWITH, L"Hello World", -1, L"World", -1, NULL, NULL, NULL, 0);
    ok(Ret == -1, "STARTSWITH no match: %d\n", Ret);

    Ret = pFindNLSStringEx(L"en-US", FIND_ENDSWITH, L"Hello World", -1, L"World", -1, NULL, NULL, NULL, 0);
    ok(Ret == 6, "ENDSWITH: %d\n", Ret);
    Ret = pFindNLSStringEx(L"en-US", FIND_ENDSWITH, L"Hello World", -1, L"Hello", -1, NULL, NULL, NULL, 0);
    ok(Ret == -1, "ENDSWITH no match: %d\n", Ret);

    /* The invariant locale (LOCALE_NAME_INVARIANT) */
    Ret = pFindNLSStringEx(L"", FIND_FROMSTART, L"Hello World", -1, L"World", -1, NULL, NULL, NULL, 0);
    ok(Ret == 6, "invariant: %d\n", Ret);

    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART, L"Hi", -1, L"Hello", -1, NULL, NULL, NULL, 0);
    ok(Ret == -1, "longer value: %d\n", Ret);

    SetLastError(0xdeadbeef);
    Ret = pFindNLSStringEx(L"en-US", FIND_FROMSTART, L"Hello", 0, L"H", -1, NULL, NULL, NULL, 0);
    ok(Ret == -1 && GetLastError() == ERROR_INVALID_PARAMETER, "empty source: %d %lu\n", Ret, GetLastError());

    Found = -2;
    Ret = pFindNLSString(LOCALE_USER_DEFAULT, FIND_FROMSTART, L"Hello World", -1, L"World", -1, &Found);
    ok(Ret == 6 && Found == 5, "FindNLSString: %d %d\n", Ret, Found);
}
