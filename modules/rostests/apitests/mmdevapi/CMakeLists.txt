
remove_definitions(-D_WIN32_WINNT=0x502)
add_definitions(-D_WIN32_WINNT=0x600)

add_executable(mmdevapi_apitest AudioRender.c testlist.c)
target_link_libraries(mmdevapi_apitest wine uuid)
set_module_type(mmdevapi_apitest win32cui)
add_importlibs(mmdevapi_apitest ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET mmdevapi_apitest)
