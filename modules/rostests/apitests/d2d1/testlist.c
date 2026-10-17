#define STANDALONE
#include <apitest.h>

extern void func_CustomEffect(void);

const struct test winetest_testlist[] =
{
    { "CustomEffect", func_CustomEffect },
    { 0, 0 }
};
