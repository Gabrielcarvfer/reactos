
add_definitions(-D__WINESRC__)
spec2def(windows.data.pdf.dll windows.data.pdf.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.data.pdf_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/windows.data.pdf.def)

add_library(windows.data.pdf MODULE ${SOURCE})
set_module_type(windows.data.pdf win32dll ENTRYPOINT 0)
target_link_libraries(windows.data.pdf wine)
add_importlibs(windows.data.pdf combase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows.data.pdf DESTINATION reactos/system32 FOR all)
