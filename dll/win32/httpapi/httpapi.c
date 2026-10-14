/*
 * PROJECT:     ReactOS HTTP Server API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HTTP Server API (httpapi.dll), not available
 *
 * The HTTP Server API is served by the http.sys driver, which ReactOS does not
 * have: programs are told so (ERROR_NOT_SUPPORTED) instead of being given
 * sessions and queues that do not exist. LibreOffice imports these.
 */

#include <windef.h>
#include <winbase.h>

typedef ULONGLONG HTTP_OPAQUE_ID;

ULONG WINAPI HttpInitialize(ULONG Version, ULONG Flags, PVOID Reserved)
{
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI HttpTerminate(ULONG Flags, PVOID Reserved)
{
    return NO_ERROR;
}

ULONG WINAPI HttpCreateServerSession(ULONG Version, HTTP_OPAQUE_ID *ServerSessionId, ULONG Reserved)
{
    if (ServerSessionId) *ServerSessionId = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI HttpCreateUrlGroup(HTTP_OPAQUE_ID ServerSessionId, HTTP_OPAQUE_ID *UrlGroupId, ULONG Reserved)
{
    if (UrlGroupId) *UrlGroupId = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI HttpCreateRequestQueue(ULONG Version, PCWSTR Name, PSECURITY_ATTRIBUTES SecurityAttributes,
                                    ULONG Flags, PHANDLE RequestQueueHandle)
{
    if (RequestQueueHandle) *RequestQueueHandle = NULL;
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI HttpShutdownRequestQueue(HANDLE RequestQueueHandle)
{
    return ERROR_INVALID_HANDLE;
}

ULONG WINAPI HttpSetUrlGroupProperty(HTTP_OPAQUE_ID UrlGroupId, ULONG Property, PVOID Information, ULONG Length)
{
    return ERROR_INVALID_PARAMETER;
}

ULONG WINAPI HttpAddUrlToUrlGroup(HTTP_OPAQUE_ID UrlGroupId, PCWSTR FullyQualifiedUrl,
                                  HTTP_OPAQUE_ID UrlContext, ULONG Reserved)
{
    return ERROR_INVALID_PARAMETER;
}

ULONG WINAPI HttpRemoveUrlFromUrlGroup(HTTP_OPAQUE_ID UrlGroupId, PCWSTR FullyQualifiedUrl, ULONG Flags)
{
    return ERROR_INVALID_PARAMETER;
}

ULONG WINAPI HttpReceiveHttpRequest(HANDLE RequestQueueHandle, HTTP_OPAQUE_ID RequestId, ULONG Flags,
                                    PVOID RequestBuffer, ULONG RequestBufferLength, PULONG BytesReturned,
                                    LPOVERLAPPED Overlapped)
{
    return ERROR_INVALID_HANDLE;
}

ULONG WINAPI HttpSendHttpResponse(HANDLE RequestQueueHandle, HTTP_OPAQUE_ID RequestId, ULONG Flags,
                                  PVOID HttpResponse, PVOID CachePolicy, PULONG BytesSent, PVOID Reserved1,
                                  ULONG Reserved2, LPOVERLAPPED Overlapped, PVOID LogData)
{
    return ERROR_INVALID_HANDLE;
}
