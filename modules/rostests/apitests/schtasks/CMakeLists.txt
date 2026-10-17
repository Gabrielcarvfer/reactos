
add_executable(schtasks_apitest CreateTr.c testlist.c)
target_link_libraries(schtasks_apitest uuid)
set_module_type(schtasks_apitest win32cui)
add_importlibs(schtasks_apitest ole32 oleaut32 msvcrt kernel32)
add_rostests_file(TARGET schtasks_apitest)
