/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DPI functions for user32 and user32_vista.
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <cbialo2@outlook.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

HDC APIENTRY
NtUserGetDC(HWND hWnd);

#define NDEBUG
#include <debug.h>

/*
 * @stub
 */
UINT
WINAPI
GetDpiForSystem(VOID)
{
    HDC hDC;
    UINT Dpi;
    hDC = NtUserGetDC(NULL);
    Dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ReleaseDC(NULL, hDC);
    return Dpi;
}

/*
 * ReactOS has one DPI for all the monitors and never scales the windows of a
 * program for it: every process, thread and window is aware of the system DPI,
 * whatever it asks for.
 */

static BOOL
IsValidContext(DPI_AWARENESS_CONTEXT context)
{
    return context == DPI_AWARENESS_CONTEXT_UNAWARE ||
           context == DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ||
           context == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ||
           context == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ||
           context == DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED;
}

/*
 * @implemented
 */
UINT
WINAPI
GetDpiForWindow(
    _In_ HWND hWnd)
{
    if (!IsWindow(hWnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    return GetDpiForSystem();
}

/*
 * @implemented
 */
BOOL
WINAPI
IsProcessDPIAware(VOID)
{
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetProcessDPIAware(VOID)
{
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetProcessDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    if (!IsValidContext(context))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetProcessDpiAwarenessInternal(
    _In_  HANDLE process,
    _Out_ DPI_AWARENESS *awareness)
{
    if (!awareness)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *awareness = DPI_AWARENESS_SYSTEM_AWARE;
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetProcessDpiAwarenessInternal(
    _In_ DPI_AWARENESS awareness)
{
    if (awareness < DPI_AWARENESS_UNAWARE || awareness > DPI_AWARENESS_PER_MONITOR_AWARE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetDpiForMonitorInternal(
    _In_  HMONITOR monitor,
    _In_  UINT type,
    _Out_ UINT *x,
    _Out_ UINT *y)
{
    if (!x || !y || type > 2 /* MDT_RAW_DPI */)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *x = *y = GetDpiForSystem();
    return TRUE;
}

/*
 * @implemented
 */
DPI_AWARENESS_CONTEXT
WINAPI
GetWindowDpiAwarenessContext(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
}

/*
 * @implemented
 */
DPI_AWARENESS_CONTEXT
WINAPI
GetThreadDpiAwarenessContext(VOID)
{
    return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
}

/*
 * @implemented
 */
DPI_AWARENESS_CONTEXT
WINAPI
SetThreadDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    if (!IsValidContext(context))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    /* The previous one */
    return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
}

/* The DPI hosting behavior of each thread (a TLS slot holds the value + 2,
 * so an unset slot reads as DPI_HOSTING_BEHAVIOR_DEFAULT) */
static DWORD HostingBehaviorTls = TLS_OUT_OF_INDEXES;

static DWORD
GetHostingBehaviorTls(VOID)
{
    DWORD Index = HostingBehaviorTls;

    if (Index == TLS_OUT_OF_INDEXES)
    {
        Index = TlsAlloc();
        if (Index == TLS_OUT_OF_INDEXES)
            return Index;
        if (InterlockedCompareExchange((LONG *)&HostingBehaviorTls, Index, TLS_OUT_OF_INDEXES) != TLS_OUT_OF_INDEXES)
        {
            TlsFree(Index);
            Index = HostingBehaviorTls;
        }
    }
    return Index;
}

/*
 * @implemented
 */
DPI_HOSTING_BEHAVIOR
WINAPI
GetThreadDpiHostingBehavior(VOID)
{
    DWORD Index = GetHostingBehaviorTls();
    ULONG_PTR Value;

    if (Index == TLS_OUT_OF_INDEXES)
        return DPI_HOSTING_BEHAVIOR_DEFAULT;
    Value = (ULONG_PTR)TlsGetValue(Index);
    return Value ? (DPI_HOSTING_BEHAVIOR)(Value - 2) : DPI_HOSTING_BEHAVIOR_DEFAULT;
}

/*
 * @implemented
 */
DPI_HOSTING_BEHAVIOR
WINAPI
SetThreadDpiHostingBehavior(
    _In_ DPI_HOSTING_BEHAVIOR value)
{
    DPI_HOSTING_BEHAVIOR Previous;
    DWORD Index;

    if (value != DPI_HOSTING_BEHAVIOR_DEFAULT && value != DPI_HOSTING_BEHAVIOR_MIXED)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return DPI_HOSTING_BEHAVIOR_INVALID;
    }
    Previous = GetThreadDpiHostingBehavior();
    Index = GetHostingBehaviorTls();
    if (Index != TLS_OUT_OF_INDEXES)
        TlsSetValue(Index, (PVOID)(ULONG_PTR)(value + 2));
    return Previous;
}

/*
 * @implemented
 */
DPI_HOSTING_BEHAVIOR
WINAPI
GetWindowDpiHostingBehavior(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return DPI_HOSTING_BEHAVIOR_INVALID;
    }
    /* No window is ever scaled: they all behave the same */
    return DPI_HOSTING_BEHAVIOR_DEFAULT;
}

/*
 * @implemented
 */
BOOL
WINAPI
GetPhysicalCursorPos(
    _Out_ LPPOINT lpPoint)
{
    /* Logical and physical coordinates are the same */
    return GetCursorPos(lpPoint);
}

/*
 * @implemented
 */
BOOL
WINAPI
SetPhysicalCursorPos(
    _In_ int X,
    _In_ int Y)
{
    /* Logical and physical coordinates are the same */
    return SetCursorPos(X, Y);
}

/*
 * @implemented
 */
BOOL
WINAPI
IsValidDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    return IsValidContext(context);
}

/*
 * @implemented
 */
DPI_AWARENESS
WINAPI
GetAwarenessFromDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    if (context == DPI_AWARENESS_CONTEXT_UNAWARE || context == DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED)
        return DPI_AWARENESS_UNAWARE;
    if (context == DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)
        return DPI_AWARENESS_SYSTEM_AWARE;
    if (context == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE || context == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        return DPI_AWARENESS_PER_MONITOR_AWARE;
    return DPI_AWARENESS_INVALID;
}

/*
 * @implemented
 */
BOOL
WINAPI
AreDpiAwarenessContextsEqual(
    _In_ DPI_AWARENESS_CONTEXT ctx1,
    _In_ DPI_AWARENESS_CONTEXT ctx2)
{
    return IsValidContext(ctx1) && ctx1 == ctx2;
}

/*
 * @implemented
 */
BOOL
WINAPI
EnableNonClientDpiScaling(
    _In_ HWND hwnd)
{
    return TRUE;
}

/*
 * @implemented
 */
int
WINAPI
GetSystemMetricsForDpi(
    _In_ int index,
    _In_ UINT dpi)
{
    UINT System = GetDpiForSystem();
    int Value = GetSystemMetrics(index);

    if (!dpi || dpi == System)
        return Value;

    /* The metrics that are sizes follow the DPI */
    switch (index)
    {
        case SM_CXVSCROLL: case SM_CYHSCROLL: case SM_CYCAPTION: case SM_CXBORDER:
        case SM_CYBORDER: case SM_CXDLGFRAME: case SM_CYDLGFRAME: case SM_CYVTHUMB:
        case SM_CXHTHUMB: case SM_CXICON: case SM_CYICON: case SM_CXCURSOR:
        case SM_CYCURSOR: case SM_CYMENU: case SM_CYVSCROLL: case SM_CXHSCROLL:
        case SM_CXMIN: case SM_CYMIN: case SM_CXSIZE: case SM_CYSIZE:
        case SM_CXFRAME: case SM_CYFRAME: case SM_CXMINTRACK: case SM_CYMINTRACK:
        case SM_CXICONSPACING: case SM_CYICONSPACING: case SM_CXSMICON: case SM_CYSMICON:
        case SM_CYSMCAPTION: case SM_CXSMSIZE: case SM_CYSMSIZE: case SM_CXMENUSIZE:
        case SM_CYMENUSIZE: case SM_CXMENUCHECK: case SM_CYMENUCHECK: case 92 /* SM_CXPADDEDBORDER */:
            return MulDiv(Value, dpi, System);
    }
    return Value;
}

/*
 * @implemented
 */
BOOL
WINAPI
AdjustWindowRectExForDpi(
    _Inout_ LPRECT rect,
    _In_ DWORD style,
    _In_ BOOL menu,
    _In_ DWORD exStyle,
    _In_ UINT dpi)
{
    return AdjustWindowRectEx(rect, style, menu, exStyle);
}

/*
 * @implemented
 */
BOOL
WINAPI
SystemParametersInfoForDpi(
    _In_ UINT action,
    _In_ UINT param,
    _Inout_ PVOID ptr,
    _In_ UINT winini,
    _In_ UINT dpi)
{
    return SystemParametersInfoW(action, param, ptr, winini);
}

/*
 * @implemented (no virtualized coordinates: nothing to convert)
 */
BOOL
WINAPI
LogicalToPhysicalPoint(
    _In_ HWND hwnd, 
    _Inout_ POINT *point )
{
    return TRUE;
}

/*
 * @implemented (no virtualized coordinates: nothing to convert)
 */
BOOL
WINAPI
PhysicalToLogicalPoint(
    _In_ HWND hwnd,
    _Inout_ POINT *point)
{
    return TRUE;
}

/*
 * @implemented (no per-monitor scaling: nothing to convert)
 */
BOOL
WINAPI
LogicalToPhysicalPointForPerMonitorDPI(
    _In_opt_ HWND hwnd,
    _Inout_ POINT *point)
{
    return TRUE;
}

/*
 * @implemented (no per-monitor scaling: nothing to convert)
 */
BOOL
WINAPI
PhysicalToLogicalPointForPerMonitorDPI(
    _In_opt_ HWND hwnd,
    _Inout_ POINT *point)
{
    return TRUE;
}

/*
 * @implemented (physical and logical coordinates are the same)
 */
HWND
WINAPI
WindowFromPhysicalPoint(
    _In_ POINT Point)
{
    return WindowFromPoint(Point);
}
