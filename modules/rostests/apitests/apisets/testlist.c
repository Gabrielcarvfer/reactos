
#define STANDALONE
#include <apitest.h>

extern void func_apisets(void);
extern void func_featurestaging(void);

const struct test winetest_testlist[] =
{
    { "apisets", func_apisets },
    { "featurestaging", func_featurestaging },
    { 0, 0 }
};
