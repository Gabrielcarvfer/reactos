/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for Core Audio (WASAPI) rendering on the default endpoint
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * What players (Spotify, Chromium's audio service) do with WASAPI: open the
 * default render endpoint, play in shared event-driven mode through
 * IAudioRenderClient and follow IAudioClock, a polling stream with
 * AUTOCONVERTPCM, IAudioClient3 and ISimpleAudioVolume.
 * Without a sound device everything after the enumeration is skipped.
 */

#define COBJMACROS
#include <apitest.h>
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <math.h>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif

/* IAudioClient3 (Windows 10), called through its vtable: IUnknown (3),
 * IAudioClient (12), IAudioClient2 (3), then IAudioClient3's own methods */
DEFINE_GUID(IID_IAudioClient3_, 0x7ed4ee07, 0x8e67, 0x4cd4, 0x8c, 0x1a, 0x2b, 0x7a, 0x59, 0x87, 0xad, 0x42);
#define IAUDIOCLIENT3_GETSHAREDMODEENGINEPERIOD 18
#define IAUDIOCLIENT3_INITIALIZESHAREDAUDIOSTREAM 20
typedef HRESULT (STDMETHODCALLTYPE *GETSHAREDMODEENGINEPERIOD)(void *, const WAVEFORMATEX *, UINT32 *, UINT32 *, UINT32 *, UINT32 *);
typedef HRESULT (STDMETHODCALLTYPE *INITIALIZESHAREDAUDIOSTREAM)(void *, DWORD, UINT32, const WAVEFORMATEX *, const GUID *);

/* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT */
static const GUID SubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

#define MIN_U32(a, b) ((a) < (b) ? (a) : (b))

static BOOL IsFloat(const WAVEFORMATEX *fmt)
{
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return TRUE;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return IsEqualGUID(&((const WAVEFORMATEXTENSIBLE *)fmt)->SubFormat, &SubtypeFloat);
    return FALSE;
}

/* A quiet sine into the buffer, in the stream's format */
static void FillSine(const WAVEFORMATEX *fmt, BYTE *data, UINT32 frames, double freq, UINT64 *phase)
{
    UINT32 i, c;
    for (i = 0; i < frames; i++)
    {
        double t = (double)(*phase + i) / fmt->nSamplesPerSec;
        float v = (float)(0.1 * sin(2.0 * 3.14159265358979 * freq * t));
        for (c = 0; c < fmt->nChannels; c++)
        {
            if (IsFloat(fmt))
                ((float *)data)[i * fmt->nChannels + c] = v;
            else if (fmt->wBitsPerSample == 16)
                ((INT16 *)data)[i * fmt->nChannels + c] = (INT16)(v * 32767);
            else if (fmt->wBitsPerSample == 32)
                ((INT32 *)data)[i * fmt->nChannels + c] = (INT32)(v * 2147483647.0);
        }
    }
    *phase += frames;
}

static double ClockSeconds(IAudioClock *clock)
{
    UINT64 pos = 0, freq = 0;
    if (FAILED(IAudioClock_GetFrequency(clock, &freq)) || !freq)
        return -1;
    if (FAILED(IAudioClock_GetPosition(clock, &pos, NULL)))
        return -1;
    return (double)pos / freq;
}

/* 1 s of 440 Hz, event-driven */
static void TestEventDriven(IMMDevice *dev)
{
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    IAudioClock *clock = NULL;
    WAVEFORMATEX *fmt = NULL, *closest = NULL;
    REFERENCE_TIME defper = 0, minper = 0;
    UINT32 bufsize = 0, padding = 0, total, written = 0;
    UINT64 phase = 0, freq = 0;
    HANDLE event = NULL;
    HRESULT hr;
    DWORD start, waited, t09 = 0;
    double pos;
    int timeouts = 0;

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    ok(hr == S_OK, "Activate(IAudioClient): 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IAudioClient_GetMixFormat(client, &fmt);
    ok(hr == S_OK && fmt != NULL, "GetMixFormat: 0x%08lx\n", hr);
    if (FAILED(hr) || !fmt)
        goto done;
    trace("mix format: tag 0x%x, %u ch, %lu Hz, %u bits\n", fmt->wFormatTag, fmt->nChannels,
          fmt->nSamplesPerSec, fmt->wBitsPerSample);
    ok(fmt->nChannels >= 1 && fmt->nSamplesPerSec >= 8000 &&
       (IsFloat(fmt) || fmt->wBitsPerSample == 16 || fmt->wBitsPerSample == 32),
       "mix format not usable: tag 0x%x, %u ch, %lu Hz, %u bits\n",
       fmt->wFormatTag, fmt->nChannels, fmt->nSamplesPerSec, fmt->wBitsPerSample);

    hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_SHARED, fmt, &closest);
    ok(hr == S_OK, "IsFormatSupported(mix format): 0x%08lx\n", hr);
    CoTaskMemFree(closest);

    hr = IAudioClient_GetDevicePeriod(client, &defper, &minper);
    ok(hr == S_OK && defper >= 10000 && defper <= 200000 && minper > 0 && minper <= defper,
       "GetDevicePeriod: 0x%08lx, default %ld, minimum %ld (100 ns)\n", hr, (long)defper, (long)minper);

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                                 0, 0, fmt, NULL);
    ok(hr == S_OK, "Initialize(shared, event): 0x%08lx\n", hr);
    if (FAILED(hr))
        goto done;

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, fmt, NULL);
    ok(hr == AUDCLNT_E_ALREADY_INITIALIZED, "second Initialize: 0x%08lx\n", hr);

    hr = IAudioClient_GetBufferSize(client, &bufsize);
    ok(hr == S_OK && bufsize > 0, "GetBufferSize: 0x%08lx, %u frames\n", hr, bufsize);

    hr = IAudioClient_Start(client);
    ok(hr == AUDCLNT_E_EVENTHANDLE_NOT_SET, "Start without an event: 0x%08lx\n", hr);

    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = IAudioClient_SetEventHandle(client, event);
    ok(hr == S_OK, "SetEventHandle: 0x%08lx\n", hr);

    hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
    ok(hr == S_OK, "GetService(IAudioRenderClient): 0x%08lx\n", hr);
    hr = IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(hr == S_OK, "GetService(IAudioClock): 0x%08lx\n", hr);
    if (!render || !clock)
        goto done;

    hr = IAudioClock_GetFrequency(clock, &freq);
    ok(hr == S_OK && freq > 0, "IAudioClock::GetFrequency: 0x%08lx, %lu\n", hr, (ULONG)freq);

    total = fmt->nSamplesPerSec;

    /* Fill the buffer first, as players do */
    {
        BYTE *data;
        UINT32 n = MIN_U32(bufsize, total);
        hr = IAudioRenderClient_GetBuffer(render, n, &data);
        ok(hr == S_OK, "GetBuffer(%u) before Start: 0x%08lx\n", n, hr);
        if (SUCCEEDED(hr))
        {
            FillSine(fmt, data, n, 440.0, &phase);
            hr = IAudioRenderClient_ReleaseBuffer(render, n, 0);
            ok(hr == S_OK, "ReleaseBuffer: 0x%08lx\n", hr);
            written += n;
        }
    }

    hr = IAudioClient_GetCurrentPadding(client, &padding);
    ok(hr == S_OK && padding == written, "padding before Start: %u, written %u\n", padding, written);

    pos = ClockSeconds(clock);
    ok(pos == 0.0, "clock before Start: %d ms\n", (int)(pos * 1000));

    hr = IAudioClient_Start(client);
    ok(hr == S_OK, "Start: 0x%08lx\n", hr);
    start = GetTickCount();

    while (written < total)
    {
        BYTE *data;
        UINT32 n;

        if (WaitForSingleObject(event, 2000) != WAIT_OBJECT_0)
        {
            if (++timeouts > 2)
                break;
            continue;
        }
        if (FAILED(IAudioClient_GetCurrentPadding(client, &padding)))
            break;
        n = MIN_U32(bufsize - padding, total - written);
        if (!n)
            continue;
        if (FAILED(IAudioRenderClient_GetBuffer(render, n, &data)))
            break;
        FillSine(fmt, data, n, 440.0, &phase);
        IAudioRenderClient_ReleaseBuffer(render, n, 0);
        written += n;
        if (!t09 && ClockSeconds(clock) >= 0.9)
            t09 = GetTickCount() - start;
    }
    ok(timeouts == 0, "the event was not signaled %d times\n", timeouts);
    ok(written == total, "wrote %u of %u frames\n", written, total);

    /* Until it is all played (and a bit) */
    for (waited = 0; waited < 3000; waited += 20)
    {
        pos = ClockSeconds(clock);
        if (!t09 && pos >= 0.9)
            t09 = GetTickCount() - start;
        if (pos >= (double)written / fmt->nSamplesPerSec - 0.0005)
            break;
        Sleep(20);
    }
    pos = ClockSeconds(clock);
    ok(pos >= 0.9 && pos <= 1.01, "IAudioClock did not advance ~1 s: %d ms\n", (int)(pos * 1000));
    ok(t09 >= 700 && t09 <= 2500, "not played in real time: 0.9 s of audio took %lu ms\n", t09);

    hr = IAudioClient_GetCurrentPadding(client, &padding);
    ok(hr == S_OK && padding == 0, "padding at the end: %u\n", padding);

    hr = IAudioClient_Stop(client);
    ok(hr == S_OK, "Stop: 0x%08lx\n", hr);
    hr = IAudioClient_Stop(client);
    ok(hr == S_FALSE, "second Stop: 0x%08lx\n", hr);
    hr = IAudioClient_Reset(client);
    ok(hr == S_OK, "Reset: 0x%08lx\n", hr);
    pos = ClockSeconds(clock);
    ok(pos == 0.0, "clock after Reset: %d ms\n", (int)(pos * 1000));

