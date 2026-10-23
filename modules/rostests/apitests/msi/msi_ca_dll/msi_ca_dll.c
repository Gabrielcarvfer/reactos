/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Custom action DLL for the msi custom action tests
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <windef.h>
#include <winbase.h>
#include <msiquery.h>

UINT WINAPI CA_Ok(MSIHANDLE hInstall)
{
    return ERROR_SUCCESS;
}

UINT WINAPI CA_Fail(MSIHANDLE hInstall)
{
    return ERROR_INSTALL_FAILURE;
}

/* What an MSVC C++ throw nobody catches does */
UINT WINAPI CA_Throw(MSIHANDLE hInstall)
{
    MsiSetPropertyA(hInstall, "BEFORE_THROW", "1");
    RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 0, NULL);
    MsiSetPropertyA(hInstall, "AFTER_THROW", "1");
    return ERROR_SUCCESS;
}

UINT WINAPI CA_AccessViolation(MSIHANDLE hInstall)
{
    MsiSetPropertyA(hInstall, "BEFORE_THROW", "1");
    *(volatile LONG *)NULL = 1;
    return ERROR_SUCCESS;
}
