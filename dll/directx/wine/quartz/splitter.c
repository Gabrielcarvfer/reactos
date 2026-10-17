/*
 * AVI Splitter, MPEG-I Stream Splitter and Wave Parser
 *
 * Wine's quartz hands these filters to winegstreamer, which ReactOS does not
 * have. These are small native pull-mode parsers built on strmbase instead:
 * the input pin reads the file through the upstream IAsyncReader, builds a
 * packet list per stream when it is connected, and each output pin has a
 * streaming thread that delivers its packets.
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

#include "quartz_private.h"
#include "mmreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

#define SECOND 10000000

enum splitter_kind
{
    SPLITTER_AVI,
    SPLITTER_MPEG,
    SPLITTER_WAVE,
};

struct packet
{
    LONGLONG offset;
    DWORD size;
    BOOL sync;
    REFERENCE_TIME start, stop;
};

struct split_stream
{
    struct strmbase_source source;
    SourceSeeking seek;
    struct splitter *filter;

    AM_MEDIA_TYPE mt;
    struct packet *packets;
    size_t count, capacity;
    DWORD max_size;
    BOOL video;

    HANDLE thread;
};

struct splitter
{
    struct strmbase_filter filter;
    struct strmbase_sink sink;
    enum splitter_kind kind;

    IAsyncReader *reader;
    LONGLONG file_size;
    REFERENCE_TIME duration;

    struct split_stream **streams;
    unsigned int stream_count;

    BOOL streaming;
    volatile LONG stopping;
    REFERENCE_TIME seek_start, seek_stop;
};

static inline struct splitter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct splitter, filter);
}

static inline struct split_stream *impl_from_source_pin(struct strmbase_pin *iface)
{
    return CONTAINING_RECORD(iface, struct split_stream, source.pin);
}

static inline struct split_stream *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct split_stream, seek.IMediaSeeking_iface);
}

static GUID fourcc_guid(DWORD fourcc)
{
    GUID guid = {0x00000000, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    guid.Data1 = fourcc;
    return guid;
}

static HRESULT read_data(struct splitter *filter, LONGLONG offset, void *buffer, LONG size)
{
    if (offset < 0 || offset + size > filter->file_size)
        return S_FALSE;
    return IAsyncReader_SyncRead(filter->reader, offset, size, buffer);
}

static struct split_stream *add_stream(struct splitter *filter, const WCHAR *name);

static BOOL add_packet(struct split_stream *stream, LONGLONG offset, DWORD size,
        REFERENCE_TIME start, REFERENCE_TIME stop, BOOL sync)
{
    struct packet *packet;

    if (!array_reserve((void **)&stream->packets, &stream->capacity, stream->count + 1, sizeof(*stream->packets)))
        return FALSE;
    packet = &stream->packets[stream->count++];
    packet->offset = offset;
    packet->size = size;
    packet->start = start;
    packet->stop = stop;
    packet->sync = sync;
    stream->max_size = max(stream->max_size, size);
    return TRUE;
}

/* Builds an audio media type from a WAVEFORMATEX (or PCMWAVEFORMAT) read from the file. */
static HRESULT init_audio_mt(AM_MEDIA_TYPE *mt, const BYTE *fmt, DWORD size)
{
    WAVEFORMATEX *wfx;
    DWORD format_size = max(size, sizeof(WAVEFORMATEX));

    if (size < sizeof(PCMWAVEFORMAT))
        return VFW_E_INVALID_FILE_FORMAT;
    if (!(wfx = CoTaskMemAlloc(format_size)))
        return E_OUTOFMEMORY;
    memset(wfx, 0, format_size);
    memcpy(wfx, fmt, size);
    if (size < sizeof(WAVEFORMATEX))
        wfx->cbSize = 0;
    else if (wfx->cbSize > size - sizeof(WAVEFORMATEX))
        wfx->cbSize = size - sizeof(WAVEFORMATEX);
    if (!wfx->nAvgBytesPerSec || !wfx->nBlockAlign)
    {
        CoTaskMemFree(wfx);
        return VFW_E_INVALID_FILE_FORMAT;
    }

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Audio;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->cbSize >= 22)
        memcpy(&mt->subtype, (BYTE *)wfx + 24, sizeof(GUID));
    else
        mt->subtype = fourcc_guid(wfx->wFormatTag);
    mt->formattype = FORMAT_WaveFormatEx;
    mt->bFixedSizeSamples = (wfx->wFormatTag == WAVE_FORMAT_PCM);
    mt->lSampleSize = wfx->nBlockAlign;
    mt->cbFormat = format_size;
    mt->pbFormat = (BYTE *)wfx;
    return S_OK;
}

/* Wave Parser ************************************************************/

static HRESULT wave_parse(struct splitter *filter)
{
    LONGLONG pos = 12, data_offset = -1, data_size = 0;
    BYTE fmt[256];
    DWORD fmt_size = 0, header[3], chunk[2];
    struct split_stream *stream;
    const WAVEFORMATEX *wfx;
    LONGLONG done, block;
    HRESULT hr;

    if (read_data(filter, 0, header, sizeof(header)) != S_OK
            || header[0] != mmioFOURCC('R','I','F','F') || header[2] != mmioFOURCC('W','A','V','E'))
        return VFW_E_INVALID_FILE_FORMAT;

    while (pos + 8 <= filter->file_size)
    {
        if (read_data(filter, pos, chunk, sizeof(chunk)) != S_OK)
            break;
        if (chunk[0] == mmioFOURCC('f','m','t',' '))
        {
            fmt_size = min(chunk[1], sizeof(fmt));
            if (read_data(filter, pos + 8, fmt, fmt_size) != S_OK)
                return VFW_E_INVALID_FILE_FORMAT;
        }
        else if (chunk[0] == mmioFOURCC('d','a','t','a'))
        {
            data_offset = pos + 8;
            data_size = min((LONGLONG)chunk[1], filter->file_size - data_offset);
            if (fmt_size)
                break;
        }
        pos += 8 + chunk[1] + (chunk[1] & 1);
    }

    if (!fmt_size || data_offset < 0)
        return VFW_E_INVALID_FILE_FORMAT;

    if (!(stream = add_stream(filter, L"output")))
        return E_OUTOFMEMORY;
    if (FAILED(hr = init_audio_mt(&stream->mt, fmt, fmt_size)))
        return hr;
    wfx = (const WAVEFORMATEX *)stream->mt.pbFormat;

    /* A quarter of a second per sample. */
    block = max(wfx->nAvgBytesPerSec / 4 / wfx->nBlockAlign, 1) * wfx->nBlockAlign;
    for (done = 0; done < data_size; done += block)
    {
        DWORD size = (DWORD)min(block, data_size - done);
        if (!add_packet(stream, data_offset + done, size, done * SECOND / wfx->nAvgBytesPerSec,
                (done + size) * SECOND / wfx->nAvgBytesPerSec, TRUE))
            return E_OUTOFMEMORY;
    }
    filter->duration = data_size * SECOND / wfx->nAvgBytesPerSec;
    return S_OK;
}

