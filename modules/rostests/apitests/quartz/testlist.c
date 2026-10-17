/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test list for quartz.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_Parsers(void);

const struct test winetest_testlist[] =
{
    { "Parsers", func_Parsers },

    { 0, 0 }
};
