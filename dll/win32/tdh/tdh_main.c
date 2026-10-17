/*
 * PROJECT:     ReactOS Trace Data Helper (tdh.dll)
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Decoding of event tracing (ETW) events: not supported yet
 * COPYRIGHT:   Copyright 2017 Austin English (Wine's TdhLoadManifest stubs)
 *              Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * ReactOS has no event tracing consumer support, so every decoding function
 * answers ERROR_NOT_SUPPORTED. Programs only need the DLL to load: Wireshark's
 * etwdump.exe imports it, and without it Windows' loader error box popped up
 * while Wireshark started.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tdh);

ULONG WINAPI TdhGetEventInformation(PVOID event, ULONG context_count, PVOID context,
                                    PVOID info, PULONG size)
{
    FIXME("(%p, %lu, %p, %p, %p): stub\n", event, context_count, context, info, size);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhGetEventMapInformation(PVOID event, LPWSTR map_name, PVOID info, PULONG size)
{
    FIXME("(%p, %s, %p, %p): stub\n", event, debugstr_w(map_name), info, size);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhGetPropertySize(PVOID event, ULONG context_count, PVOID context,
                                ULONG descriptor_count, PVOID descriptors, PULONG size)
{
    FIXME("(%p, %lu, %p, %lu, %p, %p): stub\n", event, context_count, context,
          descriptor_count, descriptors, size);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhGetProperty(PVOID event, ULONG context_count, PVOID context,
                            ULONG descriptor_count, PVOID descriptors, ULONG size, PBYTE buffer)
{
    FIXME("(%p, %lu, %p, %lu, %p, %lu, %p): stub\n", event, context_count, context,
          descriptor_count, descriptors, size, buffer);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhFormatProperty(PVOID event_info, PVOID map_info, ULONG pointer_size,
                               USHORT in_type, USHORT out_type, USHORT length,
                               USHORT data_length, PBYTE data, PULONG size, PWCHAR buffer,
                               PUSHORT consumed)
{
    FIXME("(%p, %p, %lu, %u, %u, %u, %u, %p, %p, %p, %p): stub\n", event_info, map_info,
          pointer_size, in_type, out_type, length, data_length, data, size, buffer, consumed);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhEnumerateProviders(PVOID buffer, PULONG size)
{
    FIXME("(%p, %p): stub\n", buffer, size);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhOpenDecodingHandle(PVOID *handle)
{
    FIXME("(%p): stub\n", handle);
    return ERROR_NOT_SUPPORTED;
}

ULONG WINAPI TdhCloseDecodingHandle(PVOID handle)
{
    FIXME("(%p): stub\n", handle);
    return ERROR_SUCCESS;
}

ULONG WINAPI TdhLoadManifest(LPWSTR manifest)
{
    FIXME("(%s): stub\n", debugstr_w(manifest));
    return ERROR_SUCCESS;
}

ULONG WINAPI TdhLoadManifestFromBinary(LPWSTR binary)
{
    FIXME("(%s): stub\n", debugstr_w(binary));
    return ERROR_SUCCESS;
}

ULONG WINAPI TdhUnloadManifest(LPWSTR manifest)
{
    FIXME("(%s): stub\n", debugstr_w(manifest));
    return ERROR_SUCCESS;
}
