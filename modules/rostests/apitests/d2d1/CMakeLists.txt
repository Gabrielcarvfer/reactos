
add_executable(d2d1_apitest CustomEffect.c testlist.c)
target_link_libraries(d2d1_apitest wine uuid dxguid)
add_dependencies(d2d1_apitest d3d_idl_headers psdk)
set_module_type(d2d1_apitest win32cui)
add_importlibs(d2d1_apitest d2d1 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET d2d1_apitest)
