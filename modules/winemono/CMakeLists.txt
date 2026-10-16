
## Wine Mono (https://github.com/madewokherd/wine-mono; MIT licence, built from
## Mono and Microsoft's open-source .NET code): the .NET Framework runtime
## that mscoree.dll (Wine's) loads, for .NET Framework 2.0-4.8 programs.
## The release MSI is downloaded when configuring, checked against a fixed
## SHA256, and put in %SystemRoot%\3rdParty. On the first boot (RunOnceEx, after
## syssetup's Finalize) it is installed with msiexec into %SystemRoot%\mono\mono-2.0,
## where mscoree looks first (get_mono_path_local), for 64-bit and 32-bit
## (WoW64) programs alike: bin\ has libmono-2.0-x86_64.dll and
## libmono-2.0-x86.dll. It also installs its support MSI (the .NET Framework
## registry keys, %SystemRoot%\Microsoft.NET), then mscoree is registered.
## The version must be the one mscoree asks for: WINE_MONO_VERSION in
## dll/win32/mscoree/mscoree_private.h (Wine 10.0: 9.4.0).
## Without network access: put the MSI in WINEMONO_DOWNLOAD_DIR by hand.
option(ENABLE_WINEMONO "Ship Wine Mono (the .NET Framework runtime for mscoree)" ON)
# (The SysWOW64 build: the 64-bit build ships it, for both)
if(NOT ENABLE_WINEMONO OR BUILD_WOW6432)
    return()
endif()
if(NOT (ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386"))
    return()
endif()

set(WINEMONO_VERSION "9.4.0")
# One MSI for x86 and x86-64 (both libmono DLLs)
set(_winemono_sha256 "cf6173ae94b79e9de13d9a74cdb2560a886fc3d271f9489acb1cfdbd961cacb2")
set(_winemono_name "wine-mono-${WINEMONO_VERSION}-x86.msi")
set(_winemono_url "https://github.com/madewokherd/wine-mono/releases/download/wine-mono-${WINEMONO_VERSION}/${_winemono_name}")

set(WINEMONO_DOWNLOAD_DIR "${REACTOS_BINARY_DIR}/wine-mono" CACHE PATH
    "Where the Wine Mono MSI is downloaded to (or was put by hand)")
set(_winemono_msi "${WINEMONO_DOWNLOAD_DIR}/${_winemono_name}")

set(_winemono_hash "")
if(EXISTS ${_winemono_msi})
    file(SHA256 ${_winemono_msi} _winemono_hash)
endif()

if(NOT _winemono_hash STREQUAL _winemono_sha256)
    message(STATUS "Downloading ${_winemono_url}")
    file(MAKE_DIRECTORY ${WINEMONO_DOWNLOAD_DIR})
    file(REMOVE ${_winemono_msi})
    # CMake's own download first; some CMake builds (e.g. RosBE's) have
    # no HTTPS support, so then wget or curl
    file(DOWNLOAD ${_winemono_url} ${_winemono_msi} STATUS _winemono_status TLS_VERIFY ON)
    list(GET _winemono_status 0 _winemono_rc)
    if(NOT _winemono_rc EQUAL 0)
        find_program(_winemono_wget wget)
        find_program(_winemono_curl curl)
        if(_winemono_wget)
            execute_process(COMMAND ${_winemono_wget} -q -O ${_winemono_msi} ${_winemono_url} RESULT_VARIABLE _winemono_rc)
        elseif(_winemono_curl)
            execute_process(COMMAND ${_winemono_curl} -fsSL -o ${_winemono_msi} ${_winemono_url} RESULT_VARIABLE _winemono_rc)
        endif()
    endif()
    set(_winemono_hash "")
    if(EXISTS ${_winemono_msi})
        file(SHA256 ${_winemono_msi} _winemono_hash)
    endif()
    if(NOT _winemono_hash STREQUAL _winemono_sha256)
        file(REMOVE ${_winemono_msi})
        message(FATAL_ERROR "Could not download ${_winemono_url} (or its SHA256 is wrong).\n"
            "Download it by hand into ${WINEMONO_DOWNLOAD_DIR}, or configure with -DENABLE_WINEMONO=OFF.")
    endif()
endif()

# Installed by setup only (the LiveCD cannot install it)
add_cd_file(FILE ${_winemono_msi} DESTINATION reactos/3rdParty FOR bootcd)
add_registry_inf(winemono_reg.inf)
if(ARCH STREQUAL "amd64")
    add_registry_inf(winemono_wow64_reg.inf)
endif()
