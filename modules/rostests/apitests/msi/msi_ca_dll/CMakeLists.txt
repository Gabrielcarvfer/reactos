
add_library(msi_ca_dll MODULE msi_ca_dll.c msi_ca_dll.spec)
set_module_type(msi_ca_dll win32dll ENTRYPOINT 0)
add_importlibs(msi_ca_dll msi kernel32)
add_dependencies(msi_ca_dll psdk)