/* MPEG audio *************************************************************/

struct mpeg_header
{
    unsigned int version; /* 1, 2 or 25 (MPEG-2.5) */
    unsigned int layer;
    unsigned int bitrate; /* bits per second */
    unsigned int rate;
    unsigned int channels;
    unsigned int mode, mode_ext, emphasis;
    BOOL crc;
    unsigned int length; /* bytes */
    unsigned int samples;
};

static BOOL parse_mpeg_header(const BYTE *p, struct mpeg_header *h)
{
    static const unsigned short bitrates[2][3][15] =
    {
        {
            {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
            {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
            {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
        },
        {
            {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
            {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
            {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        },
    };
    static const unsigned int rates[3] = {44100, 48000, 32000};
    unsigned int version_bits, layer_bits, bitrate_index, rate_index, padding;

    if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
        return FALSE;
    version_bits = (p[1] >> 3) & 3;
    layer_bits = (p[1] >> 1) & 3;
    bitrate_index = p[2] >> 4;
    rate_index = (p[2] >> 2) & 3;
    if (version_bits == 1 || !layer_bits || !bitrate_index || bitrate_index == 15 || rate_index == 3)
        return FALSE;

    h->version = version_bits == 3 ? 1 : version_bits == 2 ? 2 : 25;
    h->layer = 4 - layer_bits;
    h->bitrate = bitrates[h->version != 1][h->layer - 1][bitrate_index] * 1000;
    h->rate = rates[rate_index] >> (h->version == 1 ? 0 : h->version == 2 ? 1 : 2);
    h->crc = !(p[1] & 1);
    padding = (p[2] >> 1) & 1;
    h->mode = p[3] >> 6;
    h->mode_ext = (p[3] >> 4) & 3;
    h->emphasis = p[3] & 3;
    h->channels = h->mode == 3 ? 1 : 2;

    if (h->layer == 1)
    {
        h->length = (12 * h->bitrate / h->rate + padding) * 4;
        h->samples = 384;
    }
    else if (h->layer == 2 || h->version == 1)
    {
        h->length = 144 * h->bitrate / h->rate + padding;
        h->samples = 1152;
    }
    else
    {
        h->length = 72 * h->bitrate / h->rate + padding;
        h->samples = 576;
    }
    return h->length >= 4;
}

static BOOL same_mpeg_stream(const struct mpeg_header *a, const struct mpeg_header *b)
{
    return a->version == b->version && a->layer == b->layer && a->rate == b->rate;
}

/* Buffered forward reads for scanning a file. */
struct scan_buffer
{
    struct splitter *filter;
    LONGLONG start;
    LONG size;
    BYTE data[65536];
};

static const BYTE *scan_get(struct scan_buffer *buf, LONGLONG pos, LONG size)
{
    if (pos < 0 || pos + size > buf->filter->file_size)
        return NULL;
    if (pos < buf->start || pos + size > buf->start + buf->size)
    {
        buf->start = pos;
        buf->size = (LONG)min((LONGLONG)sizeof(buf->data), buf->filter->file_size - pos);
        if (FAILED(IAsyncReader_SyncRead(buf->filter->reader, pos, buf->size, buf->data)))
        {
            buf->size = 0;
            return NULL;
        }
    }
    return buf->data + (pos - buf->start);
}

static HRESULT mpeg_parse(struct splitter *filter)
{
    struct mpeg_header first, h, next;
    struct split_stream *stream;
    struct scan_buffer *buf;
    LONGLONG pos = 0, end = filter->file_size, packet_offset, total_bytes = 0;
    LONGLONG samples = 0, packet_samples = 0, resync_limit;
    unsigned int frames = 0, frames_per_packet;
    DWORD packet_size = 0;
    const BYTE *p;
    HRESULT hr = S_OK;

    if (!(buf = malloc(sizeof(*buf))))
        return E_OUTOFMEMORY;
    buf->filter = filter;
    buf->start = 0;
    buf->size = 0;

    /* ID3v2 tag at the start, ID3v1 tag at the end. */
    if ((p = scan_get(buf, 0, 10)) && !memcmp(p, "ID3", 3))
    {
        pos = 10 + (((p[6] & 0x7f) << 21) | ((p[7] & 0x7f) << 14) | ((p[8] & 0x7f) << 7) | (p[9] & 0x7f));
        if (p[5] & 0x10)
            pos += 10;
    }
    if (end >= 128 && (p = scan_get(buf, end - 128, 3)) && !memcmp(p, "TAG", 3))
        end -= 128;

    /* Find a frame that is followed by another frame of the same stream. */
    resync_limit = min(end, pos + 1024 * 1024);
    for (;; ++pos)
    {
        if (pos + 4 > resync_limit)
        {
            free(buf);
            return VFW_E_INVALID_FILE_FORMAT;
        }
        if (!(p = scan_get(buf, pos, 4)) || !parse_mpeg_header(p, &first))
            continue;
        if (pos + first.length + 4 > end)
            break;
        if ((p = scan_get(buf, pos + first.length, 4)) && parse_mpeg_header(p, &next)
                && same_mpeg_stream(&first, &next))
            break;
    }

    if (!(stream = add_stream(filter, L"Audio")))
    {
        free(buf);
        return E_OUTOFMEMORY;
    }

    /* About a fifth of a second per sample. */
    frames_per_packet = max(first.rate / 5 / first.samples, 1);
    packet_offset = pos;
    while (pos + 4 <= end)
    {
        if (!(p = scan_get(buf, pos, 4)) || !parse_mpeg_header(p, &h) || !same_mpeg_stream(&first, &h))
        {
            /* Lost sync: close the current packet and look for the next frame. */
            if (packet_size && !add_packet(stream, packet_offset, packet_size,
                    samples * SECOND / first.rate, (samples + packet_samples) * SECOND / first.rate, TRUE))
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            samples += packet_samples;
            packet_size = 0;
            packet_samples = 0;
            frames = 0;
            ++pos;
            packet_offset = pos;
            continue;
        }
        if (pos + h.length > end)
            break;

        packet_size += h.length;
        packet_samples += h.samples;
        total_bytes += h.length;
        pos += h.length;
        if (++frames == frames_per_packet)
        {
            if (!add_packet(stream, packet_offset, packet_size,
                    samples * SECOND / first.rate, (samples + packet_samples) * SECOND / first.rate, TRUE))
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            samples += packet_samples;
            packet_size = 0;
            packet_samples = 0;
            frames = 0;
            packet_offset = pos;
        }
    }
    if (SUCCEEDED(hr) && packet_size && !add_packet(stream, packet_offset, packet_size,
            samples * SECOND / first.rate, (samples + packet_samples) * SECOND / first.rate, TRUE))
        hr = E_OUTOFMEMORY;
    samples += packet_samples;
    free(buf);
    if (FAILED(hr))
        return hr;
    if (!stream->count)
        return VFW_E_INVALID_FILE_FORMAT;

    filter->duration = samples * SECOND / first.rate;

    memset(&stream->mt, 0, sizeof(stream->mt));
    stream->mt.majortype = MEDIATYPE_Audio;
    stream->mt.formattype = FORMAT_WaveFormatEx;
    stream->mt.bFixedSizeSamples = FALSE;
    stream->mt.lSampleSize = 0;
    if (first.layer == 3)
    {
        MPEGLAYER3WAVEFORMAT *format;

        if (!(format = CoTaskMemAlloc(sizeof(*format))))
            return E_OUTOFMEMORY;
        memset(format, 0, sizeof(*format));
        format->wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        format->wfx.nChannels = first.channels;
        format->wfx.nSamplesPerSec = first.rate;
        format->wfx.nAvgBytesPerSec = (DWORD)(total_bytes * first.rate / max(samples, 1));
        format->wfx.nBlockAlign = 1;
        format->wfx.wBitsPerSample = 0;
        format->wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
        format->wID = MPEGLAYER3_ID_MPEG;
        format->fdwFlags = MPEGLAYER3_FLAG_PADDING_ISO;
        format->nBlockSize = first.length;
        format->nFramesPerBlock = 1;
        format->nCodecDelay = 1393;
        stream->mt.subtype = fourcc_guid(WAVE_FORMAT_MPEGLAYER3);
        stream->mt.cbFormat = sizeof(*format);
        stream->mt.pbFormat = (BYTE *)format;
    }
    else
    {
        MPEG1WAVEFORMAT *format;

        if (!(format = CoTaskMemAlloc(sizeof(*format))))
            return E_OUTOFMEMORY;
        memset(format, 0, sizeof(*format));
        format->wfx.wFormatTag = WAVE_FORMAT_MPEG;
        format->wfx.nChannels = first.channels;
        format->wfx.nSamplesPerSec = first.rate;
        format->wfx.nAvgBytesPerSec = first.bitrate / 8;
        format->wfx.nBlockAlign = first.length;
        format->wfx.cbSize = sizeof(*format) - sizeof(WAVEFORMATEX);
        format->fwHeadLayer = first.layer == 1 ? ACM_MPEG_LAYER1 : ACM_MPEG_LAYER2;
        format->dwHeadBitrate = first.bitrate;
        format->fwHeadMode = first.mode == 0 ? ACM_MPEG_STEREO : first.mode == 1 ? ACM_MPEG_JOINTSTEREO
                : first.mode == 2 ? ACM_MPEG_DUALCHANNEL : ACM_MPEG_SINGLECHANNEL;
        format->fwHeadModeExt = 1 << first.mode_ext;
        format->wHeadEmphasis = first.emphasis + 1;
        format->fwHeadFlags = (first.crc ? ACM_MPEG_PROTECTIONBIT : 0) | (first.version == 1 ? ACM_MPEG_ID_MPEG1 : 0);
        stream->mt.subtype = MEDIASUBTYPE_MPEG1AudioPayload;
        stream->mt.cbFormat = sizeof(*format);
        stream->mt.pbFormat = (BYTE *)format;
    }
    return S_OK;
}

/* AVI ********************************************************************/

#include <pshpack1.h>
struct avi_stream_header
{
    DWORD type;
    DWORD handler;
    DWORD flags;
    WORD priority;
    WORD language;
    DWORD initial_frames;
    DWORD scale;
    DWORD rate;
    DWORD start;
    DWORD length;
    DWORD buffer_size;
    DWORD quality;
    DWORD sample_size;
    short frame[4];
};
#include <poppack.h>

struct avi_stream
{
    struct split_stream *stream; /* NULL for streams we do not expose */
    struct avi_stream_header header;
    LONGLONG chunks, bytes;
};

#define AVIIF_LIST      0x00000001
#define AVIIF_KEYFRAME  0x00000010

static void avi_chunk_time(const struct avi_stream *s, DWORD size, REFERENCE_TIME *start, REFERENCE_TIME *stop)
{
    const struct avi_stream_header *h = &s->header;
    LONGLONG begin, end;

    if (s->stream && !s->stream->video && h->sample_size)
    {
        begin = s->bytes / h->sample_size;
        end = (s->bytes + size) / h->sample_size;
    }
    else
    {
        begin = s->chunks;
        end = s->chunks + 1;
    }
    *start = (h->start + begin) * h->scale * (LONGLONG)SECOND / h->rate;
    *stop = (h->start + end) * h->scale * (LONGLONG)SECOND / h->rate;
}

static HRESULT avi_add_chunk(struct avi_stream *streams, unsigned int count, DWORD id,
        LONGLONG offset, DWORD size, BOOL sync)
{
    REFERENCE_TIME start, stop;
    unsigned int index;
    char c0 = id & 0xff, c1 = (id >> 8) & 0xff;
    struct avi_stream *s;

    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9')
        return S_OK;
    index = (c0 - '0') * 10 + (c1 - '0');
    if (index >= count)
        return S_OK;
    s = &streams[index];
    if (!s->stream)
        return S_OK;
    /* Palette changes are not supported. */
    if (((id >> 16) & 0xffff) == ('p' | ('c' << 8)))
        return S_OK;

    avi_chunk_time(s, size, &start, &stop);
    /* Empty video chunks are dropped frames; they only take time. */
    if (size && !add_packet(s->stream, offset, size, start, stop, sync))
        return E_OUTOFMEMORY;
    s->chunks++;
    s->bytes += size;
    return S_OK;
}

static HRESULT avi_scan_movi(struct splitter *filter, struct avi_stream *streams, unsigned int count,
        LONGLONG pos, LONGLONG end)
{
    DWORD chunk[3];
    HRESULT hr;

    while (pos + 8 <= end)
    {
        if (read_data(filter, pos, chunk, 12) != S_OK && read_data(filter, pos, chunk, 8) != S_OK)
            break;
        if (chunk[0] == mmioFOURCC('L','I','S','T'))
        {
            if (chunk[2] == mmioFOURCC('r','e','c',' '))
                pos += 12;
            else
                pos += 8 + chunk[1] + (chunk[1] & 1);
            continue;
        }
        if (chunk[1] > end - pos - 8)
            chunk[1] = (DWORD)(end - pos - 8);
        if (FAILED(hr = avi_add_chunk(streams, count, chunk[0], pos + 8, chunk[1], TRUE)))
            return hr;
        pos += 8 + chunk[1] + (chunk[1] & 1);
    }
    return S_OK;
}

static HRESULT avi_read_index(struct splitter *filter, struct avi_stream *streams, unsigned int count,
        LONGLONG movi, LONGLONG index_offset, DWORD index_size)
{
    DWORD *index, entries, i, chunk[2];
    LONGLONG base;
    HRESULT hr = S_OK;

    entries = index_size / 16;
    if (!entries || !(index = malloc(entries * 16)))
        return E_FAIL;
    if (read_data(filter, index_offset, index, entries * 16) != S_OK)
    {
        free(index);
        return E_FAIL;
    }

    /* Offsets are normally relative to the "movi" FOURCC, but some writers
     * use absolute offsets. Check which one points at the first chunk. */
    for (i = 0; i < entries && (index[i * 4 + 1] & AVIIF_LIST); ++i)
        ;
    if (i == entries)
    {
        free(index);
        return E_FAIL;
    }
    if (read_data(filter, movi + index[i * 4 + 2], chunk, 8) == S_OK && chunk[0] == index[i * 4])
        base = movi;
    else if (read_data(filter, index[i * 4 + 2], chunk, 8) == S_OK && chunk[0] == index[i * 4])
        base = 0;
    else
    {
        free(index);
        return E_FAIL;
    }

    for (i = 0; i < entries && SUCCEEDED(hr); ++i)
    {
        if (index[i * 4 + 1] & AVIIF_LIST)
            continue;
        hr = avi_add_chunk(streams, count, index[i * 4], base + index[i * 4 + 2] + 8,
                index[i * 4 + 3], !!(index[i * 4 + 1] & AVIIF_KEYFRAME));
    }
    free(index);
    return hr;
}

static HRESULT avi_init_video_mt(AM_MEDIA_TYPE *mt, const struct avi_stream_header *header,
        const BYTE *fmt, DWORD size)
{
    const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)fmt;
    VIDEOINFOHEADER *vih;
    DWORD format_size;

    if (size < sizeof(BITMAPINFOHEADER))
        return VFW_E_INVALID_FILE_FORMAT;
    format_size = offsetof(VIDEOINFOHEADER, bmiHeader) + size;
    if (!(vih = CoTaskMemAlloc(format_size)))
        return E_OUTOFMEMORY;
    memset(vih, 0, format_size);
    memcpy(&vih->bmiHeader, fmt, size);
    vih->bmiHeader.biSize = max(bih->biSize, sizeof(BITMAPINFOHEADER));
    if (header->rate)
        vih->AvgTimePerFrame = (LONGLONG)header->scale * SECOND / header->rate;

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    if (bih->biCompression == BI_RGB || bih->biCompression == BI_BITFIELDS)
    {
        switch (bih->biBitCount)
        {
            case 1: mt->subtype = MEDIASUBTYPE_RGB1; break;
            case 4: mt->subtype = MEDIASUBTYPE_RGB4; break;
            case 8: mt->subtype = MEDIASUBTYPE_RGB8; break;
            case 16: mt->subtype = bih->biCompression == BI_BITFIELDS ? MEDIASUBTYPE_RGB565 : MEDIASUBTYPE_RGB555; break;
            case 24: mt->subtype = MEDIASUBTYPE_RGB24; break;
            case 32: mt->subtype = MEDIASUBTYPE_RGB32; break;
            default:
                CoTaskMemFree(vih);
                return VFW_E_INVALID_FILE_FORMAT;
        }
        mt->bFixedSizeSamples = TRUE;
        mt->lSampleSize = bih->biSizeImage;
    }
    else
    {
        mt->subtype = fourcc_guid(bih->biCompression);
        mt->bTemporalCompression = TRUE;
    }
    mt->formattype = FORMAT_VideoInfo;
    mt->cbFormat = format_size;
    mt->pbFormat = (BYTE *)vih;
    return S_OK;
}

static HRESULT avi_parse(struct splitter *filter)
{
    struct avi_stream streams[100];
    unsigned int count = 0, i;
    LONGLONG pos = 12, riff_end, movi = -1, movi_end = 0, index_offset = -1;
    DWORD index_size = 0, header[3], chunk[3];
    BOOL extended = FALSE;
    BYTE *hdrl = NULL;
    DWORD hdrl_size = 0, p;
    HRESULT hr;

    if (read_data(filter, 0, header, sizeof(header)) != S_OK
            || header[0] != mmioFOURCC('R','I','F','F') || header[2] != mmioFOURCC('A','V','I',' '))
        return VFW_E_INVALID_FILE_FORMAT;
    riff_end = min(filter->file_size, 8 + (LONGLONG)header[1]);

    memset(streams, 0, sizeof(streams));

    while (pos + 8 <= riff_end)
    {
        if (read_data(filter, pos, chunk, 12) != S_OK && read_data(filter, pos, chunk, 8) != S_OK)
            break;
        if (chunk[0] == mmioFOURCC('L','I','S','T') && chunk[2] == mmioFOURCC('h','d','r','l') && !hdrl)
        {
            hdrl_size = min(chunk[1], 1024 * 1024);
            if (hdrl_size < 4 || !(hdrl = malloc(hdrl_size)))
                return VFW_E_INVALID_FILE_FORMAT;
            if (read_data(filter, pos + 8, hdrl, hdrl_size) != S_OK)
            {
                free(hdrl);
                return VFW_E_INVALID_FILE_FORMAT;
            }
        }
        else if (chunk[0] == mmioFOURCC('L','I','S','T') && chunk[2] == mmioFOURCC('m','o','v','i') && movi < 0)
        {
            movi = pos + 8;
            movi_end = min(riff_end, pos + 8 + (LONGLONG)chunk[1]);
        }
        else if (chunk[0] == mmioFOURCC('i','d','x','1'))
        {
            index_offset = pos + 8;
            index_size = chunk[1];
        }
        pos += 8 + chunk[1] + (chunk[1] & 1);
    }
    /* OpenDML files continue in further RIFF 'AVIX' chunks. */
    if (pos + 12 <= filter->file_size && read_data(filter, pos, chunk, 12) == S_OK
            && chunk[0] == mmioFOURCC('R','I','F','F') && chunk[2] == mmioFOURCC('A','V','I','X'))
        extended = TRUE;

    if (!hdrl || movi < 0)
    {
        free(hdrl);
        return VFW_E_INVALID_FILE_FORMAT;
    }

    /* Walk "hdrl": skip the list type, then avih and one "strl" list per stream. */
    for (p = 4; p + 8 <= hdrl_size; )
    {
        DWORD id = *(DWORD *)(hdrl + p), size = *(DWORD *)(hdrl + p + 4);

        if (size > hdrl_size - p - 8)
            size = hdrl_size - p - 8;
        if (id == mmioFOURCC('L','I','S','T') && size >= 4
                && *(DWORD *)(hdrl + p + 8) == mmioFOURCC('s','t','r','l') && count < ARRAY_SIZE(streams))
        {
            struct avi_stream *s = &streams[count++];
            const BYTE *fmt = NULL;
            DWORD q, fmt_size = 0;
            BOOL have_header = FALSE;
            WCHAR name[16];

            for (q = p + 12; q + 8 <= p + 8 + size; )
            {
                DWORD sid = *(DWORD *)(hdrl + q), ssize = *(DWORD *)(hdrl + q + 4);

                if (ssize > p + 8 + size - q - 8)
                    ssize = p + 8 + size - q - 8;
                if (sid == mmioFOURCC('s','t','r','h') && ssize >= offsetof(struct avi_stream_header, frame))
                {
                    memcpy(&s->header, hdrl + q + 8, min(ssize, sizeof(s->header)));
                    have_header = TRUE;
                }
                else if (sid == mmioFOURCC('s','t','r','f'))
                {
                    fmt = hdrl + q + 8;
                    fmt_size = ssize;
                }
                q += 8 + ssize + (ssize & 1);
            }

            if (!have_header || !fmt || !s->header.scale || !s->header.rate)
                continue;
            if (s->header.type != mmioFOURCC('v','i','d','s') && s->header.type != mmioFOURCC('a','u','d','s'))
                continue;

            wsprintfW(name, L"Stream %02u", count - 1);
            if (!(s->stream = add_stream(filter, name)))
            {
                free(hdrl);
                return E_OUTOFMEMORY;
            }
            if (s->header.type == mmioFOURCC('v','i','d','s'))
            {
                s->stream->video = TRUE;
                hr = avi_init_video_mt(&s->stream->mt, &s->header, fmt, fmt_size);
            }
            else
            {
                hr = init_audio_mt(&s->stream->mt, fmt, fmt_size);
            }
            if (FAILED(hr))
            {
                WARN("Failed to get the format of stream %u, hr %#lx.\n", count - 1, hr);
                /* Keep the pin list consistent: drop the pin again. */
                filter->stream_count--;
                strmbase_source_cleanup(&s->stream->source);
                strmbase_seeking_cleanup(&s->stream->seek);
                free(s->stream);
                s->stream = NULL;
            }
        }
        p += 8 + size + (size & 1);
    }
    free(hdrl);

    if (!filter->stream_count)
        return VFW_E_INVALID_FILE_FORMAT;

    hr = E_FAIL;
    if (index_offset >= 0 && !extended)
        hr = avi_read_index(filter, streams, count, movi, index_offset, index_size);
    if (FAILED(hr) && hr != E_OUTOFMEMORY)
    {
        for (i = 0; i < filter->stream_count; ++i)
            filter->streams[i]->count = 0;
        for (i = 0; i < count; ++i)
            streams[i].chunks = streams[i].bytes = 0;

        hr = avi_scan_movi(filter, streams, count, movi + 4, movi_end);
        /* Scan the movi lists of the AVIX extensions. */
        while (SUCCEEDED(hr) && extended && pos + 12 <= filter->file_size)
        {
            LONGLONG riff_pos = pos + 12, end;

            if (read_data(filter, pos, chunk, 12) != S_OK || chunk[0] != mmioFOURCC('R','I','F','F'))
                break;
            end = min(filter->file_size, pos + 8 + (LONGLONG)chunk[1]);
            while (SUCCEEDED(hr) && riff_pos + 12 <= end)
            {
                DWORD sub[3];
                if (read_data(filter, riff_pos, sub, 12) != S_OK)
                    break;
                if (sub[0] == mmioFOURCC('L','I','S','T') && sub[2] == mmioFOURCC('m','o','v','i'))
                    hr = avi_scan_movi(filter, streams, count, riff_pos + 12, min(end, riff_pos + 8 + (LONGLONG)sub[1]));
                riff_pos += 8 + sub[1] + (sub[1] & 1);
            }
            pos = end + (end & 1);
        }
    }
    if (FAILED(hr))
        return hr;

    filter->duration = 0;
    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream->count)
            filter->duration = max(filter->duration, stream->packets[stream->count - 1].stop);
    }
    return S_OK;
}

/* Streaming **************************************************************/

static DWORD WINAPI stream_thread(void *arg)
{
    struct split_stream *stream = arg;
    struct splitter *filter = stream->filter;
    REFERENCE_TIME start = filter->seek_start, t0, t1;
    IPin *peer = stream->source.pin.peer;
    BOOL discontinuity = TRUE;
    size_t i = 0, j;
    HRESULT hr = S_OK;

    TRACE("Starting streaming thread for pin %s at %s.\n", debugstr_w(stream->source.pin.name), debugstr_time(start));

    IPin_NewSegment(peer, start, filter->seek_stop, 1.0);

    /* First packet that is still playing at the start time; for video, go
     * back to the key frame it depends on. */
    while (i < stream->count && stream->packets[i].stop <= start)
        ++i;
    if (stream->video && i < stream->count)
    {
        for (j = i; j > 0 && !stream->packets[j].sync; --j)
            ;
        i = j;
    }

    for (; i < stream->count; ++i)
    {
        const struct packet *packet = &stream->packets[i];
        IMediaSample *sample;
        BYTE *data;

        if (filter->stopping)
            break;
        if (packet->start >= filter->seek_stop)
        {
            i = stream->count;
            break;
        }

        if (FAILED(hr = IMemAllocator_GetBuffer(stream->source.pAllocator, &sample, NULL, NULL, 0)))
        {
            TRACE("Failed to get a sample, hr %#lx.\n", hr);
            break;
        }
        if (filter->stopping)
        {
            IMediaSample_Release(sample);
            break;
        }

        IMediaSample_GetPointer(sample, &data);
        if (packet->size > (DWORD)IMediaSample_GetSize(sample)
                || FAILED(IAsyncReader_SyncRead(filter->reader, packet->offset, packet->size, data)))
        {
            WARN("Failed to read %lu bytes at %s.\n", packet->size, wine_dbgstr_longlong(packet->offset));
            IMediaSample_Release(sample);
            continue;
        }
        IMediaSample_SetActualDataLength(sample, packet->size);
        t0 = packet->start - start;
        t1 = packet->stop - start;
        IMediaSample_SetTime(sample, &t0, &t1);
        IMediaSample_SetMediaTime(sample, NULL, NULL);
        IMediaSample_SetDiscontinuity(sample, discontinuity);
        IMediaSample_SetSyncPoint(sample, packet->sync);
        IMediaSample_SetPreroll(sample, packet->stop <= start);
        discontinuity = FALSE;

        hr = IMemInputPin_Receive(stream->source.pMemInputPin, sample);
        IMediaSample_Release(sample);
        if (hr != S_OK)
        {
            TRACE("Receive returned %#lx.\n", hr);
            break;
        }
    }

    if (i == stream->count && !filter->stopping)
        IPin_EndOfStream(peer);

    TRACE("Streaming thread for pin %s exiting.\n", debugstr_w(stream->source.pin.name));
    return 0;
}

static void start_threads(struct splitter *filter)
{
    unsigned int i;

    filter->stopping = FALSE;
    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream->source.pin.peer && stream->source.pAllocator)
            stream->thread = CreateThread(NULL, 0, stream_thread, stream, 0, NULL);
    }
}

static void stop_threads(struct splitter *filter)
{
    unsigned int i;

    filter->stopping = TRUE;
    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream->thread)
        {
            WaitForSingleObject(stream->thread, INFINITE);
            CloseHandle(stream->thread);
            stream->thread = NULL;
        }
    }
}

