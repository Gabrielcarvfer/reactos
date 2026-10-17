
add_definitions(-D__WINESRC__)
spec2def(rometadata.dll rometadata.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    assembly.c
    main.c
    mdtables.c
    ${CMAKE_CURRENT_BINARY_DIR}/rometadata_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/rometadata.def)

add_library(rometadata MODULE ${SOURCE})
set_module_type(rometadata win32dll)
target_link_libraries(rometadata uuid wine)
add_importlibs(rometadata combase msvcrt kernel32 ntdll)
add_dependencies(rometadata psdk)
add_cd_file(TARGET rometadata DESTINATION reactos/system32 FOR all)
