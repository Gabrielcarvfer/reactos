/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for SystemParametersInfo function family
 * PROGRAMMERS:     Giannis Adamopoulos
 */

#include "precomp.h"
#include <versionhelpers.h>

static HWND hWnd1, hWnd2;

static BOOL g_bReadyForDisplayChange = FALSE;
static HANDLE g_hSemDisplayChange;

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef SPI_GETLOGICALDPIOVERRIDE
#define SPI_GETLOGICALDPIOVERRIDE 0x009E
#define SPI_SETLOGICALDPIOVERRIDE 0x009F
#endif
static UINT g_DpiChangedCount;
static WPARAM g_DpiChangedWParam;
static RECT g_DpiChangedRect;

/* FIXME: test for HWND_TOP, etc...*/
static int get_iwnd(HWND hWnd)
{
    if(hWnd == hWnd1) return 1;
    else if(hWnd == hWnd2) return 2;
    else return 0;
}

LRESULT CALLBACK SysParamsTestProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int iwnd = get_iwnd(hWnd);

    if(message > WM_USER || !iwnd || IsDWmMsg(message) || IseKeyMsg(message))
        return DefWindowProc(hWnd, message, wParam, lParam);

    switch(message)
    {
    case WM_GETICON :
    case WM_SETICON:
        break;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS* pwp = (WINDOWPOS*)lParam;
            ok(wParam==0,"expected wParam=0\n");
            RECORD_MESSAGE(iwnd, message, SENT, get_iwnd(pwp->hwndInsertAfter), pwp->flags);
            break;
        }
    case WM_DISPLAYCHANGE:
        if (g_bReadyForDisplayChange)
        {
            g_bReadyForDisplayChange = FALSE;
            ReleaseSemaphore(g_hSemDisplayChange, 1, 0);
        }
        break;
    default:
        RECORD_MESSAGE(iwnd, message, SENT, 0,0);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static void FlushMessages()
{
    MSG msg;

    while (PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
    {
        int iwnd = get_iwnd(msg.hwnd);
        if(!(msg.message > WM_USER || !iwnd || IsDWmMsg(msg.message) || IseKeyMsg(msg.message)))
            RECORD_MESSAGE(iwnd, msg.message, POST,0,0);
        DispatchMessageA( &msg );
    }
}

static MSG_ENTRY NcMetricsChange_chain[]={
    {2,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {2,WM_GETMINMAXINFO},
    {2,WM_NCCALCSIZE},
    {2,WM_WINDOWPOSCHANGED, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOSIZE},
    {1,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {1,WM_GETMINMAXINFO},
    {1,WM_NCCALCSIZE},
    {1,WM_WINDOWPOSCHANGED, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOSIZE},
    {0,0}};

static MSG_ENTRY NcMetricsChange1_chain[]={
    {2,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {2,WM_GETMINMAXINFO},
    {2,WM_NCCALCSIZE},
    {2,WM_WINDOWPOSCHANGED, SENT, 0,  SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOSIZE},
    {1,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {1,WM_GETMINMAXINFO},
    {1,WM_NCCALCSIZE},
    {1,WM_WINDOWPOSCHANGED, SENT, 0,  SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOSIZE},
    {2,WM_SETTINGCHANGE},
    {1,WM_SETTINGCHANGE},
    {0,0}};

static MSG_ENTRY CaptionHeight_chain[]={
    {2,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {2,WM_GETMINMAXINFO},
    {2,WM_NCCALCSIZE},
    {2,WM_WINDOWPOSCHANGED, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE},
    {2,WM_MOVE},
    {1,WM_WINDOWPOSCHANGING, SENT, 0, SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW},
    {1,WM_GETMINMAXINFO},
    {1,WM_NCCALCSIZE},
    {1,WM_WINDOWPOSCHANGED, SENT, 0,  SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOREDRAW | SWP_NOCLIENTSIZE},
    {1,WM_MOVE},
    {0,0}};

static void Test_NonClientMetrics()
{
    NONCLIENTMETRICS NonClientMetrics;

    /* WARNING: this test requires themes and dwm to be disabled */

    SetCursorPos(0,0);

    /* Retrieve th non client metrics */
    NonClientMetrics.cbSize = sizeof(NONCLIENTMETRICS);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, 0);
    FlushMessages();
    COMPARE_CACHE(empty_chain);

    /* Set the non client metric without making any change */
    SystemParametersInfo(SPI_SETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, 0);
    FlushMessages();
    COMPARE_CACHE(NcMetricsChange_chain);

    /* Set the same metrics again with the SPIF_SENDCHANGE param */
    SystemParametersInfo(SPI_SETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, SPIF_SENDCHANGE|SPIF_UPDATEINIFILE );
    FlushMessages();
    COMPARE_CACHE(NcMetricsChange1_chain);

    /* Slightly change the caption height */
    NonClientMetrics.iCaptionHeight += 1;
    SystemParametersInfo(SPI_SETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, 0);
    FlushMessages();
    COMPARE_CACHE(CaptionHeight_chain);

    /* Restore the original caption height */
    NonClientMetrics.iCaptionHeight -= 1;
    SystemParametersInfo(SPI_SETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, 0);
    FlushMessages();
    COMPARE_CACHE(CaptionHeight_chain);
}

static void Test_MouseSpeed()
{
    ULONG ulMouseSpeed, temp;
    BOOL ret;

    ret = SystemParametersInfo(SPI_GETMOUSESPEED, 0, &ulMouseSpeed, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(ulMouseSpeed >= 1 && ulMouseSpeed <=20, "Wrong mouse speed (%d)\n", (int)ulMouseSpeed);

    temp = 1;
    ret = SystemParametersInfo(SPI_SETMOUSESPEED, 0, UlongToPtr(temp), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
    ok(ret, "SystemParametersInfo failed\n");
    ret = SystemParametersInfo(SPI_GETMOUSESPEED, 0, &temp, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(temp == 1, "SPI_GETMOUSESPEED did not get value set by SPI_SETMOUSESPEED (%d instead of 1)\n", (int)temp);

    temp = 20;
    ret = SystemParametersInfo(SPI_SETMOUSESPEED, 0, UlongToPtr(temp), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
    ok(ret, "SystemParametersInfo failed\n");
    ret = SystemParametersInfo(SPI_GETMOUSESPEED, 0, &temp, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(temp == 20, "SPI_GETMOUSESPEED did not get value set by SPI_SETMOUSESPEED (%d instead of 20)\n", (int)temp);

    temp = 21;
    ret = SystemParametersInfo(SPI_SETMOUSESPEED, 0, UlongToPtr(temp), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
    ok(!ret, "expected SystemParametersInfo to fail\n");
    ret = SystemParametersInfo(SPI_GETMOUSESPEED, 0, &temp, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(temp == 20, "SPI_GETMOUSESPEED got unexpected value (%d instead of 20)\n", (int)temp);

    temp = 0;
    ret = SystemParametersInfo(SPI_SETMOUSESPEED, 0, UlongToPtr(temp), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
    ok(!ret, "expected SystemParametersInfo to fail\n");
    ret = SystemParametersInfo(SPI_GETMOUSESPEED, 0, &temp, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(temp == 20, "SPI_GETMOUSESPEED got unexpected value (%d instead of 20)\n", (int)temp);

    ret = SystemParametersInfo(SPI_SETMOUSESPEED, 0, UlongToPtr(ulMouseSpeed), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
    ok(ret, "SystemParametersInfo failed\n");
}

static void Test_GradientCaptions(void)
{
    BOOL ret;
    LONG lResult;
    DWORD dwResult;
    BOOL bGradientCaptions, bModeFound;
    DEVMODEW OldDevMode, NewDevMode;
    INT iMode;

    ret = SystemParametersInfo(SPI_GETGRADIENTCAPTIONS, 0, &bGradientCaptions, 0);
    ok(ret, "SystemParametersInfo failed\n");
    if (bGradientCaptions == FALSE)
    {
        skip("GRADIENTCAPTIONS value has changed from its original state\n");
        return;
    }

    /* Try to find a graphics mode with 16 or 256 colors */
    iMode = 0;
    bModeFound = FALSE;
    while (EnumDisplaySettingsW(NULL, iMode, &NewDevMode))
    {
        if ((NewDevMode.dmBitsPerPel == 4) ||
            (NewDevMode.dmBitsPerPel == 8))
        {
            bModeFound = TRUE;
            break;
        }

        ++iMode;
    }
    if (!bModeFound)
    {
        skip("4bpp/8bpp graphics mode is not supported\n");
        return;
    }

    /* Save the current graphics mode */
    ret = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &OldDevMode);
    ok(ret, "EnumDisplaySettingsW failed\n");

    g_hSemDisplayChange = CreateSemaphoreW(NULL, 0, 1, NULL);

    /* Switch to the new graphics mode */
    g_bReadyForDisplayChange = TRUE;
    lResult = ChangeDisplaySettingsExW(NULL, &NewDevMode, NULL, 0, NULL);
    if (lResult == DISP_CHANGE_SUCCESSFUL)
    {
        dwResult = WaitForSingleObject(g_hSemDisplayChange, 10000);
        ok(dwResult == WAIT_OBJECT_0, "Waiting for the WM_DISPLAYCHANGE message timed out\n");
    }
    g_bReadyForDisplayChange = FALSE;
    ok(lResult == DISP_CHANGE_SUCCESSFUL, "ChangeDisplaySettingsExW returned %ld\n", lResult);

    /* SPI_GETGRADIENTCAPTIONS will now always return FALSE */
    ret = SystemParametersInfo(SPI_GETGRADIENTCAPTIONS, 0, &bGradientCaptions, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(bGradientCaptions == FALSE, "SPI_GETGRADIENTCAPTIONS got unexpected value (%d instead of 0)\n", bGradientCaptions);

    /* Enable gradient captions */
    bGradientCaptions = TRUE;
    SystemParametersInfo(SPI_SETGRADIENTCAPTIONS, 0, UlongToPtr(bGradientCaptions), 0);

    /* Still FALSE */
    ret = SystemParametersInfo(SPI_GETGRADIENTCAPTIONS, 0, &bGradientCaptions, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(bGradientCaptions == FALSE, "SPI_GETGRADIENTCAPTIONS got unexpected value (%d instead of 0)\n", bGradientCaptions);

    /* Restore the previous graphics mode */
    g_bReadyForDisplayChange = TRUE;
    lResult = ChangeDisplaySettingsExW(NULL, &OldDevMode, NULL, 0, NULL);
    if (lResult == DISP_CHANGE_SUCCESSFUL)
    {
        dwResult = WaitForSingleObject(g_hSemDisplayChange, 10000);
        ok(dwResult == WAIT_OBJECT_0, "Waiting for the WM_DISPLAYCHANGE message timed out\n");
    }
    g_bReadyForDisplayChange = FALSE;
    ok(lResult == DISP_CHANGE_SUCCESSFUL, "ChangeDisplaySettingsExW returned %ld\n", lResult);

    /* The original value should be restored */
    ret = SystemParametersInfo(SPI_GETGRADIENTCAPTIONS, 0, &bGradientCaptions, 0);
    ok(ret, "SystemParametersInfo failed\n");
    ok(bGradientCaptions == TRUE, "SPI_GETGRADIENTCAPTIONS got unexpected value (%d instead of 1)\n", bGradientCaptions);

    CloseHandle(g_hSemDisplayChange);
}

static UINT GetScreenDpi(void)
{
    HDC hdc = GetDC(NULL);
    UINT Dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    return Dpi;
}

/*
 * SPI_GETLOGICALDPIOVERRIDE / SPI_SETLOGICALDPIOVERRIDE (undocumented): the
 * display scale as steps relative to the recommended scale, in the list
 * 100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500 (%).
 * GET gives the step of 100% (minus the recommended index), SET takes a step.
 * On Windows, most applications only see a new scale after signing out again;
 * ReactOS applies it at once and sends WM_DPICHANGED to top-level windows.
 */
static LRESULT CALLBACK DpiTestProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DPICHANGED)
    {
        g_DpiChangedCount++;
        g_DpiChangedWParam = wParam;
        if (lParam)
            g_DpiChangedRect = *(RECT *)lParam;
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static void Test_LogicalDpiOverride(void)
{
    HWND hWndDpi;
    static const UINT Scales[] = { 100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500 };
    INT iMinStep = 1, iMinStep2 = 1, iCurrent = -1, iOther;
    UINT OldDpi, NewDpi, i;
    RECT rcWindow;
    BOOL ret;
    MSG msg;

    ret = SystemParametersInfoW(SPI_GETLOGICALDPIOVERRIDE, 0, &iMinStep, 0);
    if (!ret)
    {
        skip("SPI_GETLOGICALDPIOVERRIDE is not supported\n");
        return;
    }
    ok(iMinStep <= 0 && iMinStep > -(INT)_countof(Scales), "GET returned step %d\n", iMinStep);

    /* Find the current scale in the list */
    OldDpi = GetScreenDpi();
    for (i = 0; i < _countof(Scales); i++)
    {
        if (Scales[i] * 96 / 100 == OldDpi)
            iCurrent = (INT)i;
    }
    if (iCurrent < 0)
    {
        skip("The current DPI %u is not one of the scales\n", OldDpi);
        return;
    }

    /* One step up (or down at the top of the list) */
    iOther = (iCurrent + 1 < (INT)_countof(Scales)) ? iCurrent + 1 : iCurrent - 1;
    g_DpiChangedCount = 0;
    SetRectEmpty(&g_DpiChangedRect);
    /* WM_DPICHANGED goes to visible top-level windows */
    RegisterSimpleClass(DpiTestProc, L"dpitest");
    hWndDpi = CreateWindowW(L"dpitest", L"dpitest", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            40, 40, 300, 200, NULL, NULL, 0, NULL);
    ok(hWndDpi != NULL, "CreateWindowW failed\n");
    GetWindowRect(hWndDpi, &rcWindow);
    ret = SystemParametersInfoW(SPI_SETLOGICALDPIOVERRIDE, (UINT)(iMinStep + iOther), NULL, 0);
    ok(ret, "SET step %d failed (error %lu)\n", iMinStep + iOther, GetLastError());

    /* GET still gives the step of 100% */
    ret = SystemParametersInfoW(SPI_GETLOGICALDPIOVERRIDE, 0, &iMinStep2, 0);
    ok(ret, "GET failed\n");
    ok(iMinStep2 == iMinStep, "GET returned %d after SET, expected %d\n", iMinStep2, iMinStep);

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);

    NewDpi = GetScreenDpi();
    if (NewDpi == OldDpi)
    {
        /* Windows: the new scale is used after signing out */
        skip("The system DPI did not change at once\n");
    }
    else
    {
        ok(NewDpi == Scales[iOther] * 96 / 100, "DPI is %u, expected %u\n", NewDpi, Scales[iOther] * 96 / 100);
        ok(g_DpiChangedCount >= 1, "WM_DPICHANGED was not received\n");
        ok(LOWORD(g_DpiChangedWParam) == NewDpi && HIWORD(g_DpiChangedWParam) == NewDpi,
           "WM_DPICHANGED wParam is 0x%Ix, expected both words %u\n", g_DpiChangedWParam, NewDpi);
        /* The suggested rect keeps the top-left corner and scales the size */
        ok(g_DpiChangedRect.left == rcWindow.left && g_DpiChangedRect.top == rcWindow.top,
           "Suggested rect moved to %ld,%ld\n", g_DpiChangedRect.left, g_DpiChangedRect.top);
        ok(g_DpiChangedRect.right - g_DpiChangedRect.left ==
           MulDiv(rcWindow.right - rcWindow.left, NewDpi, OldDpi),
           "Suggested width %ld, expected %d\n", g_DpiChangedRect.right - g_DpiChangedRect.left,
           MulDiv(rcWindow.right - rcWindow.left, NewDpi, OldDpi));
    }

    /* A step outside of the list is refused (ReactOS; not verified on Windows) */
    if (IsReactOS())
    {
        ret = SystemParametersInfoW(SPI_SETLOGICALDPIOVERRIDE, (UINT)(iMinStep + (INT)_countof(Scales)), NULL, 0);
        ok(!ret, "SET with an out of range step succeeded\n");
    }

    /* Back to the original scale */
    ret = SystemParametersInfoW(SPI_SETLOGICALDPIOVERRIDE, (UINT)(iMinStep + iCurrent), NULL, 0);
    ok(ret, "Restoring the scale failed\n");
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
    if (NewDpi != OldDpi)
        ok(GetScreenDpi() == OldDpi, "DPI is %u after restoring, expected %u\n", GetScreenDpi(), OldDpi);
    DestroyWindow(hWndDpi);
    UnregisterClassW(L"dpitest", 0);
}

START_TEST(SystemParametersInfo)
{
    RegisterSimpleClass(SysParamsTestProc, L"sysparamstest");
    hWnd1 = CreateWindowW(L"sysparamstest", L"sysparamstest", WS_OVERLAPPEDWINDOW,
                        20, 20, 300, 300, NULL, NULL, 0, NULL);

    hWnd2 = CreateWindowW(L"sysparamstest", L"sysparamstest", WS_OVERLAPPEDWINDOW,
                         200, 200, 300, 300, NULL, NULL, 0, NULL);

    Test_NonClientMetrics();
    Test_MouseSpeed();
    Test_GradientCaptions();
    Test_LogicalDpiOverride();

    DestroyWindow(hWnd1);
    DestroyWindow(hWnd2);
    UnregisterClassW(L"sysparamstest", 0);
}
