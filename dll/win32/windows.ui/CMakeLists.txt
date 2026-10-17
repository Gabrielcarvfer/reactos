
spec2def(windows.ui.dll windows.ui.spec)

list(APPEND SOURCE
    composition.c
    uisettings.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.ui_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.ui.def)

add_library(windows.ui MODULE ${SOURCE})
set_module_type(windows.ui win32dll ENTRYPOINT 0)
target_link_libraries(windows.ui uuid wine)
add_importlibs(windows.ui combase advapi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET windows.ui DESTINATION reactos/system32 FOR all)