done:
    if (event)
        CloseHandle(event);
    if (clock)
        IAudioClock_Release(clock);
    if (render)
        IAudioRenderClient_Release(render);
    CoTaskMemFree(fmt);
    IAudioClient_Release(client);
}

/* 0.3 s of 660 Hz, polling (no event), 16-bit stereo converted by the engine */
static void TestPolling(IMMDevice *dev)
{
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    IAudioClock *clock = NULL;
    WAVEFORMATEX *mix = NULL, fmt;
    UINT32 bufsize = 0, padding, total, written = 0;
    UINT64 phase = 0;
    DWORD start;
    HRESULT hr;
    double pos;

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr))
        return;
    IAudioClient_GetMixFormat(client, &mix);

    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = mix ? mix->nSamplesPerSec : 48000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 4;
    fmt.cbSize = 0;
    CoTaskMemFree(mix);

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                 1000000 /* 100 ms */, 0, &fmt, NULL);
    ok(hr == S_OK, "Initialize(shared, polling, 16-bit stereo, AUTOCONVERTPCM): 0x%08lx\n", hr);
    if (FAILED(hr))
        goto done;
    IAudioClient_GetBufferSize(client, &bufsize);
    IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render);
    IAudioClient_GetService(client, &IID_IAudioClock, (void **)&clock);
    ok(render != NULL && clock != NULL, "polling: no render client (%p) or clock (%p)\n", render, clock);
    if (!render || !clock)
        goto done;

    total = fmt.nSamplesPerSec * 3 / 10;
    hr = IAudioClient_Start(client);
    ok(hr == S_OK, "polling: Start: 0x%08lx\n", hr);
    start = GetTickCount();
    while (written < total && GetTickCount() - start < 3000)
    {
        BYTE *data;
        UINT32 n;
        if (FAILED(IAudioClient_GetCurrentPadding(client, &padding)))
            break;
        n = MIN_U32(bufsize - padding, total - written);
        if (n && SUCCEEDED(IAudioRenderClient_GetBuffer(render, n, &data)))
        {
            FillSine(&fmt, data, n, 660.0, &phase);
            IAudioRenderClient_ReleaseBuffer(render, n, 0);
            written += n;
        }
        Sleep(5);
    }
    while (GetTickCount() - start < 3000 && ClockSeconds(clock) < 0.299)
        Sleep(10);
    pos = ClockSeconds(clock);
    ok(pos >= 0.27 && pos <= 0.31, "polling: clock did not advance ~0.3 s: %d ms after %lu ms\n",
       (int)(pos * 1000), GetTickCount() - start);
    IAudioClient_Stop(client);

