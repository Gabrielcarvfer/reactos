## Wine Gecko, the HTML engine of MSHTML (the "Internet Browser", HTML help,
## every program that shows a web page through MSHTML), from WineHQ (MPL 2.0).
## ReactOS's MSHTML does not offer to download it, so without it every page
## stays blank. When configuring, the MSI packages are downloaded and checked
## against a fixed SHA256; they go to %SystemRoot%\3rdParty (appwiz.cpl's
## GeckoCabDir) and are installed silently on the first boot (RunOnceEx, after
## syssetup's Finalize, like Wine Mono: one after the other). A 64-bit build ships
## the 64-bit package and the 32-bit one for SysWOW64; a 32-bit build the 32-bit
## one. Packages put in this folder by hand win (building without network).
option(ENABLE_WINE_GECKO "Ship Wine Gecko and install it at the first logon" ON)
if(NOT ENABLE_WINE_GECKO OR BUILD_WOW6432)
    return()
endif()

set(WINE_GECKO_VERSION "2.40")
set(_gecko_sha256_x86 "1a29d17435a52b7663cea6f30a0771f74097962b07031947719bb7b46057d302")
set(_gecko_sha256_x86_64 "c3e28988e7d92221596fc4c569d10eb4dd2ca64b9f4970bf77e791f5dd8c9230")

if(ARCH STREQUAL "amd64")
    set(_gecko_archs x86_64 x86)
    set(_gecko_reg gecko_reg_amd64.inf)
elseif(ARCH STREQUAL "i386")
    set(_gecko_archs x86)
    set(_gecko_reg gecko_reg_i386.inf)
else()
    return()
endif()

set(WINE_GECKO_DOWNLOAD_DIR "${REACTOS_BINARY_DIR}/wine-gecko" CACHE PATH
    "Where the Wine Gecko packages are downloaded to (or were put by hand)")

foreach(_arch ${_gecko_archs})
    set(_name "wine_gecko-${WINE_GECKO_VERSION}-${_arch}.msi")
    set(_sha ${_gecko_sha256_${_arch}})
    set(_msi ${CMAKE_CURRENT_SOURCE_DIR}/${_name})
    if(NOT EXISTS ${_msi})
        set(_msi "${WINE_GECKO_DOWNLOAD_DIR}/${_name}")
        set(_url "https://dl.winehq.org/wine/wine-gecko/${WINE_GECKO_VERSION}/${_name}")
        set(_hash "")
        if(EXISTS ${_msi})
            file(SHA256 ${_msi} _hash)
        endif()
        if(NOT _hash STREQUAL _sha)
            message(STATUS "Downloading ${_url}")
            file(MAKE_DIRECTORY ${WINE_GECKO_DOWNLOAD_DIR})
            file(REMOVE ${_msi})
            # (RosBE's CMake may have no HTTPS support: then wget or curl)
            file(DOWNLOAD ${_url} ${_msi} STATUS _status TLS_VERIFY ON)
            list(GET _status 0 _rc)
            if(NOT _rc EQUAL 0)
                find_program(_gecko_wget wget)
                find_program(_gecko_curl curl)
                if(_gecko_wget)
                    execute_process(COMMAND ${_gecko_wget} -q -O ${_msi} ${_url} RESULT_VARIABLE _rc)
                elseif(_gecko_curl)
                    execute_process(COMMAND ${_gecko_curl} -fsSL -o ${_msi} ${_url} RESULT_VARIABLE _rc)
                endif()
            endif()
            set(_hash "")
            if(EXISTS ${_msi})
                file(SHA256 ${_msi} _hash)
            endif()
            if(NOT _hash STREQUAL _sha)
                file(REMOVE ${_msi})
                message(FATAL_ERROR "Could not download ${_url} (or its SHA256 is wrong).\n"
                    "Download it by hand into ${WINE_GECKO_DOWNLOAD_DIR}, or configure with -DENABLE_WINE_GECKO=OFF.")
            endif()
        endif()
    endif()
    add_cd_file(FILE ${_msi} DESTINATION reactos/3rdParty FOR all)
endforeach()

add_registry_inf(${_gecko_reg})
