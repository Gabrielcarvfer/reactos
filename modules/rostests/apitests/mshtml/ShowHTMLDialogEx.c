/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for the export of ShowHTMLDialogEx
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * NVDA 2026 binds mshtml's ShowHTMLDialogEx when it starts (winBindings.mshtml,
 * imported by its ui module) and stops when it's missing.
 */

#include <apitest.h>

START_TEST(ShowHTMLDialogEx)
{
    HMODULE Mshtml = LoadLibraryW(L"mshtml.dll");

    ok(Mshtml != NULL, "LoadLibrary(mshtml) failed: %lu\n", GetLastError());
    if (!Mshtml)
        return;
    ok(GetProcAddress(Mshtml, "ShowHTMLDialogEx") != NULL, "ShowHTMLDialogEx is missing\n");
    ok(GetProcAddress(Mshtml, "ShowHTMLDialog") != NULL, "ShowHTMLDialog is missing\n");
    FreeLibrary(Mshtml);
}
