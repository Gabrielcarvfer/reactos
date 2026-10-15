
spec2def(ucrtbase.dll ucrtbase.spec NO_DBG ADD_IMPORTLIB)

# Hack to replace the old CRT include directory with the UCRT include directory
get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

add_compile_definitions(
    _UCRT
    _CORECRT_BUILD
    CRTDLL
)

# The C99 math functions that the CRT libraries lack, from FreeBSD's msun
list(APPEND MSUN_SOURCE
    msun/b_exp.c
    msun/b_log.c
    msun/b_tgamma.c
    msun/c99_extra.c
    msun/e_acosh.c
    msun/e_acoshf.c
    msun/e_atanh.c
    msun/e_atanhf.c
    msun/e_lgamma_r.c
    msun/e_lgammaf_r.c
    msun/e_remainder.c
    msun/e_remainderf.c
    msun/s_asinh.c
    msun/s_asinhf.c
    msun/s_cbrt.c
    msun/s_cbrtf.c
    msun/s_erf.c
    msun/s_erff.c
    msun/s_llrint.c
    msun/s_llrintf.c
    msun/s_llround.c
    msun/s_llroundf.c
    msun/s_log1p.c
    msun/s_log1pf.c
    msun/s_logbf.c
    msun/s_lround.c
    msun/s_lroundf.c
    msun/s_remquo.c
    msun/s_remquof.c
    msun/s_rint.c
    msun/s_rintf.c
    msun/s_tgammaf.c)

add_library(ucrtbase_msun STATIC ${MSUN_SOURCE})
target_include_directories(ucrtbase_msun PRIVATE msun/include msun)
if(NOT MSVC)
    target_compile_options(ucrtbase_msun PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable
                           -Wno-parentheses -Wno-implicit-fallthrough -fno-builtin -include math.h)
endif()
target_compile_definitions(ucrtbase_msun PRIVATE _USE_MATH_DEFINES)
add_dependencies(ucrtbase_msun psdk)

add_library(ucrtbase SHARED
    printf.c
    stubs.c
    ucrtbase_stubs.c
    ucrtbase.def
)

if(NOT MSVC)
    target_compile_options(ucrtbase PRIVATE -Wno-builtin-declaration-mismatch)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Silence warnings in ucrtbase_stubs.c
    target_compile_options(ucrtbase PRIVATE -Wno-incompatible-library-redeclaration)
endif()

set_entrypoint(ucrtbase __acrt_DllMain 12)

target_link_libraries(ucrtbase
    ucrtbase_msun
    ucrt
    msvcrt_shared
    crtmath
    vcruntime
    vcstartup
    chkstk
    wine
)

# Implicitly link to vcstartup
target_link_libraries(libucrtbase INTERFACE vcstartup)

if(MSVC)
    target_link_libraries(ucrtbase runtmchk)
else()
    # For __cxa_guard_acquire / __cxa_guard_release
    target_link_libraries(ucrtbase libsupc++)
endif()

add_importlibs(ucrtbase kernel32 ntdll)

add_cd_file(TARGET ucrtbase DESTINATION reactos/system32 FOR all)
