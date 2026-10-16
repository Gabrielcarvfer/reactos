
add_executable(icuuc_apitest Converters.c testlist.c)
target_link_libraries(icuuc_apitest wine)
set_module_type(icuuc_apitest win32cui)
add_importlibs(icuuc_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET icuuc_apitest)