static HRESULT splitter_init_stream(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);
    unsigned int i;
    HRESULT hr;

    if (!filter->sink.pin.peer)
        return S_OK;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream->source.pin.peer && stream->source.pAllocator
                && FAILED(hr = IMemAllocator_Commit(stream->source.pAllocator)))
            ERR("Failed to commit allocator, hr %#lx.\n", hr);
    }
    filter->streaming = TRUE;
    start_threads(filter);
    return S_OK;
}

static HRESULT splitter_cleanup_stream(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);
    unsigned int i;

    if (!filter->streaming)
        return S_OK;

    filter->stopping = TRUE;
    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream->source.pin.peer && stream->source.pAllocator)
            IMemAllocator_Decommit(stream->source.pAllocator);
    }
    stop_threads(filter);
    filter->streaming = FALSE;
    return S_OK;
}

/* Source pins ************************************************************/

static HRESULT WINAPI stream_seeking_QueryInterface(IMediaSeeking *iface, REFIID iid, void **out)
{
    struct split_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&stream->source.pin.IPin_iface, iid, out);
}

static ULONG WINAPI stream_seeking_AddRef(IMediaSeeking *iface)
{
    struct split_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&stream->source.pin.IPin_iface);
}

static ULONG WINAPI stream_seeking_Release(IMediaSeeking *iface)
{
    struct split_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_Release(&stream->source.pin.IPin_iface);
}

