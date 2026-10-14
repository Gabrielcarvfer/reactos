/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Pointer input functions of Windows 8
 *
 * ReactOS sends no pointer messages (WM_POINTER*), so no pointer id a
 * program could pass is valid: these fail like Windows does for an unknown
 * pointer. Qt's windows platform plugin imports them.
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winuser.h>

static
BOOL
NoSuchPointer(VOID)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* @implemented */
BOOL
WINAPI
GetPointerDeviceRects(
    _In_ HANDLE device,
    _Out_ RECT *pointerDeviceRect,
    _Out_ RECT *displayRect)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerFrameTouchInfo(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *pointerCount,
    _Out_opt_ PVOID touchInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerFrameTouchInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Inout_ UINT32 *pointerCount,
    _Out_opt_ PVOID touchInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerPenInfo(
    _In_ UINT32 pointerId,
    _Out_ PVOID penInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerPenInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Out_opt_ PVOID penInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
SkipPointerFrameMessages(
    _In_ UINT32 pointerId)
{
    return NoSuchPointer();
}
