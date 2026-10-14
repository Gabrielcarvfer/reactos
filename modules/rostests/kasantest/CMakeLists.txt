
add_library(kasantest MODULE kasantest.c)
set_module_type(kasantest kernelmodedriver)
add_importlibs(kasantest ntoskrnl)
add_rostests_file(TARGET kasantest)
