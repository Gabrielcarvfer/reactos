#define STANDALONE
#include <apitest.h>

extern void func_ShowHTMLDialogEx(void);

const struct test winetest_testlist[] =
{
    { "ShowHTMLDialogEx", func_ShowHTMLDialogEx },
    { 0, 0 }
};
