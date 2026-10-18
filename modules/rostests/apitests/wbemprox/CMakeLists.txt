
list(APPEND SOURCE
    NetworkAdapterConfig.c
    testlist.c)

add_executable(wbemprox_apitest ${SOURCE})
target_link_libraries(wbemprox_apitest wine uuid)
set_module_type(wbemprox_apitest win32cui)
add_importlibs(wbemprox_apitest oleaut32 ole32 msvcrt kernel32)
add_rostests_file(TARGET wbemprox_apitest)
