/*
 * Built-in audio driver of mmdevapi for ReactOS: plays through winmm (waveOut)
 *
 * Copyright 2011 Andrew Eikum for CodeWeavers (the Wine OSS driver this is
 * based on: sessions, volumes, the render buffer)
 * Copyright 2026 ReactOS project (the waveOut back end)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * Wine's mmdevapi gets its devices and streams from a driver DLL
 * (winealsa.drv, winepulse.drv...) that talks to the host's sound system.
 * ReactOS has none: its sound system is winmm (wdmaud.drv, kernel streaming).
 * This driver is built into mmdevapi and puts the Core Audio interfaces on
 * top of waveOut:
 *
 *  - an endpoint per waveOut device (capture: none yet);
 *  - an IAudioClient keeps the program's samples in a ring buffer, as Wine's
 *    drivers do. A thread per stream moves them, one device period (10 ms) at
 *    a time, into a few waveOut buffers: converted to 16-bit PCM, with the
 *    session and stream volumes applied. Each finished waveOut buffer wakes
 *    the thread, which refills it and signals the program's event;
 *  - the clock counts the frames the device has played.
 */

#include "precomp.h"

#include <mmsystem.h>
#include <mmddk.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <audioclient.h>
#include <devpkey.h>
#include <wine/list.h>

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

#define NULL_PTR_ERR MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, RPC_X_NULL_REF_POINTER)

static const REFERENCE_TIME DefaultPeriod = 100000; /* 10 ms */
static const REFERENCE_TIME MinimumPeriod = 100000;

/* waveOut buffers in flight, of a period each */
#define NUM_WAVEHDRS 8

struct ACImpl;
typedef struct ACImpl ACImpl;

typedef struct _AudioSession {
    GUID guid;
    struct list clients;

    IMMDevice *device;

    float master_vol;
    UINT32 channel_count;
    float *channel_vols;
    BOOL mute;

    CRITICAL_SECTION lock;

    struct list entry;
} AudioSession;

typedef struct _AudioSessionWrapper {
    IAudioSessionControl2 IAudioSessionControl2_iface;
    IChannelAudioVolume IChannelAudioVolume_iface;
    ISimpleAudioVolume ISimpleAudioVolume_iface;

    LONG ref;

    ACImpl *client;
    AudioSession *session;
} AudioSessionWrapper;

struct ACImpl {
    IAudioClient3 IAudioClient3_iface;
    IAudioRenderClient IAudioRenderClient_iface;
    IAudioClock IAudioClock_iface;
    IAudioClock2 IAudioClock2_iface;
    IAudioStreamVolume IAudioStreamVolume_iface;

    LONG ref;

    IMMDevice *parent;
    IUnknown *pUnkFTMarshal;

    WAVEFORMATEX *fmt;

    EDataFlow dataflow;
    DWORD flags;
    AUDCLNT_SHAREMODE share;
    HANDLE event;
    float *vols;

    /* waveOut side */
    UINT wave_id;
    HWAVEOUT hwo;
    WAVEFORMATEX out_fmt;           /* what waveOut plays: 16-bit PCM */
    WAVEHDR hdrs[NUM_WAVEHDRS];
    UINT32 hdr_frames[NUM_WAVEHDRS]; /* frames in each queued buffer */
    BYTE *hdr_mem;
    UINT hdr_first, hdr_queued;     /* oldest queued buffer, how many */
    HANDLE wave_event;              /* set by winmm when a buffer is done */
    HANDLE quit_event, thread;
    BOOL paused;

    BOOL initted, playing;
    UINT64 written_frames;          /* given by the program */
    UINT64 submitted_frames;        /* sent to waveOut */
    UINT64 played_frames;           /* waveOut buffers done */
    UINT64 last_pos_frames;
    UINT32 period_frames, bufsize_frames, held_frames, tmp_buffer_frames;
    UINT32 lcl_offs_frames; /* offs into local_buffer where valid data starts */

    BYTE *local_buffer, *tmp_buffer;
    LONG32 getbuf_last; /* <0 when using tmp_buffer */

    CRITICAL_SECTION lock;

    AudioSession *session;
    AudioSessionWrapper *session_wrapper;

    struct list entry;
};

typedef struct _SessionMgr {
    IAudioSessionManager2 IAudioSessionManager2_iface;

    LONG ref;

    IMMDevice *device;
} SessionMgr;

typedef struct _WinMMDevice {
    EDataFlow flow;
    UINT id;
    GUID guid;
    struct list entry;
} WinMMDevice;

static struct list g_devices = LIST_INIT(g_devices);

static CRITICAL_SECTION g_sessions_lock;
static CRITICAL_SECTION_DEBUG g_sessions_lock_debug =
{
    0, 0, &g_sessions_lock,
    { &g_sessions_lock_debug.ProcessLocksList, &g_sessions_lock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_sessions_lock") }
};
static CRITICAL_SECTION g_sessions_lock = { &g_sessions_lock_debug, -1, 0, 0, 0, 0 };
static struct list g_sessions = LIST_INIT(g_sessions);

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client);

static const IAudioClient3Vtbl AudioClient3_Vtbl;
static const IAudioRenderClientVtbl AudioRenderClient_Vtbl;
static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl;
static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl;
static const IAudioClockVtbl AudioClock_Vtbl;
static const IAudioClock2Vtbl AudioClock2_Vtbl;
static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl;
static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl;
static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl;

static inline ACImpl *impl_from_IAudioClient3(IAudioClient3 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClient3_iface);
}

static inline ACImpl *impl_from_IAudioRenderClient(IAudioRenderClient *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioRenderClient_iface);
}

static inline AudioSessionWrapper *impl_from_IAudioSessionControl2(IAudioSessionControl2 *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IAudioSessionControl2_iface);
}

static inline AudioSessionWrapper *impl_from_ISimpleAudioVolume(ISimpleAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, ISimpleAudioVolume_iface);
}

static inline AudioSessionWrapper *impl_from_IChannelAudioVolume(IChannelAudioVolume *iface)
{
    return CONTAINING_RECORD(iface, AudioSessionWrapper, IChannelAudioVolume_iface);
}

static inline ACImpl *impl_from_IAudioClock(IAudioClock *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock_iface);
}

static inline ACImpl *impl_from_IAudioClock2(IAudioClock2 *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioClock2_iface);
}

static inline ACImpl *impl_from_IAudioStreamVolume(IAudioStreamVolume *iface)
{
    return CONTAINING_RECORD(iface, ACImpl, IAudioStreamVolume_iface);
}

static inline SessionMgr *impl_from_IAudioSessionManager2(IAudioSessionManager2 *iface)
{
    return CONTAINING_RECORD(iface, SessionMgr, IAudioSessionManager2_iface);
}

int WINAPI WINMM_GetPriority(void)
{
    return waveOutGetNumDevs() ? Priority_Preferred : Priority_Neutral;
}

/* The same GUID for a device in every process (32 and 64-bit ones too) and
 * every session: made from its flow, index and name */
static void make_device_guid(EDataFlow flow, UINT id, const WCHAR *name, GUID *guid)
{
    DWORD h1 = 2166136261u, h2 = 0x9e3779b9u;
    const WCHAR *p;

    for (p = name; *p; p++)
    {
        h1 = (h1 ^ *p) * 16777619u;
        h2 = (h2 ^ *p) * 0x01000193u + (h2 >> 7);
    }
    guid->Data1 = 0x3a1c0000 | (flow << 8) | (id & 0xff);
    guid->Data2 = 0x52f5;   /* (arbitrary: "ReactOS winmm endpoint") */
    guid->Data3 = 0x4e31;
    guid->Data4[0] = 0x8a;
    guid->Data4[1] = 0x6d;
    guid->Data4[2] = (BYTE)(h1 >> 24);
    guid->Data4[3] = (BYTE)(h1 >> 16);
    guid->Data4[4] = (BYTE)(h1 >> 8);
    guid->Data4[5] = (BYTE)h1;
    guid->Data4[6] = (BYTE)(h2 >> 8);
    guid->Data4[7] = (BYTE)h2;
}

static WinMMDevice *get_device_from_guid(const GUID *guid)
{
    WinMMDevice *dev;
    LIST_FOR_EACH_ENTRY(dev, &g_devices, WinMMDevice, entry)
        if (IsEqualGUID(guid, &dev->guid))
            return dev;
    return NULL;
}

