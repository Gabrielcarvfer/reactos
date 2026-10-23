
add_subdirectory(msi_ca_dll)

# Ensure the resource compiler can find the dll
include_directories($<TARGET_FILE_DIR:msi_ca_dll>)

list(APPEND SOURCE
    CustomAction.c
    testdata.rc
    testlist.c)

add_rc_deps(testdata.rc ${CMAKE_CURRENT_BINARY_DIR}/msi_ca_dll/msi_ca_dll.dll)

add_executable(msi_apitest ${SOURCE})
set_module_type(msi_apitest win32cui)
add_importlibs(msi_apitest msi user32 advapi32 msvcrt kernel32)
add_dependencies(msi_apitest msi_ca_dll)
add_rostests_file(TARGET msi_apitest)
