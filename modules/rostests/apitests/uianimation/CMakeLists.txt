
add_executable(uianimation_apitest UIAnimation.c testlist.c)
target_link_libraries(uianimation_apitest wine uuid)
set_module_type(uianimation_apitest win32cui)
add_importlibs(uianimation_apitest ole32 msvcrt kernel32 ntdll)
add_dependencies(uianimation_apitest psdk)
add_rostests_file(TARGET uianimation_apitest)