done:
    if (clock)
        IAudioClock_Release(clock);
    if (render)
        IAudioRenderClient_Release(render);
    IAudioClient_Release(client);
}

static void TestMisc(IMMDevice *dev)
{
    IAudioClient *client = NULL;
    void **client3 = NULL;
    ISimpleAudioVolume *vol = NULL;
    WAVEFORMATEX *fmt = NULL;
    HRESULT hr;

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr))
        return;
    IAudioClient_GetMixFormat(client, &fmt);
    if (!fmt)
    {
        IAudioClient_Release(client);
        return;
    }

    hr = IAudioClient_QueryInterface(client, &IID_IAudioClient3_, (void **)&client3);
    ok(hr == S_OK, "QueryInterface(IAudioClient3): 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        GETSHAREDMODEENGINEPERIOD GetPeriod = (GETSHAREDMODEENGINEPERIOD)((void **)*client3)[IAUDIOCLIENT3_GETSHAREDMODEENGINEPERIOD];
        INITIALIZESHAREDAUDIOSTREAM InitStream = (INITIALIZESHAREDAUDIOSTREAM)((void **)*client3)[IAUDIOCLIENT3_INITIALIZESHAREDAUDIOSTREAM];
        UINT32 def = 0, unit = 0, mn = 0, mx = 0;

        hr = GetPeriod(client3, fmt, &def, &unit, &mn, &mx);
        ok(hr == S_OK && def > 0 && mn <= def && def <= mx,
           "GetSharedModeEnginePeriod: 0x%08lx, default %u, min %u, max %u frames\n", hr, def, mn, mx);
        hr = InitStream(client3, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, def, fmt, NULL);
        ok(hr == S_OK, "InitializeSharedAudioStream: 0x%08lx\n", hr);
        ((IUnknown *)client3)->lpVtbl->Release((IUnknown *)client3);
    }
    else
    {
        IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, fmt, NULL);
    }

    hr = IAudioClient_GetService(client, &IID_ISimpleAudioVolume, (void **)&vol);
    ok(hr == S_OK, "GetService(ISimpleAudioVolume): 0x%08lx\n", hr);
    if (vol)
    {
        float level = 0;
        hr = ISimpleAudioVolume_SetMasterVolume(vol, 0.5f, NULL);
        ISimpleAudioVolume_GetMasterVolume(vol, &level);
        ok(hr == S_OK && level == 0.5f, "SetMasterVolume(0.5): 0x%08lx, then %d/100\n", hr, (int)(level * 100));
        ISimpleAudioVolume_SetMasterVolume(vol, 1.0f, NULL);
        ISimpleAudioVolume_Release(vol);
    }

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 100000, 100000, fmt, NULL);
    ok(FAILED(hr), "Initialize(exclusive) on an initialized client: 0x%08lx\n", hr);

    CoTaskMemFree(fmt);
    IAudioClient_Release(client);
}

