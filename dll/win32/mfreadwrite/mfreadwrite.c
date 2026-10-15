/*
 * PROJECT:     ReactOS Media Foundation
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Media Foundation source reader (mfreadwrite.dll), not available yet (see mfplat.c)
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0L)

HRESULT WINAPI MFCreateSourceReaderFromMediaSource(void *Source, void *Attributes, void **Reader)
{
    if (Reader) *Reader = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFCreateSourceReaderFromURL(const WCHAR *Url, void *Attributes, void **Reader)
{
    if (Reader) *Reader = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFCreateSourceReaderFromByteStream(void *Stream, void *Attributes, void **Reader)
{
    if (Reader) *Reader = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFCreateSinkWriterFromURL(const WCHAR *Url, void *Stream, void *Attributes, void **Writer)
{
    if (Writer) *Writer = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFCreateSinkWriterFromMediaSink(void *Sink, void *Attributes, void **Writer)
{
    if (Writer) *Writer = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}
