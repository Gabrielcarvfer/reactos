/*
 * PROJECT:     ReactOS Configuration Manager Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Configuration Manager functions of Windows 8 and later
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winreg.h>
#include <cfgmgr32.h>

/*
 * @implemented
 */
DWORD
WINAPI
CM_MapCrToWin32Err(
    _In_ CONFIGRET CmReturnCode,
    _In_ DWORD DefaultErr)
{
    switch (CmReturnCode)
    {
        case CR_SUCCESS:
            return ERROR_SUCCESS;

        case CR_OUT_OF_MEMORY:
            return ERROR_NOT_ENOUGH_MEMORY;

        case CR_INVALID_POINTER:
            return ERROR_INVALID_USER_BUFFER;

        case CR_INVALID_FLAG:
            return ERROR_INVALID_FLAGS;

        case CR_INVALID_DEVNODE:
        case CR_INVALID_DEVICE_ID:
        case CR_INVALID_MACHINENAME:
        case CR_INVALID_PROPERTY:
        case CR_INVALID_REFERENCE_STRING:
            return ERROR_INVALID_DATA;

        case CR_NO_SUCH_DEVNODE:
        case CR_NO_SUCH_VALUE:
        case CR_NO_SUCH_DEVICE_INTERFACE:
            return ERROR_NOT_FOUND;

        case CR_NO_SUCH_REGISTRY_KEY:
            return ERROR_FILE_NOT_FOUND;

        case CR_ALREADY_SUCH_DEVNODE:
            return ERROR_ALREADY_EXISTS;

        case CR_BUFFER_SMALL:
            return ERROR_INSUFFICIENT_BUFFER;

        case CR_NO_REGISTRY_HANDLE:
            return ERROR_INVALID_HANDLE;

        case CR_REGISTRY_ERROR:
            return ERROR_REGISTRY_CORRUPT;

        case CR_REMOTE_COMM_FAILURE:
        case CR_MACHINE_UNAVAILABLE:
        case CR_NO_CM_SERVICES:
            return ERROR_SERVICE_NOT_ACTIVE;

        case CR_ACCESS_DENIED:
            return ERROR_ACCESS_DENIED;

        case CR_CALL_NOT_IMPLEMENTED:
            return ERROR_CALL_NOT_IMPLEMENTED;

        default:
            return DefaultErr;
    }
}