HRESULT WINAPI WINMM_GetEndpointIDs(EDataFlow flow, WCHAR ***ids, GUID **guids,
        UINT *num, UINT *def_index)
{
    UINT count, i, n;
    DWORD preferred = (DWORD)-1, pref_flags = 0;

    TRACE("%d %p %p %p %p\n", flow, ids, guids, num, def_index);

    *num = 0;
    *def_index = 0;
    *ids = NULL;
    *guids = NULL;

    /* No capture endpoints yet */
    if (flow != eRender)
        return S_OK;

    count = waveOutGetNumDevs();
    TRACE("%u waveOut devices\n", count);
    if (!count)
        return S_OK;

    *ids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(WCHAR *));
    *guids = HeapAlloc(GetProcessHeap(), 0, count * sizeof(GUID));
    if (!*ids || !*guids)
    {
        HeapFree(GetProcessHeap(), 0, *ids);
        HeapFree(GetProcessHeap(), 0, *guids);
        *ids = NULL;
        *guids = NULL;
        return E_OUTOFMEMORY;
    }

    for (i = 0, n = 0; i < count; i++)
    {
        WAVEOUTCAPSW caps;
        WCHAR name[MAXPNAMELEN + 16];
        WinMMDevice *dev;
        UINT j;

        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;

        /* (two cards of the same kind: tell them apart) */
        lstrcpynW(name, caps.szPname, MAXPNAMELEN);
        if (!name[0])
            lstrcpyW(name, L"Speakers");
        for (j = 0; j < n; j++)
        {
            if (!lstrcmpW((*ids)[j], name))
            {
                wsprintfW(name + lstrlenW(name), L" (%u)", i + 1);
                break;
            }
        }

        (*ids)[n] = HeapAlloc(GetProcessHeap(), 0, (lstrlenW(name) + 1) * sizeof(WCHAR));
        if (!(*ids)[n])
            break;
        lstrcpyW((*ids)[n], name);
        make_device_guid(flow, i, name, &(*guids)[n]);

        dev = get_device_from_guid(&(*guids)[n]);
        if (!dev)
        {
            dev = HeapAlloc(GetProcessHeap(), 0, sizeof(*dev));
            if (!dev)
            {
                HeapFree(GetProcessHeap(), 0, (*ids)[n]);
                break;
            }
            dev->guid = (*guids)[n];
            list_add_tail(&g_devices, &dev->entry);
        }
        dev->flow = flow;
        dev->id = i;

        TRACE("endpoint %u: waveOut %u %s %s\n", n, i, debugstr_w(name), debugstr_guid(&dev->guid));
        n++;
    }
    *num = n;

    /* The device chosen in the Sound control panel */
    if (waveOutMessage((HWAVEOUT)UlongToHandle(WAVE_MAPPER), DRVM_MAPPER_PREFERRED_GET,
                       (DWORD_PTR)&preferred, (DWORD_PTR)&pref_flags) == MMSYSERR_NOERROR)
    {
        for (i = 0; i < n; i++)
        {
            WinMMDevice *dev = get_device_from_guid(&(*guids)[i]);
            if (dev && dev->id == preferred)
            {
                *def_index = i;
                break;
            }
        }
    }

    return S_OK;
}

HRESULT WINAPI WINMM_GetPropValue(GUID *guid, const PROPERTYKEY *prop, PROPVARIANT *out)
{
    TRACE("%s, (%s,%u), %p\n", debugstr_guid(guid), debugstr_guid(&prop->fmtid), prop->pid, out);

    if (IsEqualPropertyKey(*prop, PKEY_AudioEndpoint_PhysicalSpeakers))
    {
        out->vt = VT_UI4;
        out->u.ulVal = KSAUDIO_SPEAKER_STEREO;
        return S_OK;
    }

    return E_NOTIMPL;
}

/* The mix format: 32-bit float stereo, at the device's rate (48 kHz, or
 * 44.1 kHz for devices that only say they can do that) */
static void get_mix_format(UINT wave_id, WAVEFORMATEXTENSIBLE *fmt)
{
    WAVEOUTCAPSW caps;
    DWORD rate = 48000;

    if (waveOutGetDevCapsW(wave_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR &&
        !(caps.dwFormats & WAVE_FORMAT_48S16) && (caps.dwFormats & WAVE_FORMAT_4S16))
        rate = 44100;

    fmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt->Format.nChannels = 2;
    fmt->Format.nSamplesPerSec = rate;
    fmt->Format.wBitsPerSample = 32;
    fmt->Format.nBlockAlign = fmt->Format.nChannels * fmt->Format.wBitsPerSample / 8;
    fmt->Format.nAvgBytesPerSec = fmt->Format.nSamplesPerSec * fmt->Format.nBlockAlign;
    fmt->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmt->Samples.wValidBitsPerSample = 32;
    fmt->dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    fmt->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

HRESULT WINAPI WINMM_GetAudioEndpoint(void *key, IMMDevice *dev, IAudioClient **out)
{
    ACImpl *This;
    const WinMMDevice *wdev;
    HRESULT hr;

    TRACE("%s %p %p\n", debugstr_guid(key), dev, out);

    wdev = get_device_from_guid(key);
    if (!wdev)
    {
        WARN("Unknown GUID: %s\n", debugstr_guid(key));
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ACImpl));
    if (!This)
        return E_OUTOFMEMORY;

    hr = CoCreateFreeThreadedMarshaler((IUnknown *)&This->IAudioClient3_iface, &This->pUnkFTMarshal);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, This);
        return hr;
    }

    This->dataflow = wdev->flow;
    This->wave_id = wdev->id;

    This->IAudioClient3_iface.lpVtbl = &AudioClient3_Vtbl;
    This->IAudioRenderClient_iface.lpVtbl = &AudioRenderClient_Vtbl;
    This->IAudioClock_iface.lpVtbl = &AudioClock_Vtbl;
    This->IAudioClock2_iface.lpVtbl = &AudioClock2_Vtbl;
    This->IAudioStreamVolume_iface.lpVtbl = &AudioStreamVolume_Vtbl;

    InitializeCriticalSection(&This->lock);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": ACImpl.lock");

    This->parent = dev;
    IMMDevice_AddRef(This->parent);

    IAudioClient3_AddRef(&This->IAudioClient3_iface);

    *out = (IAudioClient *)&This->IAudioClient3_iface;

    return S_OK;
}

static HRESULT WINAPI AudioClient_QueryInterface(IAudioClient3 *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioClient) ||
        IsEqualIID(riid, &IID_IAudioClient2) ||
        IsEqualIID(riid, &IID_IAudioClient3))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->pUnkFTMarshal, riid, ppv);
    if (*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }
    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClient_AddRef(IAudioClient3 *iface)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static void close_wave(ACImpl *This)
{
    UINT i;

    if (This->thread)
    {
        SetEvent(This->quit_event);
        WaitForSingleObject(This->thread, INFINITE);
        CloseHandle(This->thread);
        This->thread = NULL;
    }
    if (This->hwo)
    {
        waveOutReset(This->hwo);
        for (i = 0; i < NUM_WAVEHDRS; i++)
        {
            if (This->hdrs[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(This->hwo, &This->hdrs[i], sizeof(WAVEHDR));
        }
        waveOutClose(This->hwo);
        This->hwo = NULL;
    }
    if (This->quit_event)
    {
        CloseHandle(This->quit_event);
        This->quit_event = NULL;
    }
    if (This->wave_event)
    {
        CloseHandle(This->wave_event);
        This->wave_event = NULL;
    }
    HeapFree(GetProcessHeap(), 0, This->hdr_mem);
    This->hdr_mem = NULL;
}

static ULONG WINAPI AudioClient_Release(IAudioClient3 *iface)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    ULONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if (!ref)
    {
        close_wave(This);
        IMMDevice_Release(This->parent);
        IUnknown_Release(This->pUnkFTMarshal);
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        if (This->initted)
        {
            EnterCriticalSection(&g_sessions_lock);
            list_remove(&This->entry);
            LeaveCriticalSection(&g_sessions_lock);
        }
        HeapFree(GetProcessHeap(), 0, This->vols);
        HeapFree(GetProcessHeap(), 0, This->local_buffer);
        HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
        CoTaskMemFree(This->fmt);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void dump_fmt(const WAVEFORMATEX *fmt)
{
    TRACE("wFormatTag: 0x%x, nChannels: %u, nSamplesPerSec: %u, nAvgBytesPerSec: %u, "
          "nBlockAlign: %u, wBitsPerSample: %u, cbSize: %u\n", fmt->wFormatTag, fmt->nChannels,
          fmt->nSamplesPerSec, fmt->nAvgBytesPerSec, fmt->nBlockAlign, fmt->wBitsPerSample,
          fmt->cbSize);

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE *fmtex = (const void*)fmt;
        TRACE("dwChannelMask: %08x, Samples: %04x, SubFormat: %s\n", fmtex->dwChannelMask,
              fmtex->Samples.wReserved, debugstr_guid(&fmtex->SubFormat));
    }
}

/* The kinds of samples we can play */
enum sample_type
{
    SAMPLE_INVALID,
    SAMPLE_U8,
    SAMPLE_S16,
    SAMPLE_S24,
    SAMPLE_S32,
    SAMPLE_FLOAT
};

static enum sample_type get_sample_type(const WAVEFORMATEX *fmt)
{
    const WAVEFORMATEXTENSIBLE *fmtex = (const WAVEFORMATEXTENSIBLE*)fmt;

    if (fmt->wFormatTag == WAVE_FORMAT_PCM ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         IsEqualGUID(&fmtex->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)))
    {
        switch (fmt->wBitsPerSample)
        {
        case 8: return SAMPLE_U8;
        case 16: return SAMPLE_S16;
        case 24: return SAMPLE_S24;
        case 32: return SAMPLE_S32;
        }
        return SAMPLE_INVALID;
    }

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         IsEqualGUID(&fmtex->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)))
    {
        if (fmt->wBitsPerSample == 32)
            return SAMPLE_FLOAT;
    }

    return SAMPLE_INVALID;
}

