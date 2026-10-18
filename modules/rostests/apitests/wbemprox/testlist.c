#define STANDALONE
#include <apitest.h>

extern void func_NetworkAdapterConfig(void);

const struct test winetest_testlist[] =
{
    { "NetworkAdapterConfig", func_NetworkAdapterConfig },

    { 0, 0 }
};
