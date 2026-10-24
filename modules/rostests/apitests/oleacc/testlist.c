#define STANDALONE
#include <apitest.h>

extern void func_UtilityState(void);

const struct test winetest_testlist[] =
{
    { "UtilityState", func_UtilityState },
    { 0, 0 }
};