static WAVEFORMATEX *clone_format(const WAVEFORMATEX *fmt)
{
    WAVEFORMATEX *ret;
    size_t size;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        size = sizeof(WAVEFORMATEXTENSIBLE);
    else
        size = sizeof(WAVEFORMATEX);

    ret = CoTaskMemAlloc(size);
    if (!ret)
        return NULL;

    memcpy(ret, fmt, size);

    ret->cbSize = size - sizeof(WAVEFORMATEX);

    return ret;
}

/* Every format we can turn into 16-bit PCM is fine: winmm (kmixer) converts
 * the rate */
static HRESULT check_format(const WAVEFORMATEX *fmt)
{
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        (fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) ||
         fmt->nAvgBytesPerSec == 0 || fmt->nBlockAlign == 0 ||
         ((const WAVEFORMATEXTENSIBLE*)fmt)->Samples.wValidBitsPerSample > fmt->wBitsPerSample))
        return E_INVALIDARG;

    if (get_sample_type(fmt) == SAMPLE_INVALID)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    if (fmt->nChannels == 0 || fmt->nChannels > 8 ||
        fmt->nSamplesPerSec < 8000 || fmt->nSamplesPerSec > 192000 ||
        fmt->nBlockAlign != fmt->nChannels * fmt->wBitsPerSample / 8 ||
        fmt->nAvgBytesPerSec != fmt->nBlockAlign * fmt->nSamplesPerSec)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    return S_OK;
}
static void session_init_vols(AudioSession *session, UINT channels)
{
    if(session->channel_count < channels){
        UINT i;

        if(session->channel_vols)
            session->channel_vols = HeapReAlloc(GetProcessHeap(), 0,
                    session->channel_vols, sizeof(float) * channels);
        else
            session->channel_vols = HeapAlloc(GetProcessHeap(), 0,
                    sizeof(float) * channels);
        if(!session->channel_vols)
            return;

        for(i = session->channel_count; i < channels; ++i)
            session->channel_vols[i] = 1.f;

        session->channel_count = channels;
    }
}

static AudioSession *create_session(const GUID *guid, IMMDevice *device,
        UINT num_channels)
{
    AudioSession *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AudioSession));
    if(!ret)
        return NULL;

    memcpy(&ret->guid, guid, sizeof(GUID));

    ret->device = device;

    list_init(&ret->clients);

    list_add_head(&g_sessions, &ret->entry);

    InitializeCriticalSection(&ret->lock);
    ret->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": AudioSession.lock");

    session_init_vols(ret, num_channels);

    ret->master_vol = 1.f;

    return ret;
}

/* if channels == 0, then this will return or create a session with
 * matching dataflow and GUID. otherwise, channels must also match */
