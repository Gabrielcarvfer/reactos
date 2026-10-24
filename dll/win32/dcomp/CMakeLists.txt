
add_definitions(-D__WINESRC__)
spec2def(dcomp.dll dcomp.spec)

list(APPEND SOURCE
    device.c
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp.def)

add_library(dcomp MODULE ${SOURCE})
set_module_type(dcomp win32dll ENTRYPOINT 0)
target_link_libraries(dcomp wine wine_dll_canunload)
add_importlibs(dcomp msvcrt kernel32 ntdll)
add_cd_file(TARGET dcomp DESTINATION reactos/system32 FOR all)
