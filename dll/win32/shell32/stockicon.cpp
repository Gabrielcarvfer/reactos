/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Stock icons (Vista)
 *
 * SHGetStockIconInfo gives the standard icons programs draw (folders, drives,
 * the UAC shield...). Chromium, Firefox, Krita, Audacity, OBS and Wireshark
 * call it. The first stock icons are shell32's icons 1, 2, 3... in order;
 * the warning, information, error and shield icons are user32's.
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/* (shellapi.h has them only for Vista and later builds) */
typedef struct _K_SHSTOCKICONINFO
{
    DWORD cbSize;
    HICON hIcon;
    int iSysImageIndex;
    int iIcon;
    WCHAR szPath[MAX_PATH];
} K_SHSTOCKICONINFO;

#define K_SIID_SHIELD 77
#define K_SIID_WARNING 78
#define K_SIID_INFO 79
#define K_SIID_ERROR 80
#define K_SIID_MAX_ICONS 181

#define K_SHGSI_ICON 0x100
#define K_SHGSI_SYSICONINDEX 0x4000
#define K_SHGSI_LINKOVERLAY 0x8000
#define K_SHGSI_SELECTED 0x10000
#define K_SHGSI_SMALLICON 0x1
#define K_SHGSI_SHELLICONSIZE 0x4

EXTERN_C
HRESULT
WINAPI
SHGetStockIconInfo(
    _In_ UINT siid,
    _In_ UINT uFlags,
    _Inout_ K_SHSTOCKICONINFO *psii)
{
    HMODULE hModule = shell32_hInstance;
    UINT ResourceId, LocationId;
    int cx, cy;

    TRACE("(%u, 0x%x, %p)\n", siid, uFlags, psii);

    if (!psii || psii->cbSize != sizeof(*psii) || siid >= K_SIID_MAX_ICONS)
        return E_INVALIDARG;

    /* ResourceId is for LoadImage: user32 maps the IDI_ ids of its icons to its
       resources 101 (warning), 103 (error) and 104 (information), which are
       what the icon location must name (programs extract it from the file) */
    switch (siid)
    {
        case K_SIID_SHIELD: /* (no shield icon yet) */
        case K_SIID_WARNING: ResourceId = (UINT)(ULONG_PTR)IDI_WARNING; LocationId = 101; hModule = NULL; break;
        case K_SIID_INFO:    ResourceId = (UINT)(ULONG_PTR)IDI_INFORMATION; LocationId = 104; hModule = NULL; break;
        case K_SIID_ERROR:   ResourceId = (UINT)(ULONG_PTR)IDI_ERROR; LocationId = 103; hModule = NULL; break;
        default:
            ResourceId = siid + 1;
            if (!FindResourceW(shell32_hInstance, MAKEINTRESOURCEW(ResourceId), (LPCWSTR)RT_GROUP_ICON))
                ResourceId = IDI_SHELL_DOCUMENT;
            LocationId = ResourceId;
            break;
    }

    /* The icon's place: a negative index is a resource id */
    if (hModule)
    {
        GetModuleFileNameW(hModule, psii->szPath, _countof(psii->szPath));
    }
    else
    {
        GetSystemDirectoryW(psii->szPath, _countof(psii->szPath));
        StringCchCatW(psii->szPath, _countof(psii->szPath), L"\\user32.dll");
    }
    psii->iIcon = -(int)LocationId;
    psii->iSysImageIndex = -1;
    psii->hIcon = NULL;

    if (uFlags & K_SHGSI_SYSICONINDEX)
        psii->iSysImageIndex = SIC_GetIconIndex(psii->szPath, psii->iIcon, 0);

    if (uFlags & K_SHGSI_ICON)
    {
        if (uFlags & K_SHGSI_SMALLICON)
        {
            cx = GetSystemMetrics(SM_CXSMICON);
            cy = GetSystemMetrics(SM_CYSMICON);
        }
        else
        {
            cx = GetSystemMetrics(SM_CXICON);
            cy = GetSystemMetrics(SM_CYICON);
        }
        psii->hIcon = (HICON)LoadImageW(hModule, MAKEINTRESOURCEW(ResourceId), IMAGE_ICON, cx, cy,
                                        hModule ? 0 : LR_SHARED);
        if (!psii->hIcon)
            return HRESULT_FROM_WIN32(GetLastError());
        /* (A shared system icon must not be destroyed: give a copy) */
        if (!hModule)
            psii->hIcon = CopyIcon(psii->hIcon);
    }
    return S_OK;
}

/* Default programs (setting the default browser...): not supported */
EXTERN_C
HRESULT
WINAPI
SHCreateAssociationRegistration(
    _In_ REFIID riid,
    _Outptr_ void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    return E_NOTIMPL;
}
