#define STANDALONE
#include <apitest.h>

extern void func_UIAnimation(void);

const struct test winetest_testlist[] =
{
    { "UIAnimation", func_UIAnimation },
    { 0, 0 }
};
