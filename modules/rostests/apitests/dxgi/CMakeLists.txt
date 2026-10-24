
add_executable(dxgi_apitest DeclareAdapterRemovalSupport.c EnumWarpAdapter.c testlist.c)
target_link_libraries(dxgi_apitest uuid dxguid)
set_module_type(dxgi_apitest win32cui)
add_importlibs(dxgi_apitest dxgi msvcrt kernel32)
add_dependencies(dxgi_apitest d3d_idl_headers)
add_rostests_file(TARGET dxgi_apitest)
