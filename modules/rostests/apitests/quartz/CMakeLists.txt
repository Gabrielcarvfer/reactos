
remove_definitions(-D_WIN32_WINNT=0x502)
add_definitions(-D_WIN32_WINNT=0x600)

add_executable(quartz_apitest Parsers.c testlist.c)
target_link_libraries(quartz_apitest wine strmiids uuid)
set_module_type(quartz_apitest win32cui)
add_importlibs(quartz_apitest ole32 msvcrt kernel32 ntdll)
add_dependencies(quartz_apitest dxsdk)
add_rostests_file(TARGET quartz_apitest)