static HRESULT WINAPI stream_seeking_change_rate(IMediaSeeking *iface)
{
    FIXME("Changing the rate is not implemented.\n");
    return S_OK;
}

static void sync_positions(struct split_stream *from)
{
    struct splitter *filter = from->filter;
    LONGLONG current, stop;
    unsigned int i;

    EnterCriticalSection(&from->seek.cs);
    current = from->seek.llCurrent;
    stop = from->seek.llStop;
    LeaveCriticalSection(&from->seek.cs);

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        if (stream == from)
            continue;
        EnterCriticalSection(&stream->seek.cs);
        stream->seek.llCurrent = current;
        stream->seek.llStop = stop;
        LeaveCriticalSection(&stream->seek.cs);
    }
    filter->seek_start = current;
    filter->seek_stop = stop;
}

static HRESULT WINAPI stream_seeking_change_stop(IMediaSeeking *iface)
{
    struct split_stream *stream = impl_from_IMediaSeeking(iface);

    EnterCriticalSection(&stream->filter->filter.filter_cs);
    sync_positions(stream);
    LeaveCriticalSection(&stream->filter->filter.filter_cs);
    return S_OK;
}

static HRESULT WINAPI stream_seeking_change_start(IMediaSeeking *iface)
{
    struct split_stream *stream = impl_from_IMediaSeeking(iface);
    struct splitter *filter = stream->filter;
    unsigned int i;

    EnterCriticalSection(&filter->filter.filter_cs);
    sync_positions(stream);

    if (filter->streaming)
    {
        for (i = 0; i < filter->stream_count; ++i)
        {
            if (filter->streams[i]->source.pin.peer)
                IPin_BeginFlush(filter->streams[i]->source.pin.peer);
        }
        stop_threads(filter);
        for (i = 0; i < filter->stream_count; ++i)
        {
            if (filter->streams[i]->source.pin.peer)
                IPin_EndFlush(filter->streams[i]->source.pin.peer);
        }
        start_threads(filter);
    }

    LeaveCriticalSection(&filter->filter.filter_cs);
    return S_OK;
}

