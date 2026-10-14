
spec2def(sas.dll sas.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    sas.c
    ${CMAKE_CURRENT_BINARY_DIR}/sas_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/sas.def)

add_library(sas MODULE ${SOURCE})
set_module_type(sas win32dll)
add_importlibs(sas kernel32 ntdll)
add_cd_file(TARGET sas DESTINATION reactos/system32 FOR all)
