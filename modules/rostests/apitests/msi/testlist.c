#define STANDALONE
#include <apitest.h>

extern void func_CustomAction(void);
extern void func_UILevel(void);

const struct test winetest_testlist[] =
{
    { "CustomAction", func_CustomAction },
    { "UILevel", func_UILevel },
    { 0, 0 }
};
