
## Mesa's OpenGL driver (llvmpipe, OpenGL 4.6 in software), when its DLL was
## put here: x64/libgallium_wgl.dll and x86/libgallium_wgl.dll from
## mesa-dist-win (https://github.com/pal1000/mesa-dist-win; MIT licence).
## The DLLs are not in git (see README.txt). Without them, nothing is added.
if(ARCH STREQUAL "amd64")
    set(_mesa_dll ${CMAKE_CURRENT_SOURCE_DIR}/x64/libgallium_wgl.dll)
elseif(ARCH STREQUAL "i386")
    # (The WoW64 build's system32 is SysWOW64 of 64-bit ReactOS)
    set(_mesa_dll ${CMAKE_CURRENT_SOURCE_DIR}/x86/libgallium_wgl.dll)
endif()

if(_mesa_dll AND EXISTS ${_mesa_dll})
    add_cd_file(FILE ${_mesa_dll} DESTINATION reactos/system32 FOR all)
    add_registry_inf(mesa_reg.inf)
endif()
