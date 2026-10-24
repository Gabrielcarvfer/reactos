#define STANDALONE
#include <apitest.h>

extern void func_DeclareAdapterRemovalSupport(void);
extern void func_EnumWarpAdapter(void);

const struct test winetest_testlist[] =
{
    { "DeclareAdapterRemovalSupport", func_DeclareAdapterRemovalSupport },
    { "EnumWarpAdapter", func_EnumWarpAdapter },
    { 0, 0 }
};
