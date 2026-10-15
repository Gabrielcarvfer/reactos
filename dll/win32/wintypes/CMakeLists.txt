
remove_definitions(-D_WIN32_WINNT=0x502 -DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)
add_definitions(-D__WINESRC__)
spec2def(wintypes.dll wintypes.spec ADD_IMPORTLIB)
add_idl_headers(wintypes_idlheader wintypes_private.idl)

list(APPEND SOURCE
    buffer.c
    main.c
    map.c
    propertyset.c
    storage.c
    ${CMAKE_CURRENT_BINARY_DIR}/wintypes_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wintypes.def)

add_library(wintypes MODULE ${SOURCE})
set_module_type(wintypes win32dll ENTRYPOINT 0)
target_link_libraries(wintypes uuid wine)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(wintypes PRIVATE -Wno-old-style-declaration)
endif()
add_importlibs(wintypes combase kernelbase advapi32 msvcrt kernel32 ntdll)
add_dependencies(wintypes psdk wintypes_idlheader)
add_cd_file(TARGET wintypes DESTINATION reactos/system32 FOR all)
