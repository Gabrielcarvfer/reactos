#define STANDALONE
#include <apitest.h>

extern void func_CustomAction(void);

const struct test winetest_testlist[] =
{
    { "CustomAction", func_CustomAction },
    { 0, 0 }
};
