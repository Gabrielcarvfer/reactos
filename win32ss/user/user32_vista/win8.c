/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Window and display functions of Windows 8 and 10
 *              (Chromium, Electron and Firefox import them)
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winuser.h>

/* Windows 11's snap layouts: no window is "arranged" here */
/* @implemented */
BOOL
WINAPI
IsWindowArranged(
    _In_ HWND hwnd)
{
    return FALSE;
}

/* No rotation sensor (AR_NOSENSOR) */
/* @implemented */
BOOL
WINAPI
GetAutoRotationState(
    _Out_ PDWORD pState)
{
    if (!pState)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pState = 0x10;
    return TRUE;
}

/* Every window is in the desktop band (ZBID_DESKTOP) */
/* @implemented */
BOOL
WINAPI
GetWindowBand(
    _In_ HWND hwnd,
    _Out_ PDWORD pdwBand)
{
    if (!IsWindow(hwnd) || !pdwBand)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pdwBand = 1;
    return TRUE;
}

/*
 * Touch targeting: how close a touch contact is to a control. There is no
 * touch input here (see the pointer functions): no contact is ever near.
 */
/* @implemented */
BOOL
WINAPI
EvaluateProximityToPolygon(
    _In_ UINT32 numVertices,
    _In_ const POINT *controlPolygon,
    _In_ const VOID *pHitTestingInput,
    _Out_ VOID *pProximityEval)
{
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/*
 * GetIconInfo, plus where the icon came from. ReactOS does not keep the
 * module and resource of an icon for this: they are left empty, as for an
 * icon made with CreateIconIndirect.
 */
/* @implemented */
BOOL
WINAPI
GetIconInfoExW(
    _In_ HICON hicon,
    _Inout_ PICONINFOEXW piconinfo)
{
    ICONINFO Info;

    if (!piconinfo || piconinfo->cbSize != sizeof(*piconinfo))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetIconInfo(hicon, &Info))
        return FALSE;
    piconinfo->fIcon = Info.fIcon;
    piconinfo->xHotspot = Info.xHotspot;
    piconinfo->yHotspot = Info.yHotspot;
    piconinfo->hbmMask = Info.hbmMask;
    piconinfo->hbmColor = Info.hbmColor;
    piconinfo->wResID = 0;
    piconinfo->szModName[0] = UNICODE_NULL;
    piconinfo->szResName[0] = UNICODE_NULL;
    return TRUE;
}

/* @implemented */
BOOL
WINAPI
GetIconInfoExA(
    _In_ HICON hicon,
    _Inout_ PICONINFOEXA piconinfo)
{
    ICONINFO Info;

    if (!piconinfo || piconinfo->cbSize != sizeof(*piconinfo))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetIconInfo(hicon, &Info))
        return FALSE;
    piconinfo->fIcon = Info.fIcon;
    piconinfo->xHotspot = Info.xHotspot;
    piconinfo->yHotspot = Info.yHotspot;
    piconinfo->hbmMask = Info.hbmMask;
    piconinfo->hbmColor = Info.hbmColor;
    piconinfo->wResID = 0;
    piconinfo->szModName[0] = '\0';
    piconinfo->szResName[0] = '\0';
    return TRUE;
}
