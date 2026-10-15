
spec2def(icuuc.dll icuuc.spec)

add_library(icuuc MODULE
    icuuc.c
    ${CMAKE_CURRENT_BINARY_DIR}/icuuc.def)

set_module_type(icuuc win32dll ENTRYPOINT 0)
add_importlibs(icuuc msvcrt kernel32 ntdll)
add_cd_file(TARGET icuuc DESTINATION reactos/system32 FOR all)
