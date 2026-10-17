
spec2def(tdh.dll tdh.spec)

list(APPEND SOURCE
    tdh_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/tdh_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/tdh.def)

add_library(tdh MODULE ${SOURCE})
set_module_type(tdh win32dll ENTRYPOINT 0)
target_link_libraries(tdh wine)
add_importlibs(tdh kernel32 ntdll)
add_cd_file(TARGET tdh DESTINATION reactos/system32 FOR all)
set_wine_module(tdh)