static HRESULT get_audio_session(const GUID *sessionguid,
        IMMDevice *device, UINT channels, AudioSession **out)
{
    AudioSession *session;

    if(!sessionguid || IsEqualGUID(sessionguid, &GUID_NULL)){
        *out = create_session(&GUID_NULL, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;

        return S_OK;
    }

    *out = NULL;
    LIST_FOR_EACH_ENTRY(session, &g_sessions, AudioSession, entry){
        if(session->device == device &&
                IsEqualGUID(sessionguid, &session->guid)){
            session_init_vols(session, channels);
            *out = session;
            break;
        }
    }

    if(!*out){
        *out = create_session(sessionguid, device, channels);
        if(!*out)
            return E_OUTOFMEMORY;
    }

    return S_OK;
}


/* The program's samples (any format we accept) as 16-bit PCM for waveOut,
 * with the volumes */
static float read_sample(enum sample_type type, const BYTE *p)
{
    switch (type)
    {
    case SAMPLE_U8:
        return (p[0] - 128) / 128.f;
    case SAMPLE_S16:
        return *(const INT16 *)p / 32768.f;
    case SAMPLE_S24:
        return (INT32)(((UINT32)p[0] << 8) | ((UINT32)p[1] << 16) | ((UINT32)p[2] << 24)) / 2147483648.f;
    case SAMPLE_S32:
        return *(const INT32 *)p / 2147483648.f;
    case SAMPLE_FLOAT:
        return *(const float *)p;
    default:
        return 0.f;
    }
}

static void convert_frames(ACImpl *This, const BYTE *src, UINT32 frames, INT16 *dst)
{
    enum sample_type type = get_sample_type(This->fmt);
    UINT in_ch = This->fmt->nChannels, out_ch = This->out_fmt.nChannels;
    UINT bytes = This->fmt->wBitsPerSample / 8, c;
    AudioSession *session = This->session;
    float gain[2];

    for (c = 0; c < out_ch; c++)
    {
        if (session->mute)
            gain[c] = 0.f;
        else
            gain[c] = session->master_vol * This->vols[c] *
                      (c < session->channel_count ? session->channel_vols[c] : 1.f);
    }

    /* (more than 2 channels: the front left and right ones) */
    while (frames--)
    {
        for (c = 0; c < out_ch && c < in_ch; c++)
        {
            float v = read_sample(type, src + c * bytes) * gain[c];
            if (v >= 1.f)
                *dst++ = 32767;
            else if (v <= -1.f)
                *dst++ = -32768;
            else
                *dst++ = (INT16)(v * 32767.f);
        }
        src += This->fmt->nBlockAlign;
    }
}

/* waveOut buffers done: their frames have been played */
static void reap_done(ACImpl *This)
{
    while (This->hdr_queued)
    {
        WAVEHDR *hdr = &This->hdrs[This->hdr_first];

        if (!(hdr->dwFlags & WHDR_DONE) || (hdr->dwFlags & WHDR_INQUEUE))
            break;
        This->played_frames += This->hdr_frames[This->hdr_first];
        This->hdr_first = (This->hdr_first + 1) % NUM_WAVEHDRS;
        This->hdr_queued--;
    }
}

/* Send what the program gave to waveOut, a period per buffer */
static void feed(ACImpl *This)
{
    while (This->hdr_queued < NUM_WAVEHDRS && This->held_frames)
    {
        UINT idx = (This->hdr_first + This->hdr_queued) % NUM_WAVEHDRS;
        WAVEHDR *hdr = &This->hdrs[idx];
        UINT32 frames = min(This->held_frames, This->period_frames), chunk;
        MMRESULT mr;

        /* Less than a period: wait for more, unless the device runs dry */
        if (frames < This->period_frames && This->hdr_queued >= 2)
            break;

        chunk = min(frames, This->bufsize_frames - This->lcl_offs_frames);
        convert_frames(This, This->local_buffer + This->lcl_offs_frames * This->fmt->nBlockAlign,
                       chunk, (INT16 *)hdr->lpData);
        if (frames > chunk)
            convert_frames(This, This->local_buffer, frames - chunk,
                           (INT16 *)hdr->lpData + chunk * This->out_fmt.nChannels);

        hdr->dwBufferLength = frames * This->out_fmt.nBlockAlign;
        hdr->dwFlags &= ~WHDR_DONE;
        mr = waveOutWrite(This->hwo, hdr, sizeof(WAVEHDR));
        if (mr != MMSYSERR_NOERROR)
        {
            WARN("waveOutWrite failed: %u\n", mr);
            if (!(hdr->dwFlags & WHDR_INQUEUE))
                hdr->dwFlags |= WHDR_DONE;
            break;
        }

        This->hdr_frames[idx] = frames;
        This->hdr_queued++;
        This->lcl_offs_frames = (This->lcl_offs_frames + frames) % This->bufsize_frames;
        This->held_frames -= frames;
        This->submitted_frames += frames;
    }
}

/* The stream's thread: refills the waveOut buffers as they finish and asks
 * the program for more (its event), every period */
static DWORD WINAPI wave_thread(void *user)
{
    ACImpl *This = user;
    HANDLE handles[2] = { This->quit_event, This->wave_event };
    DWORD period_ms = max(1, MulDiv(This->period_frames, 1000, This->fmt->nSamplesPerSec));
    HANDLE event;

    for (;;)
    {
        if (WaitForMultipleObjects(2, handles, FALSE, period_ms) == WAIT_OBJECT_0)
            break;

        EnterCriticalSection(&This->lock);
        reap_done(This);
        if (This->playing)
            feed(This);
        event = This->playing ? This->event : NULL;
        LeaveCriticalSection(&This->lock);

        if (event)
            SetEvent(event);
    }

    return 0;
}

static HRESULT open_wave(ACImpl *This)
{
    UINT32 hdr_bytes;
    MMRESULT mr;
    UINT i;

    This->out_fmt.wFormatTag = WAVE_FORMAT_PCM;
    This->out_fmt.nChannels = This->fmt->nChannels == 1 ? 1 : 2;
    This->out_fmt.nSamplesPerSec = This->fmt->nSamplesPerSec;
    This->out_fmt.wBitsPerSample = 16;
    This->out_fmt.nBlockAlign = This->out_fmt.nChannels * 2;
    This->out_fmt.nAvgBytesPerSec = This->out_fmt.nSamplesPerSec * This->out_fmt.nBlockAlign;
    This->out_fmt.cbSize = 0;

    This->wave_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    This->quit_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!This->wave_event || !This->quit_event)
        return E_OUTOFMEMORY;

    mr = waveOutOpen(&This->hwo, This->wave_id, &This->out_fmt, (DWORD_PTR)This->wave_event,
                     0, CALLBACK_EVENT);
    if (mr != MMSYSERR_NOERROR)
    {
        WARN("waveOutOpen(%u, %u Hz, %u channels) failed: %u\n", This->wave_id,
             This->out_fmt.nSamplesPerSec, This->out_fmt.nChannels, mr);
        This->hwo = NULL;
        return mr == MMSYSERR_ALLOCATED ? AUDCLNT_E_DEVICE_IN_USE : AUDCLNT_E_ENDPOINT_CREATE_FAILED;
    }

    hdr_bytes = This->period_frames * This->out_fmt.nBlockAlign;
    This->hdr_mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NUM_WAVEHDRS * hdr_bytes);
    if (!This->hdr_mem)
        return E_OUTOFMEMORY;

    for (i = 0; i < NUM_WAVEHDRS; i++)
    {
        This->hdrs[i].lpData = (LPSTR)(This->hdr_mem + i * hdr_bytes);
        This->hdrs[i].dwBufferLength = hdr_bytes;
        mr = waveOutPrepareHeader(This->hwo, &This->hdrs[i], sizeof(WAVEHDR));
        if (mr != MMSYSERR_NOERROR)
        {
            WARN("waveOutPrepareHeader failed: %u\n", mr);
            return AUDCLNT_E_ENDPOINT_CREATE_FAILED;
        }
        This->hdrs[i].dwFlags |= WHDR_DONE;
    }

    This->thread = CreateThread(NULL, 0, wave_thread, This, 0, NULL);
    if (!This->thread)
        return E_OUTOFMEMORY;
    SetThreadPriority(This->thread, THREAD_PRIORITY_TIME_CRITICAL);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Initialize(IAudioClient3 *iface,
        AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
        REFERENCE_TIME period, const WAVEFORMATEX *fmt,
        const GUID *sessionguid)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    int i;
    HRESULT hr;

    TRACE("(%p)->(%x, %x, %s, %s, %p, %s)\n", This, mode, flags,
          wine_dbgstr_longlong(duration), wine_dbgstr_longlong(period), fmt, debugstr_guid(sessionguid));

    if (!fmt)
        return E_POINTER;

    dump_fmt(fmt);

    if (mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_NOT_INITIALIZED;

    if (flags & ~(AUDCLNT_STREAMFLAGS_CROSSPROCESS |
                  AUDCLNT_STREAMFLAGS_LOOPBACK |
                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                  AUDCLNT_STREAMFLAGS_NOPERSIST |
                  AUDCLNT_STREAMFLAGS_RATEADJUST |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_SESSIONFLAGS_EXPIREWHENUNOWNED |
                  AUDCLNT_SESSIONFLAGS_DISPLAY_HIDE |
                  AUDCLNT_SESSIONFLAGS_DISPLAY_HIDEWHENEXPIRED))
    {
        TRACE("Unknown flags: %08x\n", flags);
        return E_INVALIDARG;
    }

    /* Only the shared mode: waveOut always goes through the mixer */
    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE)
    {
        TRACE("No exclusive mode\n");
        return AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;
    }

    if (flags & AUDCLNT_STREAMFLAGS_LOOPBACK)
    {
        FIXME("Loopback capture is not supported\n");
        return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
    }

    period = DefaultPeriod;
    if (duration < 5 * period)
        duration = 5 * period;
    if (duration > 20000000)
        duration = 20000000;

    hr = check_format(fmt);
    if (FAILED(hr))
        return hr;

    EnterCriticalSection(&This->lock);

    if (This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }

    This->fmt = clone_format(fmt);
    if (!This->fmt)
    {
        LeaveCriticalSection(&This->lock);
        return E_OUTOFMEMORY;
    }

    This->period_frames = MulDiv(fmt->nSamplesPerSec, period, 10000000);
    This->bufsize_frames = MulDiv(duration, fmt->nSamplesPerSec, 10000000);
    This->local_buffer = HeapAlloc(GetProcessHeap(), 0, This->bufsize_frames * fmt->nBlockAlign);
    This->vols = HeapAlloc(GetProcessHeap(), 0, fmt->nChannels * sizeof(float));
    if (!This->local_buffer || !This->vols)
    {
        hr = E_OUTOFMEMORY;
        goto fail;
    }

    for (i = 0; i < fmt->nChannels; ++i)
        This->vols[i] = 1.f;

    This->share = mode;
    This->flags = flags;

    EnterCriticalSection(&g_sessions_lock);
    hr = get_audio_session(sessionguid, This->parent, fmt->nChannels, &This->session);
    LeaveCriticalSection(&g_sessions_lock);
    if (FAILED(hr))
        goto fail;

    hr = open_wave(This);
    if (FAILED(hr))
        goto fail;

    EnterCriticalSection(&g_sessions_lock);
    list_add_tail(&This->session->clients, &This->entry);
    LeaveCriticalSection(&g_sessions_lock);

    This->initted = TRUE;

    TRACE("period %u frames, buffer %u frames\n", This->period_frames, This->bufsize_frames);

    LeaveCriticalSection(&This->lock);

    return S_OK;

fail:
    LeaveCriticalSection(&This->lock);
    close_wave(This);
    HeapFree(GetProcessHeap(), 0, This->vols);
    This->vols = NULL;
    HeapFree(GetProcessHeap(), 0, This->local_buffer);
    This->local_buffer = NULL;
    CoTaskMemFree(This->fmt);
    This->fmt = NULL;
    This->session = NULL;
    return hr;
}

static HRESULT WINAPI AudioClient_GetBufferSize(IAudioClient3 *iface,
        UINT32 *frames)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p)\n", This, frames);

    if (!frames)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    *frames = This->bufsize_frames;

    TRACE("buffer size: %u\n", *frames);

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetStreamLatency(IAudioClient3 *iface,
        REFERENCE_TIME *latency)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p)\n", This, latency);

    if (!latency)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    /* (the waveOut buffers: about two are being played at any time) */
    *latency = 2 * DefaultPeriod;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetCurrentPadding(IAudioClient3 *iface,
        UINT32 *numpad)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p)\n", This, numpad);

    if (!numpad)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    *numpad = This->held_frames;

    TRACE("padding: %u\n", *numpad);

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_IsFormatSupported(IAudioClient3 *iface,
        AUDCLNT_SHAREMODE mode, const WAVEFORMATEX *pwfx,
        WAVEFORMATEX **outpwfx)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    WAVEFORMATEXTENSIBLE *closest;
    HRESULT hr;

    TRACE("(%p)->(%x, %p, %p)\n", This, mode, pwfx, outpwfx);

    if (!pwfx || (mode == AUDCLNT_SHAREMODE_SHARED && !outpwfx))
        return E_POINTER;

    if (mode != AUDCLNT_SHAREMODE_SHARED && mode != AUDCLNT_SHAREMODE_EXCLUSIVE)
        return E_INVALIDARG;

    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        pwfx->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return E_INVALIDARG;

    dump_fmt(pwfx);

    if (outpwfx)
        *outpwfx = NULL;

    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE)
        return AUDCLNT_E_UNSUPPORTED_FORMAT;

    hr = check_format(pwfx);
    if (hr != AUDCLNT_E_UNSUPPORTED_FORMAT)
        return hr;

    /* Not that one: the mix format would do */
    closest = CoTaskMemAlloc(sizeof(*closest));
    if (!closest)
        return E_OUTOFMEMORY;
    get_mix_format(This->wave_id, closest);
    *outpwfx = &closest->Format;
    return S_FALSE;
}