START_TEST(AudioRender)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDeviceCollection *coll = NULL;
    IMMDevice *dev = NULL;
    UINT count = 0;
    DWORD state = 0;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx: 0x%08lx\n", hr);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    ok(hr == S_OK, "CoCreateInstance(MMDeviceEnumerator): 0x%08lx\n", hr);
    if (FAILED(hr))
        goto end;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &coll);
    ok(hr == S_OK, "EnumAudioEndpoints(eRender): 0x%08lx\n", hr);
    if (coll)
    {
        IMMDeviceCollection_GetCount(coll, &count);
        trace("%u active render endpoint(s)\n", count);
        IMMDeviceCollection_Release(coll);
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (hr == E_NOTFOUND || (count == 0 && FAILED(hr)))
    {
        skip("No render endpoint (no sound device): 0x%08lx\n", hr);
        goto end;
    }
    ok(hr == S_OK && dev != NULL, "GetDefaultAudioEndpoint(eRender, eConsole): 0x%08lx\n", hr);
    if (FAILED(hr) || !dev)
        goto end;

    IMMDevice_GetState(dev, &state);
    ok(state == DEVICE_STATE_ACTIVE, "default endpoint state %lu\n", state);

    TestEventDriven(dev);
    TestPolling(dev);
    TestMisc(dev);

    IMMDevice_Release(dev);

end:
    if (en)
        IMMDeviceEnumerator_Release(en);
    CoUninitialize();
}