static const IMediaSeekingVtbl stream_seeking_vtbl =
{
    stream_seeking_QueryInterface,
    stream_seeking_AddRef,
    stream_seeking_Release,
    SourceSeekingImpl_GetCapabilities,
    SourceSeekingImpl_CheckCapabilities,
    SourceSeekingImpl_IsFormatSupported,
    SourceSeekingImpl_QueryPreferredFormat,
    SourceSeekingImpl_GetTimeFormat,
    SourceSeekingImpl_IsUsingTimeFormat,
    SourceSeekingImpl_SetTimeFormat,
    SourceSeekingImpl_GetDuration,
    SourceSeekingImpl_GetStopPosition,
    SourceSeekingImpl_GetCurrentPosition,
    SourceSeekingImpl_ConvertTimeFormat,
    SourceSeekingImpl_SetPositions,
    SourceSeekingImpl_GetPositions,
    SourceSeekingImpl_GetAvailable,
    SourceSeekingImpl_SetRate,
    SourceSeekingImpl_GetRate,
    SourceSeekingImpl_GetPreroll,
};

static HRESULT source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct split_stream *stream = impl_from_source_pin(iface);

    if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &stream->seek.IMediaSeeking_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT source_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct split_stream *stream = impl_from_source_pin(iface);

    if (IsEqualGUID(&mt->majortype, &stream->mt.majortype)
            && (IsEqualGUID(&mt->subtype, &GUID_NULL) || IsEqualGUID(&mt->subtype, &stream->mt.subtype))
            && (IsEqualGUID(&mt->formattype, &GUID_NULL) || IsEqualGUID(&mt->formattype, &stream->mt.formattype)))
        return S_OK;
    return S_FALSE;
}

