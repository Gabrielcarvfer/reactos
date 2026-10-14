
spec2def(mf.dll mf.spec)

add_library(mf MODULE
    mf.c
    ${CMAKE_CURRENT_BINARY_DIR}/mf.def)

set_module_type(mf win32dll ENTRYPOINT 0)
add_importlibs(mf kernel32 ntdll)
add_cd_file(TARGET mf DESTINATION reactos/system32 FOR all)
