
add_library(initmapasm MODULE initmapasm.c initmapasm.rc)
set_module_type(initmapasm win32dll ENTRYPOINT DllMain 12)
add_importlibs(initmapasm kernel32 ntdll)
add_dependencies(initmapasm psdk)
