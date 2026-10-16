Mesa's OpenGL driver for ReactOS (OpenGL 4.6 in software, llvmpipe)

Put the DLLs of a mesa-dist-win release here before building
(https://github.com/pal1000/mesa-dist-win/releases):
  x64/libgallium_wgl.dll  from mesa3d-<version>-release-mingw.7z, x64 folder
  x86/libgallium_wgl.dll  from mesa3d-<version>-release-msvc.7z, x86 folder
                          (the 32-bit mingw build has no LLVM, only softpipe)
They are not in git (large binaries). The build installs them in system32
(SysWOW64 for the x86 one) and registers Mesa as the OpenGL driver.
