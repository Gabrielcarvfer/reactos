/*
 * PROJECT:     ReactOS Windows.Data.Pdf
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.Data.Pdf runtime classes and PdfCreateRenderer
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * ReactOS has no PDF engine yet. Programs that import this DLL (PdfCraft)
 * must still start, so the entry points exist and report that PDF
 * rendering is not available, as on a Windows edition without the feature.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "winstring.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(pdf);

/* windows.data.pdf.interop.h: HRESULT PdfCreateRenderer(IDXGIDevice *, IPdfRendererNative **) */
HRESULT WINAPI PdfCreateRenderer(IUnknown *device, void **renderer)
{
    FIXME("(%p, %p): no PDF engine\n", device, renderer);
    if (renderer)
        *renderer = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IUnknown **factory)
{
    FIXME("(%s, %p): no PDF engine\n", debugstr_hstring(classid), factory);
    if (factory)
        *factory = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}
