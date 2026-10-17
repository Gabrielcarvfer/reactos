
spec2def(cfgmgr32.dll cfgmgr32.spec)

list(APPEND SOURCE
    devquery.c
    cfgmgr32.rc
    ${CMAKE_CURRENT_BINARY_DIR}/cfgmgr32_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/cfgmgr32.def)

add_library(cfgmgr32 MODULE ${SOURCE})

set_module_type(cfgmgr32 win32dll)
target_link_libraries(cfgmgr32 wine)
add_importlibs(cfgmgr32 setupapi msvcrt kernel32 ntdll)
add_cd_file(TARGET cfgmgr32 DESTINATION reactos/system32 FOR all)
