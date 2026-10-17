
PROJECT(NTOS)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Enable this again. CORE-17637
    add_compile_options(-Wunused-result)
endif()

include(ntos.cmake)

if(KCSAN_PROTOTYPE)
    # The lock tables of fsrtl/filelock.c are RTL generic tables (splay trees):
    # an instrumented copy of that code, linked before rtl so it is the one used
    add_library(rtl_kcsan
        ${REACTOS_SOURCE_DIR}/sdk/lib/rtl/generictable.c
        ${REACTOS_SOURCE_DIR}/sdk/lib/rtl/splaytree.c)
    target_include_directories(rtl_kcsan PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/rtl)
    target_compile_options(rtl_kcsan PRIVATE -fsanitize=thread)
    target_compile_definitions(rtl_kcsan PRIVATE KCSAN_INSTRUMENTED)
    target_link_libraries(rtl_kcsan PRIVATE pseh)
    add_dependencies(rtl_kcsan psdk asm)
    set(KCSAN_RTL_LIB rtl_kcsan)
endif()

set(NTOSKRNL_SOURCE ${SOURCE})
set(NTOSKRNL_ASM_SOURCE ${ASM_SOURCE})

set(NTKRNLMP_SOURCE ${SOURCE})
set(NTKRNLMP_ASM_SOURCE ${ASM_SOURCE})

# KCSAN builds: the kernel also exports the KCSAN runtime for drivers built
# with KCSAN_MODULES (ntoskrnl.spec + kcsan/kcsan.spec)
set(NTOSKRNL_SPEC ntoskrnl.spec)
if(KCSAN_PROTOTYPE)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/ntoskrnl.spec _ntoskrnl_spec)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/kcsan/kcsan.spec _kcsan_spec)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl_kcsan.spec "${_ntoskrnl_spec}\n${_kcsan_spec}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ntoskrnl.spec kcsan/kcsan.spec)
    file(RELATIVE_PATH NTOSKRNL_SPEC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl_kcsan.spec)
endif()
spec2def(ntoskrnl.exe ${NTOSKRNL_SPEC} ADD_IMPORTLIB)

# Link GCC stack protector lib
if (STACK_PROTECTOR)
    target_link_libraries(libntoskrnl INTERFACE gcc_ssp_nt)
endif()

add_asm_files(ntoskrnl_asm ${NTOSKRNL_ASM_SOURCE})

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang optimises strcmp calls to memcmp.
    target_link_libraries(libntoskrnl INTERFACE memcmp)
endif()

if((CMAKE_C_COMPILER_ID STREQUAL "Clang") OR (CMAKE_C_COMPILER_ID STREQUAL "GNU"))
    # Enable xsave feature
    add_compile_options(-mxsave)
endif()

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_executable(ntoskrnl
    ${ntoskrnl_asm}
    ${NTOSKRNL_SOURCE}
    ${PCH_SKIP_SOURCE}
    ntoskrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl.def)
set_module_type(ntoskrnl kernel)

source_group(TREE ${REACTOS_SOURCE_DIR}/ntoskrnl PREFIX "Source Files" FILES ${NTOSKRNL_SOURCE})

target_link_libraries(ntoskrnl ntoskrnl_vista cportlib csq ${PSEH_LIB} arbiter cmlib ntlsalib ${KCSAN_RTL_LIB} rtl rtl_vista ${ROSSYM_LIB} libcntpr setjmp wdmguid poguid ioevent)

if(STACK_PROTECTOR)
    target_link_libraries(ntoskrnl gcc_ssp_nt)
endif()

add_importlibs(ntoskrnl hal kdcom bootvid)
add_pch(ntoskrnl ${REACTOS_SOURCE_DIR}/ntoskrnl/include/ntoskrnl.h "${PCH_SKIP_SOURCE}")
add_dependencies(ntoskrnl psdk asm)
add_cd_file(TARGET ntoskrnl DESTINATION reactos/system32 NO_CAB FOR all)

add_subdirectory(ntkrnlmp)

add_asm_files(ntdllsys_asm ntdll.S)
add_library(ntdllsys ${ntdllsys_asm})
set_target_properties(ntdllsys PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys asm)

# NT6 syscalls for ntdll_vista
add_asm_files(ntdllsys_nt6_asm ntdll_nt6.S)
add_library(ntdllsys_nt6 ${ntdllsys_nt6_asm})
set_target_properties(ntdllsys_nt6 PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys_nt6 asm)
