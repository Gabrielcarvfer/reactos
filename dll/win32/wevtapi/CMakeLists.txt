
spec2def(wevtapi.dll wevtapi.spec)

add_library(wevtapi MODULE
    wevtapi.c
    ${CMAKE_CURRENT_BINARY_DIR}/wevtapi.def)

set_module_type(wevtapi win32dll ENTRYPOINT 0)
add_importlibs(wevtapi kernel32 ntdll)
add_cd_file(TARGET wevtapi DESTINATION reactos/system32 FOR all)
