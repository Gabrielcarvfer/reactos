
add_executable(wevtutil wevtutil.c wevtutil.rc)
set_module_type(wevtutil win32cui UNICODE)
add_importlibs(wevtutil msvcrt kernel32)
add_cd_file(TARGET wevtutil DESTINATION reactos/system32 FOR all)
