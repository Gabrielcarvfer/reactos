/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test list for combase.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_JsonStringify(void);
extern void func_UISettings(void);
extern void func_UriRuntimeClass(void);

const struct test winetest_testlist[] =
{
    { "JsonStringify", func_JsonStringify },
    { "UISettings", func_UISettings },
    { "UriRuntimeClass", func_UriRuntimeClass },

    { 0, 0 }
};
