
# wow64.dll runs the system calls of 32-bit programs on 64-bit ReactOS: it
# turns each one into the 64-bit system call. It comes from Wine; reactos.c
# does the jobs of Wine's Unix side (loading the 32-bit ntdll and such).

# The CSR client is not Wine code: it goes first, without Wine's headers
add_subdirectory(csr)

add_definitions(-D__WINESRC__ -D_WINE)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

# The 32-bit and the 64-bit kernels number the system calls the same way, in
# the order of sysfuncs.lst: the thunk table follows it
file(STRINGS ${REACTOS_SOURCE_DIR}/ntoskrnl/sysfuncs.lst _sysfuncs)
set(_syscalls "")
foreach(_line IN LISTS _sysfuncs)
    if(_line MATCHES "^([A-Za-z0-9_]+)[ \t]+([0-9]+)")
        math(EXPR _bytes "${CMAKE_MATCH_2} * 4")
        string(APPEND _syscalls "    SYSCALL_ENTRY( 0, ${CMAKE_MATCH_1}, ${_bytes} ) \\\n")
    endif()
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ntsyscalls.h.new "#define ALL_SYSCALLS32 \\\n${_syscalls}\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/ntsyscalls.h.new ${CMAKE_CURRENT_BINARY_DIR}/ntsyscalls.h COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REACTOS_SOURCE_DIR}/ntoskrnl/sysfuncs.lst)

spec2def(wow64.dll wow64.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    file.c
    process.c
    reactos.c
    registry.c
    regredirect.c
    security.c
    stubs.c
    sync.c
    syscall.c
    system.c
    virtual.c)

add_library(wow64 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/wow64_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64.def)

set_module_type(wow64 win32dll ENTRYPOINT DllMain 12)
set_wine_module(wow64)
target_link_libraries(wow64 wow64_csr pseh)
add_importlibs(wow64 ntdll)
add_cd_file(TARGET wow64 DESTINATION reactos/system32 FOR all)

# The 32-bit system files go to SysWOW64: they come from an i386 build of the
# same sources (its system32 files, as listed in its reactos.dff)
set(WOW64_I386_BUILD_DIR "" CACHE PATH "i386 build tree whose system32 files go to SysWOW64")
if(WOW64_I386_BUILD_DIR)
    if(NOT EXISTS ${WOW64_I386_BUILD_DIR}/boot/bootdata/packages/reactos.dff)
        message(FATAL_ERROR "WOW64_I386_BUILD_DIR: no boot/bootdata/packages/reactos.dff (build its bootcd first)")
    endif()
    # The files of the i386 build to take, by their CD directory:
    # system32 (2) goes to SysWOW64, and the 32-bit side-by-side assemblies
    # (winsxs\x86_*, and their x86_* manifests) to the same place as there
    set(_wow64_dff ${WOW64_I386_BUILD_DIR}/boot/bootdata/packages/reactos.dff)
    file(STRINGS ${_wow64_dff} _wow64_dirs REGEX "^[0-9]+ = ")
    foreach(_line ${_wow64_dirs})
        if(_line MATCHES "^([0-9]+) = (.*)$")
            set(_wow64_dir_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    file(STRINGS ${_wow64_dff} _wow64_files REGEX "^\"[^\"]*\" [0-9]+$")
    foreach(_line ${_wow64_files})
        if(NOT _line MATCHES "^\"([^\"]*)\" ([0-9]+)$")
            continue()
        endif()
        set(_file "${CMAKE_MATCH_1}")
        set(_dir "${_wow64_dir_${CMAKE_MATCH_2}}")
        get_filename_component(_name ${_file} NAME)
        if(CMAKE_MATCH_2 STREQUAL "2")
            set(_dest reactos/SysWOW64)
        elseif(_dir MATCHES "^winsxs\\\\x86_" OR (_dir STREQUAL "winsxs\\manifests" AND _name MATCHES "^x86_"))
            string(REPLACE "\\" "/" _dest "reactos/${_dir}")
        else()
            continue()
        endif()
        # Paths are those of the i386 build: its build tree, or the sources
        file(RELATIVE_PATH _rel_src ${REACTOS_SOURCE_DIR} ${_file})
        if(EXISTS ${_file} AND NOT _rel_src MATCHES "^\\.\\.")
            add_cd_file(FILE ${_file} DESTINATION ${_dest} FOR all)
        else()
            string(REGEX REPLACE "^/[^/]+/" "" _rel ${_file})
            add_cd_file(FILE ${WOW64_I386_BUILD_DIR}/${_rel} DESTINATION ${_dest} FOR all)
        endif()
    endforeach()
    # (the 32-bit ntdll is not in that list: it is copied outside the cabinet)
    add_cd_file(FILE ${WOW64_I386_BUILD_DIR}/dll/ntdll/ntdll.dll DESTINATION reactos/SysWOW64 FOR all)
endif()
