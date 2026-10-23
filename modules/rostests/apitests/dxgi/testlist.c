#define STANDALONE
#include <apitest.h>

extern void func_DeclareAdapterRemovalSupport(void);

const struct test winetest_testlist[] =
{
    { "DeclareAdapterRemovalSupport", func_DeclareAdapterRemovalSupport },
    { 0, 0 }
};
