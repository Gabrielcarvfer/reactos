
# wow64cpu.dll switches a thread of a 32-bit program between 32-bit and
# 64-bit code (comes from Wine)

add_definitions(-D__WINESRC__ -D_WINE)
spec2def(wow64cpu.dll wow64cpu.spec)

add_library(wow64cpu MODULE
    cpu.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64cpu_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64cpu.def)

set_module_type(wow64cpu win32dll ENTRYPOINT DllMain 12)
set_wine_module(wow64cpu)
target_link_libraries(wow64cpu pseh)
add_importlibs(wow64cpu wow64 ntdll)
add_cd_file(TARGET wow64cpu DESTINATION reactos/system32 FOR all)
