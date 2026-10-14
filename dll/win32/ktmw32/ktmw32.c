/*
 * PROJECT:     ReactOS Kernel Transaction Manager API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The ktmw32.dll functions that programs import
 *
 * ReactOS has no kernel transaction manager: the "transacted" file and
 * registry functions apply their changes at once. A transaction is a plain
 * handle that the program can close; committing it succeeds (everything is
 * already done), rolling it back fails (nothing can be undone).
 */

#include <windef.h>
#include <winbase.h>

HANDLE
WINAPI
CreateTransaction(
    _In_opt_ LPSECURITY_ATTRIBUTES lpTransactionAttributes,
    _In_opt_ LPGUID UOW,
    _In_opt_ DWORD CreateOptions,
    _In_opt_ DWORD IsolationLevel,
    _In_opt_ DWORD IsolationFlags,
    _In_opt_ DWORD Timeout,
    _In_opt_ LPWSTR Description)
{
    HANDLE Transaction;

    UNREFERENCED_PARAMETER(UOW);
    UNREFERENCED_PARAMETER(CreateOptions);
    UNREFERENCED_PARAMETER(IsolationLevel);
    UNREFERENCED_PARAMETER(IsolationFlags);
    UNREFERENCED_PARAMETER(Timeout);
    UNREFERENCED_PARAMETER(Description);

    /* Any object: the program only waits on it and closes it */
    Transaction = CreateEventW(lpTransactionAttributes, TRUE, FALSE, NULL);
    return Transaction ? Transaction : INVALID_HANDLE_VALUE;
}

HANDLE
WINAPI
OpenTransaction(
    _In_ DWORD dwDesiredAccess,
    _In_ LPGUID TransactionId)
{
    UNREFERENCED_PARAMETER(dwDesiredAccess);
    UNREFERENCED_PARAMETER(TransactionId);
    SetLastError(ERROR_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
}

BOOL
WINAPI
CommitTransaction(
    _In_ HANDLE TransactionHandle)
{
    if (!TransactionHandle || TransactionHandle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SetEvent(TransactionHandle);
    return TRUE;
}

BOOL
WINAPI
CommitTransactionAsync(
    _In_ HANDLE TransactionHandle)
{
    return CommitTransaction(TransactionHandle);
}

BOOL
WINAPI
RollbackTransaction(
    _In_ HANDLE TransactionHandle)
{
    UNREFERENCED_PARAMETER(TransactionHandle);
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL
WINAPI
RollbackTransactionAsync(
    _In_ HANDLE TransactionHandle)
{
    return RollbackTransaction(TransactionHandle);
}

BOOL
WINAPI
GetTransactionId(
    _In_ HANDLE TransactionHandle,
    _Out_ LPGUID TransactionId)
{
    if (!TransactionId)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNREFERENCED_PARAMETER(TransactionHandle);
    ZeroMemory(TransactionId, sizeof(*TransactionId));
    return TRUE;
}
