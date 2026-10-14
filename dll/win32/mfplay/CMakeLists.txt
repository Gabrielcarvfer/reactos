
spec2def(mfplay.dll mfplay.spec)

add_library(mfplay MODULE
    mfplay.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfplay.def)

set_module_type(mfplay win32dll ENTRYPOINT 0)
add_importlibs(mfplay kernel32 ntdll)
add_cd_file(TARGET mfplay DESTINATION reactos/system32 FOR all)
