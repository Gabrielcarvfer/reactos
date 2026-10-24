
spec2def(profapi.dll profapi.spec)

list(APPEND SOURCE
    profapi.c
    ${CMAKE_CURRENT_BINARY_DIR}/profapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/profapi.def)

add_library(profapi MODULE ${SOURCE})
set_module_type(profapi win32dll ENTRYPOINT 0)
target_link_libraries(profapi wine)
add_importlibs(profapi kernel32 ntdll)
add_cd_file(TARGET profapi DESTINATION reactos/system32 FOR all)
