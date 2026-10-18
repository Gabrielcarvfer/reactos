add_subdirectory(makeimg)

list(APPEND SOURCE
    AttachThreadInput.c
    ../include/msgtrace.c
    CharFuncs.c
    CloseWindow.c
    CopyImage.c
    CreateDialog.c
    CreateIconFromResourceEx.c
    CreateWindowEx.c
    DeferWindowPos.c
    DestroyCursorIcon.c
    DM_REPOSITION.c
    DpiHostingBehavior.c
    DrawIconEx.c
    DrawText.c
    desktop.c
    EmptyClipboard.c
    EnumDisplaySettings.c
    GetClassInfo.c
    GlobalKeyboardHook.c
    GetDCEx.c
    GetIconInfo.c
    GetKeyState.c
    GetMessageTime.c
    GetPeekMessage.c
    GetSetWindowInt.c
    GetSystemMetrics.c
    GetUserObjectInformation.c
    GetWindowPlacement.c
    GW_ENABLEDPOPUP.c
    InitializeLpkHooks.c
    IsServerSideWindow.c
    KbdLayout.c
    keybd_event.c
    LoadImage.c
    LoadImageGCC.c
    LookupIconIdFromDirectoryEx.c
    MdiAnsi.c
    MenuUI.c
    MessageStateAnalyzer.c
    NcPaintRgn.c
    NextDlgItem.c
    NotepadWindow.c
    PrivateExtractIcons.c
    RealGetWindowClass.c
    RedrawWindow.c
    RegisterClassEx.c
    RegisterHotKey.c
    ScrollBarRedraw.c
    ScrollBarWndExtra.c
    ScrollDC.c
    ScrollWindowEx.c
    SendMessageTimeout.c
    SetActiveWindow.c
    SetCursorPos.c
    SetFocus.c
    SetParent.c
    SetProp.c
    SetScrollInfo.c
    SetScrollRange.c
    SetWindowPlacement.c
    ShowWindow.c
    SwitchToThisWindow.c
    SystemMenu.c
    SystemParametersInfo.c
    TrackMouseEvent.c
    TrackPopupMenuEx.c
    UserHandleGrantAccess.c
    VirtualKey.c
    WindowHooks.c
    WinEventHookInContext.c
    WndProc.c
    wsprintf.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(user32_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    user32_apitest.rc
    user32_apitest_1bpp.rc)

target_link_libraries(user32_apitest wine ${PSEH_LIB})
set_module_type(user32_apitest win32cui)
add_importlibs(user32_apitest advapi32 imm32 gdi32 user32 msvcrt kernel32 ntdll)
add_pch(user32_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET user32_apitest)

add_executable(user32_apitest_menuui user32_apitest_menuui.c)
set_module_type(user32_apitest_menuui win32gui UNICODE)
add_importlibs(user32_apitest_menuui user32 msvcrt kernel32)
add_rostests_file(TARGET user32_apitest_menuui)
add_dependencies(user32_apitest user32_apitest_menuui)

spec2def(user32_apitest_hookdll.dll user32_apitest_hookdll.spec)
add_library(user32_apitest_hookdll MODULE
    user32_apitest_hookdll.c
    ${CMAKE_CURRENT_BINARY_DIR}/user32_apitest_hookdll.def)
set_module_type(user32_apitest_hookdll win32dll ENTRYPOINT DllMain 12)
add_importlibs(user32_apitest_hookdll user32 kernel32)
add_rostests_file(TARGET user32_apitest_hookdll)
add_dependencies(user32_apitest user32_apitest_hookdll)

add_rostests_file(FILE "${CMAKE_CURRENT_SOURCE_DIR}/verclasstest1.manifest")
add_rostests_file(FILE "${CMAKE_CURRENT_SOURCE_DIR}/verclasstest2.manifest")
