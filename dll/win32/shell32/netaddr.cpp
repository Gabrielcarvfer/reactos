/*
 * PROJECT:     shell32
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Network address control (Vista)
 *
 * InitNetworkAddressControl registers the "msctls_netaddress" window class, an
 * edit box where an address (IP, host name, URL) is typed. Here it is a plain
 * edit box: dialogs that use it get their text, without the address checks.
 * MPC-HC calls it when it starts.
 */

#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

static const WCHAR NetAddressClass[] = L"msctls_netaddress";

EXTERN_C
BOOL
WINAPI
InitNetworkAddressControl(VOID)
{
    WNDCLASSEXW wc = { sizeof(wc) };

    if (GetClassInfoExW(shell32_hInstance, NetAddressClass, &wc))
        return TRUE;

    /* The class of an edit box, under the new name */
    if (!GetClassInfoExW(NULL, WC_EDITW, &wc))
        return FALSE;
    wc.cbSize = sizeof(wc);
    wc.hInstance = shell32_hInstance;
    wc.lpszClassName = NetAddressClass;
    wc.style |= CS_GLOBALCLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        ERR("RegisterClassExW failed: %lu\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}
