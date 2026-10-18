
add_library(init_fail_dll MODULE init_fail_dll.c)
set_module_type(init_fail_dll win32dll ENTRYPOINT DllMain 12)
add_importlibs(init_fail_dll user32 msvcrt kernel32)
add_rostests_file(TARGET init_fail_dll)
