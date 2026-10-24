
# The same stub, as the DLL of each service
foreach(_name iphlpsvc netprofmsvc winhttpsvc)
    spec2def(${_name}.dll svcstub.spec)
    add_library(${_name} MODULE
        svcstub.c
        ${_name}.rc
        ${CMAKE_CURRENT_BINARY_DIR}/${_name}.def)
    set_module_type(${_name} win32dll UNICODE)
    add_importlibs(${_name} advapi32 msvcrt kernel32 ntdll)
    add_cd_file(TARGET ${_name} DESTINATION reactos/system32 FOR all)
endforeach()