static HRESULT WINAPI AudioClient_GetMixFormat(IAudioClient3 *iface,
        WAVEFORMATEX **pwfx)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    WAVEFORMATEXTENSIBLE *fmt;

    TRACE("(%p)->(%p)\n", This, pwfx);

    if (!pwfx)
        return E_POINTER;
    *pwfx = NULL;

    fmt = CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
    if (!fmt)
        return E_OUTOFMEMORY;

    get_mix_format(This->wave_id, fmt);

    *pwfx = (WAVEFORMATEX*)fmt;
    dump_fmt(*pwfx);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetDevicePeriod(IAudioClient3 *iface,
        REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p, %p)\n", This, defperiod, minperiod);

    if (!defperiod && !minperiod)
        return E_POINTER;

    if (defperiod)
        *defperiod = DefaultPeriod;
    if (minperiod)
        *minperiod = MinimumPeriod;

    return S_OK;
}

static void silence_buffer(ACImpl *This, BYTE *buffer, UINT32 frames)
{
    if (get_sample_type(This->fmt) == SAMPLE_U8)
        memset(buffer, 128, frames * This->fmt->nBlockAlign);
    else
        memset(buffer, 0, frames * This->fmt->nBlockAlign);
}

static HRESULT WINAPI AudioClient_Start(IAudioClient3 *iface)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if ((This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !This->event)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_SET;
    }

    if (This->playing)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if (This->paused)
    {
        waveOutRestart(This->hwo);
        This->paused = FALSE;
    }

    This->playing = TRUE;
    feed(This);

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Stop(IAudioClient3 *iface)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if (!This->playing)
    {
        LeaveCriticalSection(&This->lock);
        return S_FALSE;
    }

    This->playing = FALSE;
    if (!This->paused)
    {
        waveOutPause(This->hwo);
        This->paused = TRUE;
    }

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_Reset(IAudioClient3 *iface)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if (This->playing)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_STOPPED;
    }

    if (This->getbuf_last)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }

    /* Throw away what waveOut still has */
    if (This->hdr_queued || This->submitted_frames)
    {
        waveOutReset(This->hwo);
        if (This->paused)
            waveOutPause(This->hwo);
    }
    This->hdr_first = 0;
    This->hdr_queued = 0;

    This->written_frames = 0;
    This->submitted_frames = 0;
    This->played_frames = 0;
    This->last_pos_frames = 0;
    This->held_frames = 0;
    This->lcl_offs_frames = 0;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_SetEventHandle(IAudioClient3 *iface,
        HANDLE event)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p)\n", This, event);

    if (!event)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if (!(This->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK))
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED;
    }

    This->event = event;

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetService(IAudioClient3 *iface, REFIID riid,
        void **ppv)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    EnterCriticalSection(&This->lock);

    if (!This->initted)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_NOT_INITIALIZED;
    }

    if (IsEqualIID(riid, &IID_IAudioRenderClient))
    {
        IAudioRenderClient_AddRef(&This->IAudioRenderClient_iface);
        *ppv = &This->IAudioRenderClient_iface;
    }
    else if (IsEqualIID(riid, &IID_IAudioCaptureClient))
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
    }
    else if (IsEqualIID(riid, &IID_IAudioClock))
    {
        IAudioClock_AddRef(&This->IAudioClock_iface);
        *ppv = &This->IAudioClock_iface;
    }
    else if (IsEqualIID(riid, &IID_IAudioStreamVolume))
    {
        IAudioStreamVolume_AddRef(&This->IAudioStreamVolume_iface);
        *ppv = &This->IAudioStreamVolume_iface;
    }
    else if (IsEqualIID(riid, &IID_IAudioSessionControl) ||
             IsEqualIID(riid, &IID_IChannelAudioVolume) ||
             IsEqualIID(riid, &IID_ISimpleAudioVolume))
    {
        if (!This->session_wrapper)
        {
            This->session_wrapper = AudioSessionWrapper_Create(This);
            if (!This->session_wrapper)
            {
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
        }
        else
            IAudioSessionControl2_AddRef(&This->session_wrapper->IAudioSessionControl2_iface);

        if (IsEqualIID(riid, &IID_IAudioSessionControl))
            *ppv = &This->session_wrapper->IAudioSessionControl2_iface;
        else if (IsEqualIID(riid, &IID_IChannelAudioVolume))
            *ppv = &This->session_wrapper->IChannelAudioVolume_iface;
        else
            *ppv = &This->session_wrapper->ISimpleAudioVolume_iface;
    }

    LeaveCriticalSection(&This->lock);

    if (*ppv)
        return S_OK;

    FIXME("stub %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static HRESULT WINAPI AudioClient_IsOffloadCapable(IAudioClient3 *iface,
        AUDIO_STREAM_CATEGORY category, BOOL *offload_capable)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(0x%x, %p)\n", This, category, offload_capable);

    if (!offload_capable)
        return E_INVALIDARG;

    *offload_capable = FALSE;

    return S_OK;
}

static HRESULT WINAPI AudioClient_SetClientProperties(IAudioClient3 *iface,
        const AudioClientProperties *prop)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(%p)\n", This, prop);

    if (!prop)
        return E_POINTER;

    TRACE("cbSize %u, offload %d, category %u\n", prop->cbSize, prop->bIsOffload, prop->eCategory);

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetBufferSizeLimits(IAudioClient3 *iface,
        const WAVEFORMATEX *format, BOOL event_driven, REFERENCE_TIME *min_duration,
        REFERENCE_TIME *max_duration)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    FIXME("(%p)->(%p, %u, %p, %p) - stub\n", This, format, event_driven, min_duration, max_duration);

    /* (the hardware offload mode) */
    return AUDCLNT_E_OFFLOAD_MODE_ONLY;
}

static HRESULT WINAPI AudioClient_GetSharedModeEnginePeriod(IAudioClient3 *iface,
        const WAVEFORMATEX *format, UINT32 *default_period_frames, UINT32 *unit_period_frames,
        UINT32 *min_period_frames, UINT32 *max_period_frames)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    UINT32 period;

    TRACE("(%p)->(%p, %p, %p, %p, %p)\n", This, format, default_period_frames,
          unit_period_frames, min_period_frames, max_period_frames);

    if (!format || !default_period_frames || !unit_period_frames ||
        !min_period_frames || !max_period_frames)
        return E_POINTER;

    /* One period only */
    period = MulDiv(format->nSamplesPerSec, DefaultPeriod, 10000000);
    *default_period_frames = period;
    *unit_period_frames = period;
    *min_period_frames = period;
    *max_period_frames = period;

    return S_OK;
}

static HRESULT WINAPI AudioClient_GetCurrentSharedModeEnginePeriod(IAudioClient3 *iface,
        WAVEFORMATEX **cur_format, UINT32 *cur_period_frames)
{
    ACImpl *This = impl_from_IAudioClient3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p)\n", This, cur_format, cur_period_frames);

    if (!cur_format || !cur_period_frames)
        return E_POINTER;

    hr = IAudioClient3_GetMixFormat(iface, cur_format);
    if (FAILED(hr))
        return hr;

    *cur_period_frames = MulDiv((*cur_format)->nSamplesPerSec, DefaultPeriod, 10000000);

    return S_OK;
}

static HRESULT WINAPI AudioClient_InitializeSharedAudioStream(IAudioClient3 *iface,
        DWORD flags, UINT32 period_frames, const WAVEFORMATEX *format,
        const GUID *session_guid)
{
    ACImpl *This = impl_from_IAudioClient3(iface);

    TRACE("(%p)->(0x%x, %u, %p, %s)\n", This, flags, period_frames, format,
          debugstr_guid(session_guid));

    if (!format)
        return E_POINTER;

    /* (the period is always the same: 10 ms) */
    return IAudioClient3_Initialize(iface, AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                                    format, session_guid);
}

