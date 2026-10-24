
add_executable(icacls_apitest icacls.c testlist.c)
set_module_type(icacls_apitest win32cui)
add_importlibs(icacls_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET icacls_apitest)
