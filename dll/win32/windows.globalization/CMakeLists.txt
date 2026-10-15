
remove_definitions(-D_WIN32_WINNT=0x502 -DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)
add_definitions(-D__WINESRC__)
spec2def(windows.globalization.dll windows.globalization.spec)

list(APPEND SOURCE
    geographic_region.c
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.globalization_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.globalization.def)

add_library(windows.globalization MODULE ${SOURCE})
set_module_type(windows.globalization win32dll ENTRYPOINT 0)
target_link_libraries(windows.globalization uuid wine)
add_importlibs(windows.globalization combase advapi32 msvcrt kernel32 ntdll)
add_dependencies(windows.globalization psdk)
add_cd_file(TARGET windows.globalization DESTINATION reactos/system32 FOR all)
