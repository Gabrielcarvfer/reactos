/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Timer functions of Windows 8
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winuser.h>

/* (user32's SetTimer is this system call) */
UINT_PTR
APIENTRY
NtUserSetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc);

/*
 * SetCoalescableTimer: SetTimer with a delay the system may add to group
 * timers together and save power. ReactOS fires timers on time.
 *
 * @implemented
 */
UINT_PTR
WINAPI
SetCoalescableTimer(
    _In_opt_ HWND hWnd,
    _In_ UINT_PTR nIDEvent,
    _In_ UINT uElapse,
    _In_opt_ TIMERPROC lpTimerFunc,
    _In_ ULONG uToleranceDelay)
{
    UNREFERENCED_PARAMETER(uToleranceDelay);
    return NtUserSetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
}
