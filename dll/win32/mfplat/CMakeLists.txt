
spec2def(mfplat.dll mfplat.spec)

add_library(mfplat MODULE
    mfplat.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfplat.def)

set_module_type(mfplat win32dll ENTRYPOINT 0)
add_importlibs(mfplat kernel32 ntdll)
add_cd_file(TARGET mfplat DESTINATION reactos/system32 FOR all)
