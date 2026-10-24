
add_executable(oleacc_apitest UtilityState.c testlist.c)
target_link_libraries(oleacc_apitest uuid)
set_module_type(oleacc_apitest win32cui)
add_importlibs(oleacc_apitest oleacc ole32 user32 msvcrt kernel32)
add_rostests_file(TARGET oleacc_apitest)
