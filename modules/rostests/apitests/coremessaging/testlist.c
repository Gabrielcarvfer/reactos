/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Test list for coremessaging.dll
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_DispatcherQueue(void);

const struct test winetest_testlist[] =
{
    { "DispatcherQueue", func_DispatcherQueue },

    { 0, 0 }
};