static const IAudioClient3Vtbl AudioClient3_Vtbl =
{
    AudioClient_QueryInterface,
    AudioClient_AddRef,
    AudioClient_Release,
    AudioClient_Initialize,
    AudioClient_GetBufferSize,
    AudioClient_GetStreamLatency,
    AudioClient_GetCurrentPadding,
    AudioClient_IsFormatSupported,
    AudioClient_GetMixFormat,
    AudioClient_GetDevicePeriod,
    AudioClient_Start,
    AudioClient_Stop,
    AudioClient_Reset,
    AudioClient_SetEventHandle,
    AudioClient_GetService,
    AudioClient_IsOffloadCapable,
    AudioClient_SetClientProperties,
    AudioClient_GetBufferSizeLimits,
    AudioClient_GetSharedModeEnginePeriod,
    AudioClient_GetCurrentSharedModeEnginePeriod,
    AudioClient_InitializeSharedAudioStream
};

static HRESULT WINAPI AudioRenderClient_QueryInterface(
        IAudioRenderClient *iface, REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IAudioRenderClient))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IMarshal))
        return IUnknown_QueryInterface(This->pUnkFTMarshal, riid, ppv);
    if (*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioRenderClient_AddRef(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_AddRef(&This->IAudioClient3_iface);
}

static ULONG WINAPI AudioRenderClient_Release(IAudioRenderClient *iface)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    return AudioClient_Release(&This->IAudioClient3_iface);
}

static HRESULT WINAPI AudioRenderClient_GetBuffer(IAudioRenderClient *iface,
        UINT32 frames, BYTE **data)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    UINT32 write_pos;

    TRACE("(%p)->(%u, %p)\n", This, frames, data);

    if (!data)
        return E_POINTER;

    *data = NULL;

    EnterCriticalSection(&This->lock);

    if (This->getbuf_last)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    if (!frames)
    {
        LeaveCriticalSection(&This->lock);
        return S_OK;
    }

    if (This->held_frames + frames > This->bufsize_frames)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }

    write_pos = (This->lcl_offs_frames + This->held_frames) % This->bufsize_frames;
    if (write_pos + frames > This->bufsize_frames)
    {
        if (This->tmp_buffer_frames < frames)
        {
            HeapFree(GetProcessHeap(), 0, This->tmp_buffer);
            This->tmp_buffer = HeapAlloc(GetProcessHeap(), 0, frames * This->fmt->nBlockAlign);
            if (!This->tmp_buffer)
            {
                This->tmp_buffer_frames = 0;
                LeaveCriticalSection(&This->lock);
                return E_OUTOFMEMORY;
            }
            This->tmp_buffer_frames = frames;
        }
        *data = This->tmp_buffer;
        This->getbuf_last = -frames;
    }
    else
    {
        *data = This->local_buffer + write_pos * This->fmt->nBlockAlign;
        This->getbuf_last = frames;
    }

    silence_buffer(This, *data, frames);

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static void wrap_buffer(ACImpl *This, BYTE *buffer, UINT32 written_frames)
{
    UINT32 write_offs_frames =
        (This->lcl_offs_frames + This->held_frames) % This->bufsize_frames;
    UINT32 write_offs_bytes = write_offs_frames * This->fmt->nBlockAlign;
    UINT32 chunk_frames = This->bufsize_frames - write_offs_frames;
    UINT32 chunk_bytes = chunk_frames * This->fmt->nBlockAlign;
    UINT32 written_bytes = written_frames * This->fmt->nBlockAlign;

    if (written_bytes <= chunk_bytes)
    {
        memcpy(This->local_buffer + write_offs_bytes, buffer, written_bytes);
    }
    else
    {
        memcpy(This->local_buffer + write_offs_bytes, buffer, chunk_bytes);
        memcpy(This->local_buffer, buffer + chunk_bytes, written_bytes - chunk_bytes);
    }
}

static HRESULT WINAPI AudioRenderClient_ReleaseBuffer(
        IAudioRenderClient *iface, UINT32 written_frames, DWORD flags)
{
    ACImpl *This = impl_from_IAudioRenderClient(iface);
    BYTE *buffer;

    TRACE("(%p)->(%u, %x)\n", This, written_frames, flags);

    EnterCriticalSection(&This->lock);

    if (!written_frames)
    {
        This->getbuf_last = 0;
        LeaveCriticalSection(&This->lock);
        return S_OK;
    }

    if (!This->getbuf_last)
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_OUT_OF_ORDER;
    }

    if (written_frames > (This->getbuf_last >= 0 ? This->getbuf_last : -This->getbuf_last))
    {
        LeaveCriticalSection(&This->lock);
        return AUDCLNT_E_INVALID_SIZE;
    }

    if (This->getbuf_last >= 0)
        buffer = This->local_buffer + This->fmt->nBlockAlign *
          ((This->lcl_offs_frames + This->held_frames) % This->bufsize_frames);
    else
        buffer = This->tmp_buffer;

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        silence_buffer(This, buffer, written_frames);

    if (This->getbuf_last < 0)
        wrap_buffer(This, buffer, written_frames);

    This->held_frames += written_frames;
    This->written_frames += written_frames;
    This->getbuf_last = 0;

    /* (playing: straight to waveOut if it has room) */
    if (This->playing)
        feed(This);

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static const IAudioRenderClientVtbl AudioRenderClient_Vtbl = {
    AudioRenderClient_QueryInterface,
    AudioRenderClient_AddRef,
    AudioRenderClient_Release,
    AudioRenderClient_GetBuffer,
    AudioRenderClient_ReleaseBuffer
};

static HRESULT WINAPI AudioClock_QueryInterface(IAudioClock *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioClock))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IAudioClock2))
        *ppv = &This->IAudioClock2_iface;
    if (*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioClock_AddRef(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return AudioClient_AddRef(&This->IAudioClient3_iface);
}

static ULONG WINAPI AudioClock_Release(IAudioClock *iface)
{
    ACImpl *This = impl_from_IAudioClock(iface);
    return AudioClient_Release(&This->IAudioClient3_iface);
}

static HRESULT WINAPI AudioClock_GetFrequency(IAudioClock *iface, UINT64 *freq)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, freq);

    if (!freq)
        return E_POINTER;

    if (This->share == AUDCLNT_SHAREMODE_SHARED)
        *freq = (UINT64)This->fmt->nSamplesPerSec * This->fmt->nBlockAlign;
    else
        *freq = This->fmt->nSamplesPerSec;

    return S_OK;
}

/* The frames played, in the program's rate: the waveOut buffers done, and
 * how far waveOut is in the next one */
static UINT64 get_position_frames(ACImpl *This)
{
    UINT64 pos;
    MMTIME time;

    reap_done(This);
    pos = This->played_frames;

    if (This->hwo && This->hdr_queued)
    {
        time.wType = TIME_SAMPLES;
        if (waveOutGetPosition(This->hwo, &time, sizeof(time)) == MMSYSERR_NOERROR)
        {
            UINT64 dev = (UINT64)-1;

            if (time.wType == TIME_SAMPLES)
                dev = time.u.sample;
            else if (time.wType == TIME_BYTES)
                dev = time.u.cb / This->out_fmt.nBlockAlign;
            /* (waveOut counts from its last reset, as we do) */
            if (dev > pos && dev <= This->submitted_frames)
                pos = dev;
        }
    }

    if (pos < This->last_pos_frames)
        pos = This->last_pos_frames;
    This->last_pos_frames = pos;

    return pos;
}

static HRESULT WINAPI AudioClock_GetPosition(IAudioClock *iface, UINT64 *pos,
        UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p, %p)\n", This, pos, qpctime);

    if (!pos)
        return E_POINTER;

    EnterCriticalSection(&This->lock);

    *pos = get_position_frames(This);

    TRACE("returning: %s\n", wine_dbgstr_longlong(*pos));
    if (This->share == AUDCLNT_SHAREMODE_SHARED)
        *pos *= This->fmt->nBlockAlign;

    LeaveCriticalSection(&This->lock);

    if (qpctime)
    {
        LARGE_INTEGER stamp, freq;
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return S_OK;
}

static HRESULT WINAPI AudioClock_GetCharacteristics(IAudioClock *iface,
        DWORD *chars)
{
    ACImpl *This = impl_from_IAudioClock(iface);

    TRACE("(%p)->(%p)\n", This, chars);

    if (!chars)
        return E_POINTER;

    *chars = AUDIOCLOCK_CHARACTERISTIC_FIXED_FREQ;

    return S_OK;
}

