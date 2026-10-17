
add_definitions(-D__WINESRC__)
spec2def(prntvpt.dll prntvpt.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    ticket.c
    ${CMAKE_CURRENT_BINARY_DIR}/prntvpt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/prntvpt.def)

add_library(prntvpt MODULE ${SOURCE})
set_module_type(prntvpt win32dll ENTRYPOINT 0)
target_link_libraries(prntvpt uuid wine wine_dll_canunload)
add_importlibs(prntvpt winspool ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET prntvpt DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(prntvpt) # CORE-5743: No CONST_VTABLE
