/*
 * PROJECT:     ReactOS Media Foundation
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Media Foundation platform (mfplat.dll), not available yet
 *
 * ReactOS has no Media Foundation yet. Programs that use it if present (like
 * Chromium, for hardware video decoding and capture) import these functions
 * and only call them after MFStartup, which fails here: they then use their
 * own decoders. The functions exist so that those programs load and run.
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0L)

HRESULT WINAPI MFStartup(ULONG Version, DWORD Flags)
{
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFShutdown(void)
{
    return S_OK;
}

/* No transforms (decoders, encoders...) are registered */
HRESULT WINAPI MFTEnumEx(GUID Category, UINT32 Flags, const void *InputType, const void *OutputType,
                         void ***Activates, UINT32 *Count)
{
    if (!Activates || !Count)
        return E_POINTER;
    *Activates = NULL;
    *Count = 0;
    return S_OK;
}

HRESULT WINAPI MFFrameRateToAverageTimePerFrame(UINT32 Numerator, UINT32 Denominator, UINT64 *AverageTimePerFrame)
{
    if (!AverageTimePerFrame)
        return E_POINTER;
    /* In 100 ns units */
    *AverageTimePerFrame = Numerator ? (UINT64)Denominator * 10000000 / Numerator : 0;
    return S_OK;
}

#define NOT_AVAILABLE(Out) do { if (Out) *(Out) = NULL; return MF_E_PLATFORM_NOT_INITIALIZED; } while (0)

HRESULT WINAPI MFCreateAttributes(void **Attributes, UINT32 Size) { NOT_AVAILABLE(Attributes); }
HRESULT WINAPI MFCreateMediaType(void **Type) { NOT_AVAILABLE(Type); }
HRESULT WINAPI MFCreateSample(void **Sample) { NOT_AVAILABLE(Sample); }
HRESULT WINAPI MFCreateMemoryBuffer(DWORD MaxLength, void **Buffer) { NOT_AVAILABLE(Buffer); }
HRESULT WINAPI MFCreateAlignedMemoryBuffer(DWORD MaxLength, DWORD Alignment, void **Buffer) { NOT_AVAILABLE(Buffer); }
HRESULT WINAPI MFCreateDXGISurfaceBuffer(REFIID Riid, IUnknown *Surface, UINT SubresourceIndex,
                                         BOOL BottomUpWhenLinear, void **Buffer) { NOT_AVAILABLE(Buffer); }
HRESULT WINAPI MFCreateDXGIDeviceManager(UINT *ResetToken, void **Manager)
{
    if (ResetToken) *ResetToken = 0;
    NOT_AVAILABLE(Manager);
}
HRESULT WINAPI MFLockDXGIDeviceManager(UINT *ResetToken, void **Manager)
{
    if (ResetToken) *ResetToken = 0;
    NOT_AVAILABLE(Manager);
}
HRESULT WINAPI MFUnlockDXGIDeviceManager(void) { return MF_E_PLATFORM_NOT_INITIALIZED; }
HRESULT WINAPI MFCreateEventQueue(void **Queue) { NOT_AVAILABLE(Queue); }
HRESULT WINAPI MFCreateMediaEvent(DWORD Type, REFGUID ExtendedType, HRESULT Status, const void *Value,
                                  void **Event) { NOT_AVAILABLE(Event); }
HRESULT WINAPI MFCreateAsyncResult(IUnknown *Object, void *Callback, IUnknown *State, void **AsyncResult)
{
    NOT_AVAILABLE(AsyncResult);
}
HRESULT WINAPI MFCreatePresentationDescriptor(DWORD Count, void **Descriptors, void **Presentation)
{
    NOT_AVAILABLE(Presentation);
}
HRESULT WINAPI MFCreateStreamDescriptor(DWORD Identifier, DWORD Count, void **Types, void **Descriptor)
{
    NOT_AVAILABLE(Descriptor);
}

/* Functions of Chromium's, Firefox's and OBS's Media Foundation code */
HRESULT WINAPI MFCreateMediaBufferWrapper(void *Buffer, DWORD Offset, DWORD Length, void **Wrapper)
{
    NOT_AVAILABLE(Wrapper);
}
HRESULT WINAPI MFCreateWaveFormatExFromMFMediaType(void *Type, void **Format, UINT32 *Size, UINT32 Flags)
{
    if (Size) *Size = 0;
    NOT_AVAILABLE(Format);
}
HRESULT WINAPI MFInitMediaTypeFromWaveFormatEx(void *Type, const void *Format, UINT32 Size)
{
    return MF_E_PLATFORM_NOT_INITIALIZED;
}
HRESULT WINAPI MFSerializeAttributesToStream(void *Attributes, DWORD Options, IStream *Stream)
{
    return MF_E_PLATFORM_NOT_INITIALIZED;
}
HRESULT WINAPI MFCreateSourceResolver(void **Resolver) { NOT_AVAILABLE(Resolver); }
HRESULT WINAPI MFUnwrapMediaType(void *Wrapper, void **Original) { NOT_AVAILABLE(Original); }
HRESULT WINAPI MFInvokeCallback(void *AsyncResult) { return MF_E_PLATFORM_NOT_INITIALIZED; }
HRESULT WINAPI MFPutWorkItem(DWORD Queue, void *Callback, IUnknown *State)
{
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

/* The stride of an image of a format (in bytes, from the bit count) */
HRESULT WINAPI MFGetStrideForBitmapInfoHeader(DWORD Format, DWORD Width, LONG *Stride)
{
    if (!Stride)
        return E_POINTER;
    *Stride = 0;
    switch (Format)
    {
        case 21: /* D3DFMT_A8R8G8B8 */
        case 22: /* D3DFMT_X8R8G8B8 */
            *Stride = Width * 4;
            return S_OK;
        case 20: /* D3DFMT_R8G8B8 */
            *Stride = (Width * 3 + 3) & ~3;
            return S_OK;
        case 23: /* D3DFMT_R5G6B5 */
        case 24: /* D3DFMT_X1R5G5B5 */
            *Stride = Width * 2;
            return S_OK;
        default:
            return MF_E_PLATFORM_NOT_INITIALIZED;
    }
}

/* Media Foundation's clock: 100 ns units, monotonic */
LONGLONG WINAPI MFGetSystemTime(void)
{
    LARGE_INTEGER Counter, Frequency;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Counter);
    return (LONGLONG)((double)Counter.QuadPart * 10000000.0 / (double)Frequency.QuadPart);
}
