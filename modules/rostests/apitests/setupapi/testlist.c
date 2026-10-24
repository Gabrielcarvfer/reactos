
#define STANDALONE
#include <apitest.h>

extern void func_CM_MapCrToWin32Err(void);
extern void func_devclass(void);
extern void func_DevQuery(void);
extern void func_PciIrqShare(void);
extern void func_SetupDiGetActualModelsSection(void);
extern void func_SetupDiInstallClassExA(void);
extern void func_SetupInstallServicesFromInfSectionEx(void);

const struct test winetest_testlist[] =
{
    { "CM_MapCrToWin32Err", func_CM_MapCrToWin32Err },
    { "devclass", func_devclass },
    { "DevQuery", func_DevQuery },
    { "PciIrqShare", func_PciIrqShare },
    { "SetupDiGetActualModelsSection", func_SetupDiGetActualModelsSection },
    { "SetupDiInstallClassExA", func_SetupDiInstallClassExA },
    { "SetupInstallServicesFromInfSectionEx", func_SetupInstallServicesFromInfSectionEx },
    { 0, 0 }
};
