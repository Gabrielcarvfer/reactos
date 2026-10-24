/*
 * PROJECT:     ReactOS Profile API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     User profile helpers (profapi.dll)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winerror.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(profapi);

/*
 * Ordinal 114: opens a registry key of a package (MSIX/AppX) family with the
 * given access. The Windows App SDK's resource loader (Microsoft.Internal.
 * FrameworkUdk.dll, PowerToys) imports it and calls it only for packaged
 * programs. ReactOS has no packages, so there is nothing to open.
 */
HRESULT
WINAPI
ProfApiOpenPackageKey(
    _In_ PCWSTR PackageFamilyName,
    _In_opt_ PVOID Reserved,
    _In_opt_ PCWSTR SubKey,
    _In_ REGSAM Access,
    _Out_ PHKEY Key)
{
    FIXME("(%s, %p, %s, 0x%lx, %p): no packages\n", debugstr_w(PackageFamilyName), Reserved,
          debugstr_w(SubKey), Access, Key);
    if (Key)
        *Key = NULL;
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}
