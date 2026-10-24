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

/* No pointer device exists (see GetPointerDevices): none can be asked about */
/* @implemented */
BOOL
WINAPI
GetPointerDevice(
    _In_ HANDLE device,
    _Out_ PVOID pointerDevice)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerDeviceProperties(
    _In_ HANDLE device,
    _Inout_ UINT32 *propertyCount,
    _Out_opt_ PVOID pointerProperties)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetRawPointerDeviceData(
    _In_ UINT32 pointerId,
    _In_ UINT32 historyCount,
    _In_ UINT32 propertiesCount,
    _In_ PVOID pProperties,
    _Out_ LONG *pValues)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerInputTransform(
    _In_ UINT32 pointerId,
    _In_ UINT32 historyCount,
    _Out_ PVOID inputTransform)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerFrameInfo(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *pointerCount,
    _Out_opt_ PVOID pointerInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerFrameInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_ UINT32 *entriesCount,
    _Inout_ UINT32 *pointerCount,
    _Out_opt_ PVOID pointerInfo)
{
    return NoSuchPointer();
}

/* @implemented */
BOOL
WINAPI
GetPointerFramePenInfoHistory(
    _In_ UINT32 pointerId,
    _Inout_opt_ UINT32 *entriesCount,
    _Inout_opt_ UINT32 *pointerCount,
    _Out_opt_ PVOID penInfo)
{
    return NoSuchPointer();
}

/* Synthetic pointer devices are not supported (CreateSyntheticPointerDevice) */
/* @implemented */
BOOL
WINAPI
InjectSyntheticPointerInput(
    _In_ HANDLE device,
    _In_ PVOID pointerInfo,
    _In_ UINT32 count)
{
    return NoSuchPointer();
}

/*
 * @implemented
 * Redirecting all the pointer input of a type to a window needs UIAccess,
 * which no ReactOS process has: fail like Windows does without it.
 */
BOOL
WINAPI
RegisterPointerInputTarget(
    _In_ HWND hwnd,
    _In_ DWORD pointerType)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

/* @implemented (see RegisterPointerInputTarget) */
BOOL
WINAPI
UnregisterPointerInputTarget(
    _In_ HWND hwnd,
    _In_ DWORD pointerType)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
