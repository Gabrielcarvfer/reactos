
add_subdirectory(redirptest)

add_message_headers(ANSI FormatMessage.mc)

list(APPEND SOURCE
    ConsoleCP.c
    ConsoleCtrlEvent.c
    ConsoleVT.c
    CreateEventEx.c
    CreateProcess.c
    DefaultActCtx.c
    DeviceIoControl.c
    dosdev.c
    EnumSystemCodePages.c
    FastFail.c
    FatStress.c
    FindActCtxSectionStringW.c
    FindFiles.c
    FindFirstFileEx.c
    FLS.c
    FormatMessage.c
    GetComputerNameEx.c
    GetCPInfo.c
    GetCurrentDirectory.c
    GetDriveType.c
    GetDynamicTimeZoneInformation.c
    GetEnvironmentVariable.c
    GetFinalPathNameByHandle.c
    GetLocaleInfo.c
    GetModuleFileName.c
    GetVolumeInformation.c
    HeapCreateExecute.c
    InheritHandleList.c
    InitOnce.c
    interlck.c
    IsDBCSLeadByteEx.c
    JapaneseCalendar.c
    JobObject.c
    LCMapString.c
    LoadLibraryExW.c
    LockStress.c
    LocaleNameToLCID.c
    LockFileEx.c
    lstrcpynW.c
    lstrlen.c
    Mailslot.c
    MultiByteToWideChar.c
    NestedJobs.c
    NewApis.c
    PipeCompletionPort.c
    Pipes.c
    PrivMoveFileIdentityW.c
    ProcThreadAttributeList.c
    QueryLimitedInformation.c
    QueryPerformanceCounter.c
    QueueUserAPC.c
    ReadDirectoryChanges.c
    ReadFile.c
    ReadOnlySection.c
    SecurityLabel.c
    SetComputerNameExW.c
    SetConsoleWindowInfo.c
    SetCurrentDirectory.c
    SetSearchPathMode.c
    SetUnhandledExceptionFilter.c
    StackGuard.c
    SuspendedChildPeb.c
    SystemFirmware.c
    TerminateProcess.c
    TunnelCache.c
    UEFIFirmware.c
    UnregisterWaitEx.c
    WideCharToMultiByte.c
    Wow64Basics.c
    Wow64FsRedirection.c
    WriteFile.c
    WriteProcessMemoryCow.c
)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(kernel32_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    kernel32_apitest.rc)

target_link_libraries(kernel32_apitest wine ${PSEH_LIB})
set_module_type(kernel32_apitest win32cui)
add_delay_importlibs(kernel32_apitest advapi32 shlwapi)
add_importlibs(kernel32_apitest netapi32 msvcrt kernel32 ntdll)
add_dependencies(kernel32_apitest FormatMessage)
add_pch(kernel32_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET kernel32_apitest)

list(APPEND MANIFEST_FILES
    classtest.manifest
    classtest2.manifest
    comctl32dep.manifest
    dep1.manifest
    deptest.manifest
    xmlns.manifest)

foreach(item ${MANIFEST_FILES})
    add_rostests_file(FILE "${CMAKE_CURRENT_SOURCE_DIR}/${item}")
endforeach(item)
