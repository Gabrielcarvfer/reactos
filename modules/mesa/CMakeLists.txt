## Mesa's OpenGL driver (llvmpipe, OpenGL 4.6 in software) from mesa-dist-win
## (https://github.com/pal1000/mesa-dist-win; MIT licence). The release
## archive is downloaded when configuring, checked against a fixed SHA256,
## and libgallium_wgl.dll is installed in system32 (SysWOW64 for the WoW64
## build) together with the registry entries that make it the OpenGL driver.
## See README.txt for building without network access.
option(ENABLE_MESA "Ship Mesa (llvmpipe) as the OpenGL driver" ON)
if(NOT ENABLE_MESA)
    return()
endif()

set(MESA_VERSION "26.2.4")
if(ARCH STREQUAL "amd64")
    # The mingw build: its x64 DLL has LLVM (llvmpipe)
    set(_mesa_flavor "mingw")
    set(_mesa_sha256 "36a30a04d8cab6e2bea2d3c747c326d15024d9711dd80f45ef97ac6f2c3c5086")
    set(_mesa_subdir "x64")
elseif(ARCH STREQUAL "i386")
    # The 32-bit mingw build has no LLVM (softpipe only), use the MSVC one
    set(_mesa_flavor "msvc")
    set(_mesa_sha256 "351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695")
    set(_mesa_subdir "x86")
else()
    return()
endif()

# A DLL put in x64/ or x86/ by hand wins (e.g. a self-built Mesa)
set(_mesa_dll ${CMAKE_CURRENT_SOURCE_DIR}/${_mesa_subdir}/libgallium_wgl.dll)

if(NOT EXISTS ${_mesa_dll})
    set(MESA_DOWNLOAD_DIR "${REACTOS_BINARY_DIR}/mesa-dist-win" CACHE PATH
        "Where the mesa-dist-win archives are downloaded to (or were put by hand)")
    set(_mesa_name "mesa3d-${MESA_VERSION}-release-${_mesa_flavor}.7z")
    set(_mesa_archive "${MESA_DOWNLOAD_DIR}/${_mesa_name}")
    set(_mesa_url "https://github.com/pal1000/mesa-dist-win/releases/download/${MESA_VERSION}/${_mesa_name}")
    set(_mesa_dll "${MESA_DOWNLOAD_DIR}/${MESA_VERSION}-${_mesa_flavor}/${_mesa_subdir}/libgallium_wgl.dll")

    set(_mesa_hash "")
    if(EXISTS ${_mesa_archive})
        file(SHA256 ${_mesa_archive} _mesa_hash)
    endif()

    if(NOT _mesa_hash STREQUAL _mesa_sha256)
        message(STATUS "Downloading ${_mesa_url}")
        file(MAKE_DIRECTORY ${MESA_DOWNLOAD_DIR})
        file(REMOVE ${_mesa_archive})
        # CMake's own download first; some CMake builds (e.g. RosBE's) have
        # no HTTPS support, so then wget or curl
        file(DOWNLOAD ${_mesa_url} ${_mesa_archive} STATUS _mesa_status TLS_VERIFY ON)
        list(GET _mesa_status 0 _mesa_rc)
        if(NOT _mesa_rc EQUAL 0)
            find_program(_mesa_wget wget)
            find_program(_mesa_curl curl)
            if(_mesa_wget)
                execute_process(COMMAND ${_mesa_wget} -q -O ${_mesa_archive} ${_mesa_url} RESULT_VARIABLE _mesa_rc)
            elseif(_mesa_curl)
                execute_process(COMMAND ${_mesa_curl} -fsSL -o ${_mesa_archive} ${_mesa_url} RESULT_VARIABLE _mesa_rc)
            endif()
        endif()
        set(_mesa_hash "")
        if(EXISTS ${_mesa_archive})
            file(SHA256 ${_mesa_archive} _mesa_hash)
        endif()
        if(NOT _mesa_hash STREQUAL _mesa_sha256)
            file(REMOVE ${_mesa_archive})
            message(FATAL_ERROR "Could not download ${_mesa_url} (or its SHA256 is wrong).\n"
                "Download it by hand into ${MESA_DOWNLOAD_DIR}, or configure with -DENABLE_MESA=OFF.")
        endif()
    endif()

    if(NOT EXISTS ${_mesa_dll})
        file(MAKE_DIRECTORY ${MESA_DOWNLOAD_DIR}/${MESA_VERSION}-${_mesa_flavor})
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${_mesa_archive} ${_mesa_subdir}/libgallium_wgl.dll
                        WORKING_DIRECTORY ${MESA_DOWNLOAD_DIR}/${MESA_VERSION}-${_mesa_flavor}
                        RESULT_VARIABLE _mesa_rc)
        if(NOT _mesa_rc EQUAL 0 OR NOT EXISTS ${_mesa_dll})
            message(FATAL_ERROR "Could not extract ${_mesa_subdir}/libgallium_wgl.dll from ${_mesa_archive}")
        endif()
    endif()
endif()

add_cd_file(FILE ${_mesa_dll} DESTINATION reactos/system32 FOR all)
add_registry_inf(mesa_reg.inf)
