
spec2def(wkscli.dll wkscli.spec)

list(APPEND SOURCE
    wkscli.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wkscli.def)

add_library(wkscli MODULE ${SOURCE})
set_module_type(wkscli win32dll ENTRYPOINT 0)
add_importlibs(wkscli netapi32 kernel32 ntdll)
add_cd_file(TARGET wkscli DESTINATION reactos/system32 FOR all)
