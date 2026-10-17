/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Tests for LCMapString
 * COPYRIGHT:   Copyright 2024 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */

#include "precomp.h"

#undef ok_wstr_

static void
ok_wstr_(const char *file, int line, LPCWSTR x, LPCWSTR y)
{
    char buf1[100], buf2[100];
    lstrcpynA(buf1, wine_dbgstr_w(x), _countof(buf1));
    lstrcpynA(buf2, wine_dbgstr_w(y), _countof(buf2));
    ok_(file, line)(wcscmp(x, y) == 0, "Wrong string. Expected %s, got %s\n", buf2, buf1);
}

#undef ok_wstr
#define ok_wstr(x, y)     ok_wstr_(__FILE__, __LINE__, x, y)

// "ABab12ＡＢａｂ１２あアばバﾊﾟ万萬" in UTF-16
static const WCHAR c_target[] =
    L"ABab12\xff21\xff22\xff41\xff42\xff11\xff12\x3042\x30a2\x3070\x30d0\xff8a\xff9f\x4e07\x842c";

static void TEST_LCMapStringW(void)
{
    WCHAR results[100];

    LCMapStringW(0, LCMAP_FULLWIDTH, c_target, -1, results, _countof(results));
    ok_wstr(results, L"\xff21\xff22\xff41\xff42\xff11\xff12\xff21\xff22\xff41\xff42\xff11\xff12\x3042\x30a2\x3070\x30d0\x30d1\x4e07\x842c");

    LCMapStringW(0, LCMAP_HALFWIDTH, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABab12ABab12\x3042\xff71\x3070\xff8a\xff9e\xff8a\xff9f\x4e07\x842c");

    LCMapStringW(0, LCMAP_HIRAGANA, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABab12\xff21\xff22\xff41\xff42\xff11\xff12\x3042\x3042\x3070\x3070\xff8a\xff9f\x4e07\x842c");

    LCMapStringW(0, LCMAP_KATAKANA, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABab12\xff21\xff22\xff41\xff42\xff11\xff12\x30a2\x30a2\x30d0\x30d0\xff8a\xff9f\x4e07\x842c");

    LCMapStringW(0, LCMAP_LOWERCASE, c_target, -1, results, _countof(results));
    ok_wstr(results, L"abab12\xff41\xff42\xff41\xff42\xff11\xff12\x3042\x30a2\x3070\x30d0\xff8a\xff9f\x4e07\x842c");

    LCMapStringW(0, LCMAP_UPPERCASE, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABAB12\xff21\xff22\xff21\xff22\xff11\xff12\x3042\x30a2\x3070\x30d0\xff8a\xff9f\x4e07\x842c");

    LCMapStringW(0, LCMAP_SIMPLIFIED_CHINESE, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABab12\xff21\xff22\xff41\xff42\xff11\xff12\x3042\x30a2\x3070\x30d0\xff8a\xff9f\x4e07\x4e07");

    LCMapStringW(0, LCMAP_TRADITIONAL_CHINESE, c_target, -1, results, _countof(results));
    ok_wstr(results, L"ABab12\xff21\xff22\xff41\xff42\xff11\xff12\x3042\x30a2\x3070\x30d0\xff8a\xff9f\x842c\x842c");

    // Test broken reactos behavior
    LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, L"\x0104", -1, results, _countof(results));
    ok_wstr(results,L"\x0105");
}

/* The size of a sort key asked first (no buffer) is the size then written.
 * .NET's NLS mode hashes strings this way and throws if they differ. */
static void TEST_SortKeyLength(void)
{
    static const WCHAR *strings[] = { L"a", L"HandleDispatcherRequestProcessingFailure", L"Ab-1 c", L"\x00e9t\x00e9" };
    static const DWORD flags[] = { 0, NORM_IGNORECASE, NORM_IGNORECASE | 0x08000000 /* NORM_LINGUISTIC_CASING */,
                                   NORM_IGNORESYMBOLS, SORT_STRINGSORT };
    BYTE key[256];
    UINT i, j;
    INT len, written;

    for (i = 0; i < _countof(strings); i++)
    {
        for (j = 0; j < _countof(flags); j++)
        {
            len = LCMapStringEx(L"", LCMAP_SORTKEY | flags[j], strings[i], (INT)wcslen(strings[i]), NULL, 0, NULL, NULL, 0);
            ok(len > 0, "%u/%u: size %d, error %lu\n", i, j, len, GetLastError());
            memset(key, 0xcc, sizeof(key));
            written = LCMapStringEx(L"", LCMAP_SORTKEY | flags[j], strings[i], (INT)wcslen(strings[i]), (LPWSTR)key, len, NULL, NULL, 0);
            ok(written == len, "%u/%u: asked %d, wrote %d\n", i, j, len, written);
            ok(written > 0 && key[written - 1] == 0, "%u/%u: no terminating 0\n", i, j);
            ok(key[len] == 0xcc, "%u/%u: wrote past the size\n", i, j);

            len = LCMapStringW(LOCALE_INVARIANT, LCMAP_SORTKEY | flags[j], strings[i], -1, NULL, 0);
            written = LCMapStringW(LOCALE_INVARIANT, LCMAP_SORTKEY | flags[j], strings[i], -1, (LPWSTR)key, len);
            ok(written == len, "%u/%u: LCMapStringW asked %d, wrote %d\n", i, j, len, written);
        }
    }

    len = LCMapStringA(LOCALE_INVARIANT, LCMAP_SORTKEY, "Abc", -1, NULL, 0);
    written = LCMapStringA(LOCALE_INVARIANT, LCMAP_SORTKEY, "Abc", -1, (LPSTR)key, len);
    ok(len > 0 && written == len, "LCMapStringA asked %d, wrote %d\n", len, written);
}

START_TEST(LCMapString)
{
    TEST_LCMapStringW();
    TEST_SortKeyLength();
}
