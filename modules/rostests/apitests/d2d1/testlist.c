#define STANDALONE
#include <apitest.h>

extern void func_ColorContext(void);
extern void func_CustomEffect(void);
extern void func_DrawState(void);
extern void func_EffectGraph(void);

const struct test winetest_testlist[] =
{
    { "ColorContext", func_ColorContext },
    { "CustomEffect", func_CustomEffect },
    { "DrawState", func_DrawState },
    { "EffectGraph", func_EffectGraph },
    { 0, 0 }
};
