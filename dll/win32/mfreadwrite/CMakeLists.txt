
spec2def(mfreadwrite.dll mfreadwrite.spec)

add_library(mfreadwrite MODULE
    mfreadwrite.c
    ${CMAKE_CURRENT_BINARY_DIR}/mfreadwrite.def)

set_module_type(mfreadwrite win32dll ENTRYPOINT 0)
add_importlibs(mfreadwrite kernel32 ntdll)
add_cd_file(TARGET mfreadwrite DESTINATION reactos/system32 FOR all)
