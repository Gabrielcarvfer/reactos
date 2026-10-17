
add_definitions(-D__WINESRC__)
spec2def(appxdeploymentclient.dll appxdeploymentclient.spec)

list(APPEND SOURCE
    compat.c
    main.c
    package.c
    ${CMAKE_CURRENT_BINARY_DIR}/appxdeploymentclient_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/appxdeploymentclient.def)

add_library(appxdeploymentclient MODULE ${SOURCE})
set_module_type(appxdeploymentclient win32dll ENTRYPOINT 0)
target_link_libraries(appxdeploymentclient uuid wine)
add_importlibs(appxdeploymentclient combase msvcrt kernel32 ntdll)
add_dependencies(appxdeploymentclient psdk)
add_cd_file(TARGET appxdeploymentclient DESTINATION reactos/system32 FOR all)
