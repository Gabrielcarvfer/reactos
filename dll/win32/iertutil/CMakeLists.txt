
add_definitions(-D__WINESRC__)
spec2def(iertutil.dll iertutil.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/iertutil_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/iertutil.def)

add_library(iertutil MODULE ${SOURCE})
set_module_type(iertutil win32dll ENTRYPOINT 0)
target_link_libraries(iertutil uuid wine)
add_importlibs(iertutil urlmon combase oleaut32 msvcrt kernel32 ntdll)
add_dependencies(iertutil psdk)
add_cd_file(TARGET iertutil DESTINATION reactos/system32 FOR all)
