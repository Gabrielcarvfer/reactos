Mesa's OpenGL driver for ReactOS (OpenGL 4.6 in software, llvmpipe)

When configuring, CMake downloads the mesa-dist-win release (MIT licence,
https://github.com/pal1000/mesa-dist-win/releases), checks its SHA256 and
extracts libgallium_wgl.dll:
  64-bit build:  mesa3d-<version>-release-mingw.7z, x64 folder
  32-bit build:  mesa3d-<version>-release-msvc.7z, x86 folder
                 (the 32-bit mingw build has no LLVM, only softpipe)
The build installs it in system32 (SysWOW64 for the x86 one when it is the
WoW64 part of 64-bit ReactOS) and registers Mesa as the OpenGL driver.

Options:
  -DENABLE_MESA=OFF         do not ship Mesa
  -DMESA_DOWNLOAD_DIR=path  where the archives are kept; put them there by
                            hand to build without network access
A DLL put in x64/ or x86/ of this folder (e.g. your own Mesa build) is used
instead of the downloaded one.

To update Mesa, change MESA_VERSION and the two SHA256 values in CMakeLists.txt.
