#define STANDALONE
#include <apitest.h>

extern void func_PdfInterop(void);

const struct test winetest_testlist[] =
{
    { "PdfInterop", func_PdfInterop },
    { 0, 0 }
};