static HRESULT source_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct split_stream *stream = impl_from_source_pin(iface);

    if (index)
        return VFW_S_NO_MORE_ITEMS;
    return CopyMediaType(mt, &stream->mt);
}

static HRESULT WINAPI source_attempt_connection(struct strmbase_source *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct split_stream *stream = impl_from_source_pin(&iface->pin);

    /* Always connect with our full format, not with a partial one. */
    if (source_query_accept(&iface->pin, mt) != S_OK)
        return VFW_E_TYPE_NOT_ACCEPTED;
    return BaseOutputPinImpl_AttemptConnection(iface, peer, &stream->mt);
}

static HRESULT WINAPI source_decide_buffer_size(struct strmbase_source *iface,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    struct split_stream *stream = impl_from_source_pin(&iface->pin);
    ALLOCATOR_PROPERTIES actual;

    props->cbBuffer = max(props->cbBuffer, (LONG)stream->max_size);
    props->cBuffers = max(props->cBuffers, stream->video ? 4 : 8);
    if (!props->cbAlign)
        props->cbAlign = 1;
    return IMemAllocator_SetProperties(allocator, props, &actual);
}

static const struct strmbase_source_ops source_ops =
{
    .base.pin_query_accept = source_query_accept,
    .base.pin_get_media_type = source_get_media_type,
    .base.pin_query_interface = source_query_interface,
    .pfnAttemptConnection = source_attempt_connection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = source_decide_buffer_size,
};

