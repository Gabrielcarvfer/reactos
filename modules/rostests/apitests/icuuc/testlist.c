/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test list for icuuc.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_Converters(void);

const struct test winetest_testlist[] =
{
    { "Converters", func_Converters },

    { 0, 0 }
};
