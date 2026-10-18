#define STANDALONE
#include <apitest.h>

extern void func_Sessions(void);

const struct test winetest_testlist[] =
{
    { "Sessions", func_Sessions },

    { 0, 0 }
};
