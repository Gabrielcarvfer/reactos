
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl ${CMAKE_CURRENT_BINARY_DIR})
add_rpc_files(server ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/atsvc.idl
                      ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/schrpc.idl)
spec2def(schedsvc.dll schedsvc.spec ADD_IMPORTLIB)

add_library(schedsvc MODULE
    job.c
    rpcserver.c
    schedsvc.c
    schrpc.c
    schedsvc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/atsvc_s.c
    ${CMAKE_CURRENT_BINARY_DIR}/schrpc_s.c
    ${CMAKE_CURRENT_BINARY_DIR}/schedsvc.def)

set_module_type(schedsvc win32dll UNICODE)
target_link_libraries(schedsvc wine)
add_importlibs(schedsvc advapi32 rpcrt4 ole32 xmllite msvcrt kernel32 ntdll)
add_cd_file(TARGET schedsvc DESTINATION reactos/system32 FOR all)
