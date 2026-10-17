
#define STANDALONE
#include <wine/test.h>

extern void func_initializespy(void);
extern void func_LocalService(void);

const struct test winetest_testlist[] =
{
    { "initializespy", func_initializespy },
    { "LocalService", func_LocalService },

    { 0, 0 }
};
