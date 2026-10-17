
add_definitions(-D__WINESRC__)
spec2def(uianimation.dll uianimation.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/uianimation_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/uianimation.def)

add_library(uianimation MODULE ${SOURCE})
set_module_type(uianimation win32dll)
target_link_libraries(uianimation uuid wine)
add_importlibs(uianimation ole32 advapi32 user32 msvcrt kernel32 ntdll)
add_dependencies(uianimation psdk)
add_cd_file(TARGET uianimation DESTINATION reactos/system32 FOR all)
