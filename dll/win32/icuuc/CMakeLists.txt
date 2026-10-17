
spec2def(icuuc.dll icuuc.spec)

add_library(icuuc MODULE
    icuuc.c
    ${CMAKE_CURRENT_BINARY_DIR}/icuuc.def)

set_module_type(icuuc win32dll ENTRYPOINT 0)
add_importlibs(icuuc msvcrt kernel32 ntdll)
add_cd_file(TARGET icuuc DESTINATION reactos/system32 FOR all)

# Windows 10 1903+ has all of ICU in one icu.dll. Programs ship an icuuc.dll
# forwarding to it (Audacity 4: "icu.ucnv_open could not be located").
# It goes to the Windows folder, not System32: the usual DLL search finds it
# there, but the .NET runtime, which looks in System32 only, does not. .NET
# takes an icu.dll in System32 for all of ICU and aborts on the first missing
# function (HandBrake, Paint.NET: 0xc0000409); without one it uses NLS.
spec2def(icu.dll icuuc.spec)

add_library(icu MODULE
    icuuc.c
    ${CMAKE_CURRENT_BINARY_DIR}/icu.def)

set_module_type(icu win32dll ENTRYPOINT 0)
add_importlibs(icu msvcrt kernel32 ntdll)
add_cd_file(TARGET icu DESTINATION reactos FOR all)
