
#define STANDALONE
#include <apitest.h>

extern void func_GetProfileDirs(void);
extern void func_LoadUserProfile(void);
extern void func_profapi(void);

const struct test winetest_testlist[] =
{
    { "GetProfileDirs", func_GetProfileDirs },
    { "LoadUserProfile", func_LoadUserProfile },
    { "profapi", func_profapi },
    { 0, 0 }
};
