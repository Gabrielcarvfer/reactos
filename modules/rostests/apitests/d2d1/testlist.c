#define STANDALONE
#include <apitest.h>

extern void func_CustomEffect(void);
extern void func_DrawState(void);

const struct test winetest_testlist[] =
{
    { "CustomEffect", func_CustomEffect },
    { "DrawState", func_DrawState },
    { 0, 0 }
};
