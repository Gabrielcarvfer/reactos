
add_executable(klsan klsan.c)
set_module_type(klsan win32cui)
add_importlibs(klsan ntdll msvcrt kernel32)
add_cd_file(TARGET klsan DESTINATION reactos/system32 FOR all)
