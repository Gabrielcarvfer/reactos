
spec2def(srvcli.dll srvcli.spec)

list(APPEND SOURCE
    srvcli.rc
    ${CMAKE_CURRENT_BINARY_DIR}/srvcli.def)

add_library(srvcli MODULE ${SOURCE})
set_module_type(srvcli win32dll ENTRYPOINT 0)
add_importlibs(srvcli netapi32 kernel32 ntdll)
add_cd_file(TARGET srvcli DESTINATION reactos/system32 FOR all)
