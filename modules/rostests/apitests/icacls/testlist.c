#define STANDALONE
#include <apitest.h>

extern void func_icacls(void);

const struct test winetest_testlist[] =
{
    { "icacls", func_icacls },
    { 0, 0 }
};