static const IAudioClockVtbl AudioClock_Vtbl =
{
    AudioClock_QueryInterface,
    AudioClock_AddRef,
    AudioClock_Release,
    AudioClock_GetFrequency,
    AudioClock_GetPosition,
    AudioClock_GetCharacteristics
};

static HRESULT WINAPI AudioClock2_QueryInterface(IAudioClock2 *iface,
        REFIID riid, void **ppv)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return IAudioClock_QueryInterface(&This->IAudioClock_iface, riid, ppv);
}

static ULONG WINAPI AudioClock2_AddRef(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return AudioClient_AddRef(&This->IAudioClient3_iface);
}

static ULONG WINAPI AudioClock2_Release(IAudioClock2 *iface)
{
    ACImpl *This = impl_from_IAudioClock2(iface);
    return AudioClient_Release(&This->IAudioClient3_iface);
}

static HRESULT WINAPI AudioClock2_GetDevicePosition(IAudioClock2 *iface,
        UINT64 *pos, UINT64 *qpctime)
{
    ACImpl *This = impl_from_IAudioClock2(iface);

    TRACE("(%p)->(%p, %p)\n", This, pos, qpctime);

    if (!pos)
        return E_POINTER;

    EnterCriticalSection(&This->lock);
    *pos = get_position_frames(This);
    LeaveCriticalSection(&This->lock);

    if (qpctime)
    {
        LARGE_INTEGER stamp, freq;
        QueryPerformanceCounter(&stamp);
        QueryPerformanceFrequency(&freq);
        *qpctime = (stamp.QuadPart * (INT64)10000000) / freq.QuadPart;
    }

    return S_OK;
}

static const IAudioClock2Vtbl AudioClock2_Vtbl =
{
    AudioClock2_QueryInterface,
    AudioClock2_AddRef,
    AudioClock2_Release,
    AudioClock2_GetDevicePosition
};

static AudioSessionWrapper *AudioSessionWrapper_Create(ACImpl *client)
{
    AudioSessionWrapper *ret;

    ret = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(AudioSessionWrapper));
    if(!ret)
        return NULL;

    ret->IAudioSessionControl2_iface.lpVtbl = &AudioSessionControl2_Vtbl;
    ret->ISimpleAudioVolume_iface.lpVtbl = &SimpleAudioVolume_Vtbl;
    ret->IChannelAudioVolume_iface.lpVtbl = &ChannelAudioVolume_Vtbl;

    ret->ref = 1;

    ret->client = client;
    if(client){
        ret->session = client->session;
        AudioClient_AddRef(&client->IAudioClient3_iface);
    }

    return ret;
}

static HRESULT WINAPI AudioSessionControl_QueryInterface(
        IAudioSessionControl2 *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionControl) ||
            IsEqualIID(riid, &IID_IAudioSessionControl2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionControl_AddRef(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionControl_Release(IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref){
        if(This->client){
            EnterCriticalSection(&This->client->lock);
            This->client->session_wrapper = NULL;
            LeaveCriticalSection(&This->client->lock);
            AudioClient_Release(&This->client->IAudioClient3_iface);
        }
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static HRESULT WINAPI AudioSessionControl_GetState(IAudioSessionControl2 *iface,
        AudioSessionState *state)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);
    ACImpl *client;

    TRACE("(%p)->(%p)\n", This, state);

    if(!state)
        return NULL_PTR_ERR;

    EnterCriticalSection(&g_sessions_lock);

    if(list_empty(&This->session->clients)){
        *state = AudioSessionStateExpired;
        LeaveCriticalSection(&g_sessions_lock);
        return S_OK;
    }

    LIST_FOR_EACH_ENTRY(client, &This->session->clients, ACImpl, entry){
        EnterCriticalSection(&client->lock);
        if(client->playing){
            *state = AudioSessionStateActive;
            LeaveCriticalSection(&client->lock);
            LeaveCriticalSection(&g_sessions_lock);
            return S_OK;
        }
        LeaveCriticalSection(&client->lock);
    }

    LeaveCriticalSection(&g_sessions_lock);

    *state = AudioSessionStateInactive;

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetDisplayName(
        IAudioSessionControl2 *iface, WCHAR **name)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, name);

    if(!name)
        return NULL_PTR_ERR;

    /* (no name: the program's) */
    *name = CoTaskMemAlloc(sizeof(WCHAR));
    if(!*name)
        return E_OUTOFMEMORY;
    **name = 0;

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_SetDisplayName(
        IAudioSessionControl2 *iface, const WCHAR *name, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_w(name), debugstr_guid(session));

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetIconPath(
        IAudioSessionControl2 *iface, WCHAR **path)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, path);

    if(!path)
        return NULL_PTR_ERR;

    *path = CoTaskMemAlloc(sizeof(WCHAR));
    if(!*path)
        return E_OUTOFMEMORY;
    **path = 0;

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_SetIconPath(
        IAudioSessionControl2 *iface, const WCHAR *path, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_w(path), debugstr_guid(session));

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetGroupingParam(
        IAudioSessionControl2 *iface, GUID *group)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, group);

    if(!group)
        return NULL_PTR_ERR;

    *group = GUID_NULL;

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_SetGroupingParam(
        IAudioSessionControl2 *iface, const GUID *group, const GUID *session)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%s, %s) - stub\n", This, debugstr_guid(group),
            debugstr_guid(session));

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_RegisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_UnregisterAudioSessionNotification(
        IAudioSessionControl2 *iface, IAudioSessionEvents *events)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, events);

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_GetSessionIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetSessionInstanceIdentifier(
        IAudioSessionControl2 *iface, WCHAR **id)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    FIXME("(%p)->(%p) - stub\n", This, id);

    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionControl_GetProcessId(
        IAudioSessionControl2 *iface, DWORD *pid)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%p)\n", This, pid);

    if(!pid)
        return E_POINTER;

    *pid = GetCurrentProcessId();

    return S_OK;
}

static HRESULT WINAPI AudioSessionControl_IsSystemSoundsSession(
        IAudioSessionControl2 *iface)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)\n", This);

    return S_FALSE;
}

static HRESULT WINAPI AudioSessionControl_SetDuckingPreference(
        IAudioSessionControl2 *iface, BOOL optout)
{
    AudioSessionWrapper *This = impl_from_IAudioSessionControl2(iface);

    TRACE("(%p)->(%d)\n", This, optout);

    return S_OK;
}

static const IAudioSessionControl2Vtbl AudioSessionControl2_Vtbl =
{
    AudioSessionControl_QueryInterface,
    AudioSessionControl_AddRef,
    AudioSessionControl_Release,
    AudioSessionControl_GetState,
    AudioSessionControl_GetDisplayName,
    AudioSessionControl_SetDisplayName,
    AudioSessionControl_GetIconPath,
    AudioSessionControl_SetIconPath,
    AudioSessionControl_GetGroupingParam,
    AudioSessionControl_SetGroupingParam,
    AudioSessionControl_RegisterAudioSessionNotification,
    AudioSessionControl_UnregisterAudioSessionNotification,
    AudioSessionControl_GetSessionIdentifier,
    AudioSessionControl_GetSessionInstanceIdentifier,
    AudioSessionControl_GetProcessId,
    AudioSessionControl_IsSystemSoundsSession,
    AudioSessionControl_SetDuckingPreference
};

static HRESULT WINAPI SimpleAudioVolume_QueryInterface(
        ISimpleAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_ISimpleAudioVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI SimpleAudioVolume_AddRef(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI SimpleAudioVolume_Release(ISimpleAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI SimpleAudioVolume_SetMasterVolume(
        ISimpleAudioVolume *iface, float level, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%f, %s)\n", session, level, wine_dbgstr_guid(context));

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    EnterCriticalSection(&session->lock);

    session->master_vol = level;


    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMasterVolume(
        ISimpleAudioVolume *iface, float *level)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, level);

    if(!level)
        return NULL_PTR_ERR;

    *level = session->master_vol;

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_SetMute(ISimpleAudioVolume *iface,
        BOOL mute, const GUID *context)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%u, %s)\n", session, mute, debugstr_guid(context));

    EnterCriticalSection(&session->lock);

    session->mute = mute;

    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI SimpleAudioVolume_GetMute(ISimpleAudioVolume *iface,
        BOOL *mute)
{
    AudioSessionWrapper *This = impl_from_ISimpleAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, mute);

    if(!mute)
        return NULL_PTR_ERR;

    *mute = This->session->mute;

    return S_OK;
}

