
add_definitions(-D__WINESRC__ -D_WINE)
spec2def(webservices.dll webservices.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    channel.c
    error.c
    heap.c
    listener.c
    msg.c
    proxy.c
    reader.c
    string.c
    url.c
    writer.c
    ${CMAKE_CURRENT_BINARY_DIR}/webservices_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/webservices.def)

add_library(webservices MODULE ${SOURCE})
set_module_type(webservices win32dll ENTRYPOINT 0)
target_link_libraries(webservices wine)
add_importlibs(webservices winhttp rpcrt4 user32 ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET webservices DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(webservices) # CORE-5743: No CONST_VTABLE
