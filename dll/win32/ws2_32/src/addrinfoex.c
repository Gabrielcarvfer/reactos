/*
 * PROJECT:     ReactOS WinSock 2 API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     GetAddrInfoEx (Vista, asynchronous since Windows 8)
 *
 * Built on GetAddrInfoW. ReactOS has no asynchronous name resolution: a
 * request with an OVERLAPPED is answered at once, then completed the way an
 * asynchronous one completes (its event is set, its completion routine
 * runs on a worker thread), and there is nothing left to cancel.
 */

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#define WIN32_NO_STATUS
#define WINSOCK_API_LINKAGE
#include <windef.h>
#include <winbase.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define NDEBUG
#include <debug.h>

static
PADDRINFOEXW
CopyAddrInfoToEx(PADDRINFOW Info)
{
    PADDRINFOEXW Head = NULL, *Next = &Head;

    for (; Info; Info = Info->ai_next)
    {
        SIZE_T NameLength = Info->ai_canonname ? (wcslen(Info->ai_canonname) + 1) * sizeof(WCHAR) : 0;
        PADDRINFOEXW Ex = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    sizeof(*Ex) + Info->ai_addrlen + NameLength);
        if (!Ex)
        {
            FreeAddrInfoExW(Head);
            return NULL;
        }
        Ex->ai_flags = Info->ai_flags;
        Ex->ai_family = Info->ai_family;
        Ex->ai_socktype = Info->ai_socktype;
        Ex->ai_protocol = Info->ai_protocol;
        Ex->ai_addrlen = Info->ai_addrlen;
        if (Info->ai_addr && Info->ai_addrlen)
        {
            Ex->ai_addr = (struct sockaddr *)(Ex + 1);
            memcpy(Ex->ai_addr, Info->ai_addr, Info->ai_addrlen);
        }
        if (NameLength)
        {
            Ex->ai_canonname = (PWSTR)((PBYTE)(Ex + 1) + Info->ai_addrlen);
            memcpy(Ex->ai_canonname, Info->ai_canonname, NameLength);
        }
        *Next = Ex;
        Next = &Ex->ai_next;
    }
    return Head;
}

typedef struct _ADDRINFOEX_COMPLETION
{
    LPLOOKUPSERVICE_COMPLETION_ROUTINE Routine;
    DWORD Error;
    LPOVERLAPPED Overlapped;
} ADDRINFOEX_COMPLETION, *PADDRINFOEX_COMPLETION;

static
DWORD
WINAPI
CompletionWorker(PVOID Parameter)
{
    PADDRINFOEX_COMPLETION Completion = Parameter;
    Completion->Routine(Completion->Error, 0, Completion->Overlapped);
    HeapFree(GetProcessHeap(), 0, Completion);
    return 0;
}

INT
WSAAPI
GetAddrInfoExW(PCWSTR pName,
               PCWSTR pServiceName,
               DWORD dwNameSpace,
               LPGUID lpNspId,
               const ADDRINFOEXW *hints,
               PADDRINFOEXW *ppResult,
               struct timeval *timeout,
               LPOVERLAPPED lpOverlapped,
               LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
               LPHANDLE lpHandle)
{
    ADDRINFOW Hints, *Result = NULL;
    INT Error;

    UNREFERENCED_PARAMETER(lpNspId);
    UNREFERENCED_PARAMETER(timeout);

    if (!ppResult)
        return WSA_INVALID_PARAMETER;
    *ppResult = NULL;
    if (lpHandle)
        *lpHandle = NULL;
    if (dwNameSpace != NS_ALL && dwNameSpace != NS_DNS)
        return WSAEINVAL;

    ZeroMemory(&Hints, sizeof(Hints));
    if (hints)
    {
        Hints.ai_flags = hints->ai_flags;
        Hints.ai_family = hints->ai_family;
        Hints.ai_socktype = hints->ai_socktype;
        Hints.ai_protocol = hints->ai_protocol;
    }

    Error = GetAddrInfoW(pName, pServiceName, hints ? &Hints : NULL, &Result);
    if (!Error)
    {
        *ppResult = CopyAddrInfoToEx(Result);
        freeaddrinfo((struct addrinfo *)Result); /* (FreeAddrInfoW is this export) */
        if (!*ppResult)
            Error = WSA_NOT_ENOUGH_MEMORY;
    }

    if (!lpOverlapped)
        return Error;

    /* Complete the "asynchronous" request */
    lpOverlapped->Internal = Error;
    if (lpOverlapped->hEvent)
        SetEvent(lpOverlapped->hEvent);
    if (lpCompletionRoutine)
    {
        PADDRINFOEX_COMPLETION Completion = HeapAlloc(GetProcessHeap(), 0, sizeof(*Completion));
        if (Completion)
        {
            Completion->Routine = lpCompletionRoutine;
            Completion->Error = Error;
            Completion->Overlapped = lpOverlapped;
            if (!QueueUserWorkItem(CompletionWorker, Completion, WT_EXECUTEDEFAULT))
            {
                HeapFree(GetProcessHeap(), 0, Completion);
                lpCompletionRoutine(Error, 0, lpOverlapped);
            }
        }
    }
    return WSA_IO_PENDING;
}

INT
WSAAPI
GetAddrInfoExOverlappedResult(LPOVERLAPPED lpOverlapped)
{
    if (!lpOverlapped)
        return WSAEINVAL;
    return (INT)lpOverlapped->Internal;
}

INT
WSAAPI
GetAddrInfoExCancel(LPHANDLE lpHandle)
{
    /* Every request is complete when GetAddrInfoExW returns */
    UNREFERENCED_PARAMETER(lpHandle);
    return WSA_INVALID_HANDLE;
}

VOID
WSAAPI
FreeAddrInfoExW(PADDRINFOEXW pAddrInfoEx)
{
    while (pAddrInfoEx)
    {
        PADDRINFOEXW Next = pAddrInfoEx->ai_next;
        HeapFree(GetProcessHeap(), 0, pAddrInfoEx);
        pAddrInfoEx = Next;
    }
}
