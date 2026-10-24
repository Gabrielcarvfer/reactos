
add_executable(icacls icacls.c icacls.rc)
set_module_type(icacls win32cui UNICODE)
add_importlibs(icacls advapi32 msvcrt kernel32)
add_cd_file(TARGET icacls DESTINATION reactos/system32 FOR all)
