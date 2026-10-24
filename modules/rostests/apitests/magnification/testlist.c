#define STANDALONE
#include <apitest.h>

extern void func_magnification(void);

const struct test winetest_testlist[] =
{
    { "magnification", func_magnification },
    { 0, 0 }
};
