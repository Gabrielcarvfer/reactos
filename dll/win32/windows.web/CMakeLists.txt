
add_definitions(-D__WINESRC__)
spec2def(windows.web.dll windows.web.spec)

list(APPEND SOURCE
    json_array.c
    json_collections.c
    json_object.c
    json_value.c
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.web_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.web.def)

add_library(windows.web MODULE ${SOURCE})
set_module_type(windows.web win32dll ENTRYPOINT 0)
target_link_libraries(windows.web uuid wine)
add_importlibs(windows.web combase advapi32 msvcrt kernel32 ntdll)
add_dependencies(windows.web psdk)
add_cd_file(TARGET windows.web DESTINATION reactos/system32 FOR all)
