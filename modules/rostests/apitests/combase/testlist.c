/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test list for combase.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_UISettings(void);

const struct test winetest_testlist[] =
{
    { "UISettings", func_UISettings },

    { 0, 0 }
};
