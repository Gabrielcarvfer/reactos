/*
 * PROJECT:     ReactOS Magnification API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Magnification API of Windows 7 (magnification.dll)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * ReactOS can't magnify the screen or change its colors yet. The API is
 * there for the programs that link to it (the NVDA screen reader), and it
 * answers truthfully: the identity transform and color effect work, any
 * other one fails, so a program never believes the screen changed (NVDA's
 * screen curtain must never report a curtain it couldn't draw).
 * The "Magnifier" window class doesn't exist: the window functions fail.
 */

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <magnification.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(magnification);

static LONG InitCount;
static BOOL InputTransformEnabled;
static RECT InputSource, InputDest;

static BOOL IsIdentityColorEffect(const MAGCOLOREFFECT *Effect)
{
    int i, j;

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 5; j++)
        {
            if (Effect->transform[i][j] != (i == j ? 1.0f : 0.0f))
                return FALSE;
        }
    }
    return TRUE;
}

static BOOL NotSupported(void)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

static BOOL NoMagnifierWindow(void)
{
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
}

BOOL WINAPI MagInitialize(void)
{
    TRACE("()\n");
    InterlockedIncrement(&InitCount);
    return TRUE;
}

BOOL WINAPI MagUninitialize(void)
{
    TRACE("()\n");
    if (InterlockedDecrement(&InitCount) < 0)
        InterlockedExchange(&InitCount, 0);
    return TRUE;
}

BOOL WINAPI MagSetFullscreenTransform(float magLevel, int xOffset, int yOffset)
{
    TRACE("(%f, %d, %d)\n", magLevel, xOffset, yOffset);
    if (magLevel < 1.0f)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (magLevel == 1.0f && xOffset == 0 && yOffset == 0)
        return TRUE;
    return NotSupported();
}

BOOL WINAPI MagGetFullscreenTransform(float *pMagLevel, int *pxOffset, int *pyOffset)
{
    TRACE("(%p, %p, %p)\n", pMagLevel, pxOffset, pyOffset);
    if (!pMagLevel || !pxOffset || !pyOffset)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pMagLevel = 1.0f;
    *pxOffset = 0;
    *pyOffset = 0;
    return TRUE;
}

BOOL WINAPI MagSetFullscreenColorEffect(PMAGCOLOREFFECT pEffect)
{
    TRACE("(%p)\n", pEffect);
    if (!pEffect)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (IsIdentityColorEffect(pEffect))
        return TRUE;
    return NotSupported();
}

BOOL WINAPI MagGetFullscreenColorEffect(PMAGCOLOREFFECT pEffect)
{
    int i, j;

    TRACE("(%p)\n", pEffect);
    if (!pEffect)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 5; j++)
            pEffect->transform[i][j] = (i == j) ? 1.0f : 0.0f;
    }
    return TRUE;
}

BOOL WINAPI MagSetInputTransform(BOOL fEnabled, const RECT *pRectSource, const RECT *pRectDest)
{
    TRACE("(%d, %p, %p)\n", fEnabled, pRectSource, pRectDest);
    if (fEnabled && (!pRectSource || !pRectDest))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (fEnabled && !EqualRect(pRectSource, pRectDest))
        return NotSupported();

    InputTransformEnabled = fEnabled;
    if (fEnabled)
    {
        InputSource = *pRectSource;
        InputDest = *pRectDest;
    }
    return TRUE;
}

BOOL WINAPI MagGetInputTransform(BOOL *pfEnabled, RECT *pRectSource, RECT *pRectDest)
{
    TRACE("(%p, %p, %p)\n", pfEnabled, pRectSource, pRectDest);
    if (!pfEnabled || !pRectSource || !pRectDest)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pfEnabled = InputTransformEnabled;
    *pRectSource = InputSource;
    *pRectDest = InputDest;
    return TRUE;
}

BOOL WINAPI MagShowSystemCursor(BOOL fShowCursor)
{
    TRACE("(%d)\n", fShowCursor);
    /* The cursor is always shown (nothing can hide it for the whole system) */
    return fShowCursor ? TRUE : NotSupported();
}

BOOL WINAPI MagSetWindowSource(HWND hwnd, RECT rect)
{
    TRACE("(%p, %s)\n", hwnd, wine_dbgstr_rect(&rect));
    return NoMagnifierWindow();
}

BOOL WINAPI MagGetWindowSource(HWND hwnd, RECT *pRect)
{
    TRACE("(%p, %p)\n", hwnd, pRect);
    return NoMagnifierWindow();
}

BOOL WINAPI MagSetWindowTransform(HWND hwnd, PMAGTRANSFORM pTransform)
{
    TRACE("(%p, %p)\n", hwnd, pTransform);
    return NoMagnifierWindow();
}

BOOL WINAPI MagGetWindowTransform(HWND hwnd, PMAGTRANSFORM pTransform)
{
    TRACE("(%p, %p)\n", hwnd, pTransform);
    return NoMagnifierWindow();
}

BOOL WINAPI MagSetWindowFilterList(HWND hwnd, DWORD dwFilterMode, int count, HWND *pHWND)
{
    TRACE("(%p, %lu, %d, %p)\n", hwnd, dwFilterMode, count, pHWND);
    return NoMagnifierWindow();
}

int WINAPI MagGetWindowFilterList(HWND hwnd, DWORD *pdwFilterMode, int count, HWND *pHWND)
{
    TRACE("(%p, %p, %d, %p)\n", hwnd, pdwFilterMode, count, pHWND);
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return -1;
}

BOOL WINAPI MagSetColorEffect(HWND hwnd, PMAGCOLOREFFECT pEffect)
{
    TRACE("(%p, %p)\n", hwnd, pEffect);
    return NoMagnifierWindow();
}

BOOL WINAPI MagGetColorEffect(HWND hwnd, PMAGCOLOREFFECT pEffect)
{
    TRACE("(%p, %p)\n", hwnd, pEffect);
    return NoMagnifierWindow();
}