static struct split_stream *add_stream(struct splitter *filter, const WCHAR *name)
{
    struct split_stream *stream, **streams;

    if (!(streams = realloc(filter->streams, (filter->stream_count + 1) * sizeof(*streams))))
        return NULL;
    filter->streams = streams;
    if (!(stream = calloc(1, sizeof(*stream))))
        return NULL;

    strmbase_source_init(&stream->source, &filter->filter, name, &source_ops);
    strmbase_seeking_init(&stream->seek, &stream_seeking_vtbl, stream_seeking_change_stop,
            stream_seeking_change_start, stream_seeking_change_rate);
    stream->filter = filter;
    filter->streams[filter->stream_count++] = stream;
    return stream;
}

static void free_streams(struct splitter *filter)
{
    unsigned int i;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];

        if (stream->source.pin.peer)
        {
            IPin_Disconnect(stream->source.pin.peer);
            IPin_Disconnect(&stream->source.pin.IPin_iface);
        }
        FreeMediaType(&stream->mt);
        free(stream->packets);
        strmbase_seeking_cleanup(&stream->seek);
        strmbase_source_cleanup(&stream->source);
        free(stream);
    }
    free(filter->streams);
    filter->streams = NULL;
    filter->stream_count = 0;
    BaseFilterImpl_IncrementPinVersion(&filter->filter);
}

