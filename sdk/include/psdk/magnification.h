/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Magnification API (Windows 7)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#ifndef _MAGNIFICATION_H_
#define _MAGNIFICATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#define WC_MAGNIFIERA "Magnifier"
#define WC_MAGNIFIERW L"Magnifier"
#ifdef UNICODE
#define WC_MAGNIFIER WC_MAGNIFIERW
#else
#define WC_MAGNIFIER WC_MAGNIFIERA
#endif

#define MS_SHOWMAGNIFIEDCURSOR 0x0001
#define MS_CLIPAROUNDCURSOR    0x0002
#define MS_INVERTCOLORS        0x0004

#define MW_FILTERMODE_EXCLUDE 0
#define MW_FILTERMODE_INCLUDE 1

typedef struct tagMAGTRANSFORM
{
    float v[3][3];
} MAGTRANSFORM, *PMAGTRANSFORM;

typedef struct tagMAGIMAGEHEADER
{
    UINT width;
    UINT height;
    GUID format; /* WICPixelFormatGUID */
    UINT stride;
    UINT offset;
    SIZE_T cbSize;
} MAGIMAGEHEADER, *PMAGIMAGEHEADER;

typedef struct tagMAGCOLOREFFECT
{
    float transform[5][5];
} MAGCOLOREFFECT, *PMAGCOLOREFFECT;

BOOL WINAPI MagInitialize(void);
BOOL WINAPI MagUninitialize(void);
BOOL WINAPI MagSetWindowSource(HWND hwnd, RECT rect);
BOOL WINAPI MagGetWindowSource(HWND hwnd, RECT *pRect);
BOOL WINAPI MagSetWindowTransform(HWND hwnd, PMAGTRANSFORM pTransform);
BOOL WINAPI MagGetWindowTransform(HWND hwnd, PMAGTRANSFORM pTransform);
BOOL WINAPI MagSetWindowFilterList(HWND hwnd, DWORD dwFilterMode, int count, HWND *pHWND);
int WINAPI MagGetWindowFilterList(HWND hwnd, DWORD *pdwFilterMode, int count, HWND *pHWND);
BOOL WINAPI MagSetColorEffect(HWND hwnd, PMAGCOLOREFFECT pEffect);
BOOL WINAPI MagGetColorEffect(HWND hwnd, PMAGCOLOREFFECT pEffect);
BOOL WINAPI MagSetFullscreenTransform(float magLevel, int xOffset, int yOffset);
BOOL WINAPI MagGetFullscreenTransform(float *pMagLevel, int *pxOffset, int *pyOffset);
BOOL WINAPI MagSetFullscreenColorEffect(PMAGCOLOREFFECT pEffect);
BOOL WINAPI MagGetFullscreenColorEffect(PMAGCOLOREFFECT pEffect);
BOOL WINAPI MagSetInputTransform(BOOL fEnabled, const RECT *pRectSource, const RECT *pRectDest);
BOOL WINAPI MagGetInputTransform(BOOL *pfEnabled, RECT *pRectSource, RECT *pRectDest);
BOOL WINAPI MagShowSystemCursor(BOOL fShowCursor);

#ifdef __cplusplus
}
#endif

#endif /* _MAGNIFICATION_H_ */