static const ISimpleAudioVolumeVtbl SimpleAudioVolume_Vtbl  =
{
    SimpleAudioVolume_QueryInterface,
    SimpleAudioVolume_AddRef,
    SimpleAudioVolume_Release,
    SimpleAudioVolume_SetMasterVolume,
    SimpleAudioVolume_GetMasterVolume,
    SimpleAudioVolume_SetMute,
    SimpleAudioVolume_GetMute
};

static HRESULT WINAPI AudioStreamVolume_QueryInterface(
        IAudioStreamVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioStreamVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioStreamVolume_AddRef(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return AudioClient_AddRef(&This->IAudioClient3_iface);
}

static ULONG WINAPI AudioStreamVolume_Release(IAudioStreamVolume *iface)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    return AudioClient_Release(&This->IAudioClient3_iface);
}

static HRESULT WINAPI AudioStreamVolume_GetChannelCount(
        IAudioStreamVolume *iface, UINT32 *out)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%p)\n", This, out);

    if(!out)
        return E_POINTER;

    *out = This->fmt->nChannels;

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_SetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%d, %f)\n", This, index, level);

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(index >= This->fmt->nChannels)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    This->vols[index] = level;


    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_GetChannelVolume(
        IAudioStreamVolume *iface, UINT32 index, float *level)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);

    TRACE("(%p)->(%d, %p)\n", This, index, level);

    if(!level)
        return E_POINTER;

    if(index >= This->fmt->nChannels)
        return E_INVALIDARG;

    *level = This->vols[index];

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_SetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, const float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if(!levels)
        return E_POINTER;

    if(count != This->fmt->nChannels)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    for(i = 0; i < count; ++i)
        This->vols[i] = levels[i];


    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static HRESULT WINAPI AudioStreamVolume_GetAllVolumes(
        IAudioStreamVolume *iface, UINT32 count, float *levels)
{
    ACImpl *This = impl_from_IAudioStreamVolume(iface);
    int i;

    TRACE("(%p)->(%d, %p)\n", This, count, levels);

    if(!levels)
        return E_POINTER;

    if(count != This->fmt->nChannels)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

    for(i = 0; i < count; ++i)
        levels[i] = This->vols[i];

    LeaveCriticalSection(&This->lock);

    return S_OK;
}

static const IAudioStreamVolumeVtbl AudioStreamVolume_Vtbl =
{
    AudioStreamVolume_QueryInterface,
    AudioStreamVolume_AddRef,
    AudioStreamVolume_Release,
    AudioStreamVolume_GetChannelCount,
    AudioStreamVolume_SetChannelVolume,
    AudioStreamVolume_GetChannelVolume,
    AudioStreamVolume_SetAllVolumes,
    AudioStreamVolume_GetAllVolumes
};

static HRESULT WINAPI ChannelAudioVolume_QueryInterface(
        IChannelAudioVolume *iface, REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IChannelAudioVolume))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI ChannelAudioVolume_AddRef(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_AddRef(&This->IAudioSessionControl2_iface);
}

static ULONG WINAPI ChannelAudioVolume_Release(IChannelAudioVolume *iface)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    return AudioSessionControl_Release(&This->IAudioSessionControl2_iface);
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelCount(
        IChannelAudioVolume *iface, UINT32 *out)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%p)\n", session, out);

    if(!out)
        return NULL_PTR_ERR;

    *out = session->channel_count;

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float level,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %f, %s)\n", session, index, level,
            wine_dbgstr_guid(context));

    if(level < 0.f || level > 1.f)
        return E_INVALIDARG;

    if(index >= session->channel_count)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    EnterCriticalSection(&session->lock);

    session->channel_vols[index] = level;


    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetChannelVolume(
        IChannelAudioVolume *iface, UINT32 index, float *level)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;

    TRACE("(%p)->(%d, %p)\n", session, index, level);

    if(!level)
        return NULL_PTR_ERR;

    if(index >= session->channel_count)
        return E_INVALIDARG;

    *level = session->channel_vols[index];

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_SetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, const float *levels,
        const GUID *context)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p, %s)\n", session, count, levels,
            wine_dbgstr_guid(context));

    if(!levels)
        return NULL_PTR_ERR;

    if(count != session->channel_count)
        return E_INVALIDARG;

    if(context)
        FIXME("Notifications not supported yet\n");

    EnterCriticalSection(&session->lock);

    for(i = 0; i < count; ++i)
        session->channel_vols[i] = levels[i];


    LeaveCriticalSection(&session->lock);

    return S_OK;
}

static HRESULT WINAPI ChannelAudioVolume_GetAllVolumes(
        IChannelAudioVolume *iface, UINT32 count, float *levels)
{
    AudioSessionWrapper *This = impl_from_IChannelAudioVolume(iface);
    AudioSession *session = This->session;
    int i;

    TRACE("(%p)->(%d, %p)\n", session, count, levels);

    if(!levels)
        return NULL_PTR_ERR;

    if(count != session->channel_count)
        return E_INVALIDARG;

    for(i = 0; i < count; ++i)
        levels[i] = session->channel_vols[i];

    return S_OK;
}

static const IChannelAudioVolumeVtbl ChannelAudioVolume_Vtbl =
{
    ChannelAudioVolume_QueryInterface,
    ChannelAudioVolume_AddRef,
    ChannelAudioVolume_Release,
    ChannelAudioVolume_GetChannelCount,
    ChannelAudioVolume_SetChannelVolume,
    ChannelAudioVolume_GetChannelVolume,
    ChannelAudioVolume_SetAllVolumes,
    ChannelAudioVolume_GetAllVolumes
};

static HRESULT WINAPI AudioSessionManager_QueryInterface(IAudioSessionManager2 *iface,
        REFIID riid, void **ppv)
{
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
            IsEqualIID(riid, &IID_IAudioSessionManager) ||
            IsEqualIID(riid, &IID_IAudioSessionManager2))
        *ppv = iface;
    if(*ppv){
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI AudioSessionManager_AddRef(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    return ref;
}

static ULONG WINAPI AudioSessionManager_Release(IAudioSessionManager2 *iface)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    ULONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) Refcount now %u\n", This, ref);
    if(!ref)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI AudioSessionManager_GetAudioSessionControl(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        IAudioSessionControl **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = (IAudioSessionControl*)&wrapper->IAudioSessionControl2_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSimpleAudioVolume(
        IAudioSessionManager2 *iface, const GUID *session_guid, DWORD flags,
        ISimpleAudioVolume **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    AudioSession *session;
    AudioSessionWrapper *wrapper;
    HRESULT hr;

    TRACE("(%p)->(%s, %x, %p)\n", This, debugstr_guid(session_guid),
            flags, out);

    hr = get_audio_session(session_guid, This->device, 0, &session);
    if(FAILED(hr))
        return hr;

    wrapper = AudioSessionWrapper_Create(NULL);
    if(!wrapper)
        return E_OUTOFMEMORY;

    wrapper->session = session;

    *out = &wrapper->ISimpleAudioVolume_iface;

    return S_OK;
}

static HRESULT WINAPI AudioSessionManager_GetSessionEnumerator(
        IAudioSessionManager2 *iface, IAudioSessionEnumerator **out)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, out);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterSessionNotification(
        IAudioSessionManager2 *iface, IAudioSessionNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_RegisterDuckNotification(
        IAudioSessionManager2 *iface, const WCHAR *session_id,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static HRESULT WINAPI AudioSessionManager_UnregisterDuckNotification(
        IAudioSessionManager2 *iface,
        IAudioVolumeDuckNotification *notification)
{
    SessionMgr *This = impl_from_IAudioSessionManager2(iface);
    FIXME("(%p)->(%p) - stub\n", This, notification);
    return E_NOTIMPL;
}

static const IAudioSessionManager2Vtbl AudioSessionManager2_Vtbl =
{
    AudioSessionManager_QueryInterface,
    AudioSessionManager_AddRef,
    AudioSessionManager_Release,
    AudioSessionManager_GetAudioSessionControl,
    AudioSessionManager_GetSimpleAudioVolume,
    AudioSessionManager_GetSessionEnumerator,
    AudioSessionManager_RegisterSessionNotification,
    AudioSessionManager_UnregisterSessionNotification,
    AudioSessionManager_RegisterDuckNotification,
    AudioSessionManager_UnregisterDuckNotification
};

HRESULT WINAPI WINMM_GetAudioSessionManager(IMMDevice *device,
        IAudioSessionManager2 **out)
{
    SessionMgr *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SessionMgr));
    if(!This)
        return E_OUTOFMEMORY;

    This->IAudioSessionManager2_iface.lpVtbl = &AudioSessionManager2_Vtbl;
    This->device = device;
    This->ref = 1;

    *out = &This->IAudioSessionManager2_iface;

    return S_OK;
}
