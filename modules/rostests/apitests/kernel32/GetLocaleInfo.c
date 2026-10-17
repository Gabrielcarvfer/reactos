/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Test for GetLocaleInfo(Ex)
 * PROGRAMMER:      Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include "precomp.h"

typedef
int
WINAPI
FN_GetLocaleInfoEx(
    _In_opt_ LPCWSTR lpLocaleName,
    _In_ LCTYPE  LCType,
    _Out_opt_ LPWSTR lpLCData,
    _In_ int cchData);

FN_GetLocaleInfoEx* pGetLocaleInfoEx = NULL;

static void Test_GetLocaleInfoEx(void)
{
    HMODULE hmodKernel32;
    int Ret;
    ULONG CodePage;

    hmodKernel32 = GetModuleHandleW(L"kernel32.dll");
    pGetLocaleInfoEx = (FN_GetLocaleInfoEx*)GetProcAddress(hmodKernel32, "GetLocaleInfoEx");
    if (pGetLocaleInfoEx == NULL)
    {
        hmodKernel32 = LoadLibraryW(L"kernel32_vista.dll");
        pGetLocaleInfoEx = (FN_GetLocaleInfoEx*)GetProcAddress(hmodKernel32, "GetLocaleInfoEx");
        if (pGetLocaleInfoEx == NULL)
        {
            skip("GetLocaleInfoEx not found in kernel32.dll\n");
            return;
        }
    }

    // Test normal usage
    Ret = pGetLocaleInfoEx(L"en-US",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR*)&CodePage,
                           sizeof(DWORD) / sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with neutral locale
    Ret = pGetLocaleInfoEx(L"en",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           NULL,
                           0);
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with NULL locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(NULL,
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with empty locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(L"",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 2);
    ok_eq_long(CodePage, 1252ul);

    // Test with invalid locale name
    CodePage = 0xdeadbeef;
    Ret = pGetLocaleInfoEx(L"invalid",
                           LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           (WCHAR *)&CodePage,
                           sizeof(DWORD) /sizeof(WCHAR));
    ok_eq_int(Ret, 0);
    ok_eq_long(GetLastError(), (ULONG)ERROR_INVALID_PARAMETER);
    ok(CodePage == 0xdeadbeef, "CodePage should not have been modified: %lx\n", CodePage);

}

#ifndef LOCALE_SPARENT
#define LOCALE_SPARENT 0x0000006d
#endif
#ifndef LOCALE_NAME_MAX_LENGTH
#define LOCALE_NAME_MAX_LENGTH 85
#endif

/* A neutral locale ("en") has the invariant locale as its parent and its own
 * name, whatever the flags: .NET asks with LOCALE_NOUSEROVERRIDE, and walks
 * the parents until the invariant culture */
static void Test_NeutralParent(void)
{
    static const DWORD flags[] = { 0, LOCALE_NOUSEROVERRIDE };
    WCHAR Buffer[LOCALE_NAME_MAX_LENGTH];
    int Ret;
    UINT i;

    if (!pGetLocaleInfoEx)
        return;
    for (i = 0; i < _countof(flags); i++)
    {
        memset(Buffer, 'x', sizeof(Buffer));
        Ret = pGetLocaleInfoEx(L"en", LOCALE_SPARENT | flags[i], Buffer, _countof(Buffer));
        ok(Ret == 1 && Buffer[0] == UNICODE_NULL, "%u: parent of en: %d '%.10ls'\n", i, Ret, Buffer);
        Ret = pGetLocaleInfoEx(L"en", LOCALE_SNAME | flags[i], Buffer, _countof(Buffer));
        ok(Ret == 3 && !wcscmp(Buffer, L"en"), "%u: name of en: %d '%.10ls'\n", i, Ret, Buffer);
        Ret = pGetLocaleInfoEx(L"en-US", LOCALE_SPARENT | flags[i], Buffer, _countof(Buffer));
        ok(Ret == 3 && !wcscmp(Buffer, L"en"), "%u: parent of en-US: %d '%.10ls'\n", i, Ret, Buffer);
    }
}

#undef GetLocaleInfo
START_TEST(GetLocaleInfo)
{
    Test_GetLocaleInfoEx();
    Test_NeutralParent();
}
