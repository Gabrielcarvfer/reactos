
#define STANDALONE
#include <apitest.h>

extern void func_ConnectionPool(void);
extern void func_WinHttpOpen(void);

const struct test winetest_testlist[] =
{
    { "ConnectionPool", func_ConnectionPool },
    { "WinHttpOpen", func_WinHttpOpen },

    { 0, 0 }
};
