
add_definitions(-DUSE_WINE_TODOS -D__WINESRC__ -DWINETEST_USE_DBGSTR_LONGLONG)
list(APPEND SOURCE
    channel.c
    msg.c
    proxy.c
    reader.c
    url.c
    writer.c
    testlist.c)

add_executable(webservices_winetest ${SOURCE})
set_module_type(webservices_winetest win32cui)
add_importlibs(webservices_winetest webservices rpcrt4 oleaut32 ole32 advapi32 user32 ws2_32 msvcrt kernel32)
add_rostests_file(TARGET webservices_winetest)
