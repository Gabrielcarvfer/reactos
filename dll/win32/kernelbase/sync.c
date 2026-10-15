/*
 * PROJECT:     ReactOS Kernel Base
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     WaitOnAddress (api-ms-win-core-synch-l1-2-0; not in kernel32, as on Windows)
 */

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"

BOOL
WINAPI
WaitOnAddress(
    _In_ volatile VOID *Address,
    _In_ PVOID CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ DWORD dwMilliseconds)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    if (dwMilliseconds != INFINITE)
        Timeout.QuadPart = -(LONGLONG)dwMilliseconds * 10000;

    Status = RtlWaitOnAddress((const void *)Address, CompareAddress, AddressSize,
                              (dwMilliseconds != INFINITE) ? &Timeout : NULL);
    if (Status != STATUS_SUCCESS)
    {
        /* STATUS_TIMEOUT included */
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}
