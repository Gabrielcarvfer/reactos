
add_definitions(-D__WINESRC__)
spec2def(bcp47langs.dll bcp47langs.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcp47langs_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcp47langs.def)

add_library(bcp47langs MODULE ${SOURCE})
set_module_type(bcp47langs win32dll ENTRYPOINT 0)
target_link_libraries(bcp47langs wine)
add_importlibs(bcp47langs combase msvcrt kernel32 ntdll)
add_cd_file(TARGET bcp47langs DESTINATION reactos/system32 FOR all)