/* Input pin **************************************************************/

static HRESULT sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->filter);

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream))
        return S_FALSE;
    switch (filter->kind)
    {
        case SPLITTER_AVI:
            return IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_Avi) ? S_OK : S_FALSE;
        case SPLITTER_MPEG:
            return IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Audio) ? S_OK : S_FALSE;
        case SPLITTER_WAVE:
            return IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_WAVE) ? S_OK : S_FALSE;
    }
    return S_FALSE;
}

static HRESULT sink_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->filter);

    if (index)
        return VFW_S_NO_MORE_ITEMS;
    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Stream;
    mt->subtype = filter->kind == SPLITTER_AVI ? MEDIASUBTYPE_Avi
            : filter->kind == SPLITTER_MPEG ? MEDIASUBTYPE_MPEG1Audio : MEDIASUBTYPE_WAVE;
    return S_OK;
}

static HRESULT sink_connect(struct strmbase_sink *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->pin.filter);
    LONGLONG available;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = IPin_QueryInterface(peer, &IID_IAsyncReader, (void **)&filter->reader)))
        return VFW_E_TYPE_NOT_ACCEPTED;
    if (FAILED(hr = IAsyncReader_Length(filter->reader, &filter->file_size, &available)))
        goto fail;

    switch (filter->kind)
    {
        case SPLITTER_AVI: hr = avi_parse(filter); break;
        case SPLITTER_MPEG: hr = mpeg_parse(filter); break;
        case SPLITTER_WAVE: hr = wave_parse(filter); break;
    }
    if (FAILED(hr))
    {
        WARN("Failed to parse the file, hr %#lx.\n", hr);
        goto fail;
    }

    filter->seek_start = 0;
    filter->seek_stop = filter->duration;
    for (i = 0; i < filter->stream_count; ++i)
    {
        struct split_stream *stream = filter->streams[i];
        stream->seek.llDuration = stream->seek.llStop = filter->duration;
        stream->seek.llCurrent = 0;
    }
    BaseFilterImpl_IncrementPinVersion(&filter->filter);
    TRACE("%u streams, duration %s.\n", filter->stream_count, debugstr_time(filter->duration));
    return S_OK;

fail:
    free_streams(filter);
    IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
    return hr == E_OUTOFMEMORY ? hr : VFW_E_TYPE_NOT_ACCEPTED;
}

static void sink_disconnect(struct strmbase_sink *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface->pin.filter);

    free_streams(filter);
    if (filter->reader)
        IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = sink_query_accept,
    .base.pin_get_media_type = sink_get_media_type,
    .sink_connect = sink_connect,
    .sink_disconnect = sink_disconnect,
};

/* Filter *****************************************************************/

static struct strmbase_pin *splitter_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);

    if (!index)
        return &filter->sink.pin;
    if (index - 1 < filter->stream_count)
        return &filter->streams[index - 1]->source.pin;
    return NULL;
}

static void splitter_destroy(struct strmbase_filter *iface)
{
    struct splitter *filter = impl_from_strmbase_filter(iface);

    if (filter->sink.pin.peer)
    {
        IPin_Disconnect(filter->sink.pin.peer);
        IPin_Disconnect(&filter->sink.pin.IPin_iface);
    }
    free_streams(filter);
    if (filter->reader)
        IAsyncReader_Release(filter->reader);
    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = splitter_get_pin,
    .filter_destroy = splitter_destroy,
    .filter_init_stream = splitter_init_stream,
    .filter_cleanup_stream = splitter_cleanup_stream,
};

static HRESULT splitter_create(IUnknown *outer, IUnknown **out, const CLSID *clsid,
        enum splitter_kind kind, const WCHAR *sink_name)
{
    struct splitter *filter;

    if (!(filter = calloc(1, sizeof(*filter))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&filter->filter, outer, clsid, &filter_ops);
    strmbase_sink_init(&filter->sink, &filter->filter, sink_name, &sink_ops, NULL);
    filter->kind = kind;

    TRACE("Created filter %p.\n", filter);
    *out = &filter->filter.IUnknown_inner;
    return S_OK;
}

HRESULT native_avi_splitter_create(IUnknown *outer, IUnknown **out)
{
    return splitter_create(outer, out, &CLSID_AviSplitter, SPLITTER_AVI, L"input pin");
}

HRESULT native_mpeg1_splitter_create(IUnknown *outer, IUnknown **out)
{
    return splitter_create(outer, out, &CLSID_MPEG1Splitter, SPLITTER_MPEG, L"Input");
}

HRESULT native_wave_parser_create(IUnknown *outer, IUnknown **out)
{
    return splitter_create(outer, out, &CLSID_WAVEParser, SPLITTER_WAVE, L"input pin");
}
