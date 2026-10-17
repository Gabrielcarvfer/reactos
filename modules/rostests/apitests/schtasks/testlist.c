#define STANDALONE
#include <apitest.h>

extern void func_CreateTr(void);

const struct test winetest_testlist[] =
{
    { "CreateTr", func_CreateTr },
    { 0, 0 }
};
