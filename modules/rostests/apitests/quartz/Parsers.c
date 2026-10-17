/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for the DirectShow file parsers (AVI Splitter, MPEG-I Stream Splitter, Wave Parser)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * What a media player (mplay32 through MCI, any DirectShow program) needs
 * from the parsers: they can be created, they connect to the File Source,
 * expose a stream pin with the right format and duration, and deliver the
 * whole file to a renderer so that the graph completes.
 * The test files are written here: a 1 s PCM WAV, ten MPEG-1 Layer 3
 * frames and a three-frame RGB AVI with an idx1 index.
 */

#define COBJMACROS
#include <apitest.h>
#include <objbase.h>
#include <dshow.h>
#include <mmreg.h>
#include <initguid.h>

DEFINE_GUID(CLSID_NullRenderer_test, 0xc1f400a4, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);

static WCHAR temp_dir[MAX_PATH];

static void write_file(const WCHAR *path, const void *data, DWORD size)
{
    HANDLE file;
    DWORD written;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create %S, error %lu.\n", path, GetLastError());
    WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
}

static void make_wav(const WCHAR *path)
{
    static BYTE data[44 + 16000];
    DWORD *p = (DWORD *)data;
    WAVEFORMATEX *wfx = (WAVEFORMATEX *)(data + 20);
    unsigned int i;

    p[0] = mmioFOURCC('R','I','F','F');
    p[1] = sizeof(data) - 8;
    p[2] = mmioFOURCC('W','A','V','E');
    p[3] = mmioFOURCC('f','m','t',' ');
    p[4] = 16;
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 1;
    wfx->nSamplesPerSec = 8000;
    wfx->nAvgBytesPerSec = 16000;
    wfx->nBlockAlign = 2;
    wfx->wBitsPerSample = 16;
    p[9] = mmioFOURCC('d','a','t','a');
    p[10] = 16000;
    for (i = 0; i < 8000; ++i)
        ((short *)(data + 44))[i] = (short)((i % 16) * 1000 - 8000);
    write_file(path, data, sizeof(data));
}

static void make_mp3(const WCHAR *path)
{
    /* MPEG-1 Layer 3, 128 kbit/s, 44100 Hz, stereo: 417-byte frames */
    static BYTE data[10 * 417];
    unsigned int i;

    memset(data, 0, sizeof(data));
    for (i = 0; i < 10; ++i)
    {
        data[i * 417 + 0] = 0xff;
        data[i * 417 + 1] = 0xfb;
        data[i * 417 + 2] = 0x90;
        data[i * 417 + 3] = 0x00;
    }
    write_file(path, data, sizeof(data));
}

static void put(BYTE **p, const void *data, DWORD size)
{
    memcpy(*p, data, size);
    *p += size;
}

static void put_dword(BYTE **p, DWORD value)
{
    put(p, &value, sizeof(value));
}

static void make_avi(const WCHAR *path)
{
    /* 8x8 RGB24, 10 frames per second, three frames */
    static BYTE data[4096];
    BITMAPINFOHEADER bih = {sizeof(bih), 8, 8, 1, 24, BI_RGB, 192};
    DWORD avih[14] = {100000, 0, 0, 0x10, 3, 0, 1, 192, 8, 8};
    DWORD strh[14] = {mmioFOURCC('v','i','d','s'), 0, 0, 0, 0, 1, 10, 0, 3, 192, 0, 0, 0, 0};
    BYTE frame[192], *p = data, *riff_size, *hdrl_size, *strl_size, *movi_size;
    unsigned int i;

    put_dword(&p, mmioFOURCC('R','I','F','F'));
    riff_size = p; put_dword(&p, 0);
    put_dword(&p, mmioFOURCC('A','V','I',' '));

    put_dword(&p, mmioFOURCC('L','I','S','T'));
    hdrl_size = p; put_dword(&p, 0);
    put_dword(&p, mmioFOURCC('h','d','r','l'));
    put_dword(&p, mmioFOURCC('a','v','i','h'));
    put_dword(&p, sizeof(avih));
    put(&p, avih, sizeof(avih));
    put_dword(&p, mmioFOURCC('L','I','S','T'));
    strl_size = p; put_dword(&p, 0);
    put_dword(&p, mmioFOURCC('s','t','r','l'));
    put_dword(&p, mmioFOURCC('s','t','r','h'));
    put_dword(&p, sizeof(strh));
    put(&p, strh, sizeof(strh));
    put_dword(&p, mmioFOURCC('s','t','r','f'));
    put_dword(&p, sizeof(bih));
    put(&p, &bih, sizeof(bih));
    *(DWORD *)strl_size = (DWORD)(p - strl_size - 4);
    *(DWORD *)hdrl_size = (DWORD)(p - hdrl_size - 4);

    put_dword(&p, mmioFOURCC('L','I','S','T'));
    movi_size = p; put_dword(&p, 0);
    put_dword(&p, mmioFOURCC('m','o','v','i'));
    for (i = 0; i < 3; ++i)
    {
        memset(frame, 0x40 * (i + 1), sizeof(frame));
        put_dword(&p, mmioFOURCC('0','0','d','b'));
        put_dword(&p, sizeof(frame));
        put(&p, frame, sizeof(frame));
    }
    *(DWORD *)movi_size = (DWORD)(p - movi_size - 4);

    put_dword(&p, mmioFOURCC('i','d','x','1'));
    put_dword(&p, 3 * 16);
    for (i = 0; i < 3; ++i)
    {
        put_dword(&p, mmioFOURCC('0','0','d','b'));
        put_dword(&p, 0x10);
        put_dword(&p, 4 + i * (8 + sizeof(frame)));
        put_dword(&p, sizeof(frame));
    }
    *(DWORD *)riff_size = (DWORD)(p - riff_size - 4);
    write_file(path, data, (DWORD)(p - data));
}

static IPin *get_pin(IBaseFilter *filter, PIN_DIRECTION wanted, unsigned int index)
{
    IEnumPins *enum_pins;
    PIN_DIRECTION dir;
    IPin *pin;

    if (FAILED(IBaseFilter_EnumPins(filter, &enum_pins)))
        return NULL;
    while (IEnumPins_Next(enum_pins, 1, &pin, NULL) == S_OK)
    {
        IPin_QueryDirection(pin, &dir);
        if (dir == wanted && !index--)
        {
            IEnumPins_Release(enum_pins);
            return pin;
        }
        IPin_Release(pin);
    }
    IEnumPins_Release(enum_pins);
    return NULL;
}

static void test_create(const CLSID *clsid, const char *name)
{
    IBaseFilter *filter;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&filter);
    ok(hr == S_OK, "%s: CoCreateInstance returned %#lx.\n", name, hr);
    if (SUCCEEDED(hr))
        IBaseFilter_Release(filter);
}

static void test_file(const CLSID *clsid, const WCHAR *file_name, const GUID *majortype,
        const GUID *subtype, REFERENCE_TIME duration)
{
    IBaseFilter *source = NULL, *parser = NULL, *renderer = NULL;
    IPin *source_out = NULL, *parser_in = NULL, *parser_out = NULL, *renderer_in = NULL;
    IGraphBuilder *graph = NULL;
    IMediaControl *control = NULL;
    IMediaEvent *event = NULL;
    IMediaSeeking *seeking = NULL;
    AM_MEDIA_TYPE mt;
    WCHAR path[MAX_PATH];
    LONGLONG got;
    LONG code;
    HRESULT hr;

    wcscpy(path, temp_dir);
    wcscat(path, file_name);

    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void **)&graph);
    ok(hr == S_OK, "CoCreateInstance(FilterGraph) returned %#lx.\n", hr);
    if (FAILED(hr))
        return;

    hr = IGraphBuilder_AddSourceFilter(graph, path, L"source", &source);
    ok(hr == S_OK, "%S: AddSourceFilter returned %#lx.\n", file_name, hr);
    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&parser);
    ok(hr == S_OK, "%S: creating the parser returned %#lx.\n", file_name, hr);
    if (!source || !parser)
        goto out;
    IGraphBuilder_AddFilter(graph, parser, L"parser");

    source_out = get_pin(source, PINDIR_OUTPUT, 0);
    parser_in = get_pin(parser, PINDIR_INPUT, 0);
    ok(source_out && parser_in, "%S: missing pins.\n", file_name);
    if (!source_out || !parser_in)
        goto out;
    hr = IGraphBuilder_ConnectDirect(graph, source_out, parser_in, NULL);
    ok(hr == S_OK, "%S: connecting the parser returned %#lx.\n", file_name, hr);
    if (FAILED(hr))
        goto out;

    parser_out = get_pin(parser, PINDIR_OUTPUT, 0);
    ok(parser_out != NULL, "%S: the parser has no output pin.\n", file_name);
    if (!parser_out)
        goto out;

    hr = IPin_ConnectionMediaType(parser_out, &mt);
    ok(hr == VFW_E_NOT_CONNECTED, "%S: ConnectionMediaType returned %#lx.\n", file_name, hr);
    {
        IEnumMediaTypes *enum_mt;
        AM_MEDIA_TYPE *pmt;

        hr = IPin_EnumMediaTypes(parser_out, &enum_mt);
        ok(hr == S_OK, "%S: EnumMediaTypes returned %#lx.\n", file_name, hr);
        if (SUCCEEDED(hr))
        {
            hr = IEnumMediaTypes_Next(enum_mt, 1, &pmt, NULL);
            ok(hr == S_OK, "%S: no media type, hr %#lx.\n", file_name, hr);
            if (hr == S_OK)
            {
                ok(IsEqualGUID(&pmt->majortype, majortype), "%S: wrong major type.\n", file_name);
                ok(IsEqualGUID(&pmt->subtype, subtype), "%S: wrong subtype %08lx.\n", file_name, pmt->subtype.Data1);
                CoTaskMemFree(pmt->pbFormat);
                CoTaskMemFree(pmt);
            }
            IEnumMediaTypes_Release(enum_mt);
        }
    }

    hr = IPin_QueryInterface(parser_out, &IID_IMediaSeeking, (void **)&seeking);
    ok(hr == S_OK, "%S: the output pin has no IMediaSeeking, hr %#lx.\n", file_name, hr);
    if (seeking)
    {
        got = 0;
        hr = IMediaSeeking_GetDuration(seeking, &got);
        ok(hr == S_OK, "%S: GetDuration returned %#lx.\n", file_name, hr);
        ok(got >= duration - 10000 && got <= duration + 10000, "%S: duration %I64d, expected %I64d.\n",
                file_name, got, duration);
        IMediaSeeking_Release(seeking);
    }

    /* Stream the whole file into a Null Renderer (qedit). */
    hr = CoCreateInstance(&CLSID_NullRenderer_test, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void **)&renderer);
    if (FAILED(hr))
    {
        skip("No Null Renderer, hr %#lx.\n", hr);
        goto out;
    }
    IGraphBuilder_AddFilter(graph, renderer, L"renderer");
    renderer_in = get_pin(renderer, PINDIR_INPUT, 0);
    hr = IGraphBuilder_ConnectDirect(graph, parser_out, renderer_in, NULL);
    ok(hr == S_OK, "%S: connecting the renderer returned %#lx.\n", file_name, hr);
    if (FAILED(hr))
        goto out;

    IGraphBuilder_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IGraphBuilder_QueryInterface(graph, &IID_IMediaEvent, (void **)&event);
    hr = IMediaControl_Run(control);
    ok(SUCCEEDED(hr), "%S: Run returned %#lx.\n", file_name, hr);
    code = 0;
    hr = IMediaEvent_WaitForCompletion(event, 10000, &code);
    ok(hr == S_OK, "%S: WaitForCompletion returned %#lx.\n", file_name, hr);
    ok(code == EC_COMPLETE, "%S: event code %#lx.\n", file_name, code);

    /* Seek back to the start and play again. */
    hr = IGraphBuilder_QueryInterface(graph, &IID_IMediaSeeking, (void **)&seeking);
    if (SUCCEEDED(hr))
    {
        LONGLONG start = 0;
        hr = IMediaSeeking_SetPositions(seeking, &start, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
        ok(hr == S_OK, "%S: SetPositions returned %#lx.\n", file_name, hr);
        hr = IMediaControl_Run(control);
        ok(SUCCEEDED(hr), "%S: Run returned %#lx.\n", file_name, hr);
        code = 0;
        hr = IMediaEvent_WaitForCompletion(event, 10000, &code);
        ok(hr == S_OK, "%S: second WaitForCompletion returned %#lx.\n", file_name, hr);
        IMediaSeeking_Release(seeking);
    }
    IMediaControl_Stop(control);

out:
    if (event) IMediaEvent_Release(event);
    if (control) IMediaControl_Release(control);
    if (renderer_in) IPin_Release(renderer_in);
    if (parser_out) IPin_Release(parser_out);
    if (parser_in) IPin_Release(parser_in);
    if (source_out) IPin_Release(source_out);
    if (renderer) IBaseFilter_Release(renderer);
    if (parser) IBaseFilter_Release(parser);
    if (source) IBaseFilter_Release(source);
    IGraphBuilder_Release(graph);
    DeleteFileW(path);
}

START_TEST(Parsers)
{
    GUID mp3 = {WAVE_FORMAT_MPEGLAYER3, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    GUID pcm = {WAVE_FORMAT_PCM, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    WCHAR path[MAX_PATH];

    CoInitialize(NULL);

    test_create(&CLSID_AviSplitter, "AVI Splitter");
    test_create(&CLSID_MPEG1Splitter, "MPEG-I Stream Splitter");
    test_create(&CLSID_WAVEParser, "Wave Parser");

    GetTempPathW(ARRAY_SIZE(temp_dir), temp_dir);
    wcscpy(path, temp_dir); wcscat(path, L"rostest_parser.wav"); make_wav(path);
    wcscpy(path, temp_dir); wcscat(path, L"rostest_parser.mp3"); make_mp3(path);
    wcscpy(path, temp_dir); wcscat(path, L"rostest_parser.avi"); make_avi(path);

    test_file(&CLSID_WAVEParser, L"rostest_parser.wav", &MEDIATYPE_Audio, &pcm, 10000000);
    test_file(&CLSID_MPEG1Splitter, L"rostest_parser.mp3", &MEDIATYPE_Audio, &mp3, (LONGLONG)10 * 1152 * 10000000 / 44100);
    test_file(&CLSID_AviSplitter, L"rostest_parser.avi", &MEDIATYPE_Video, &MEDIASUBTYPE_RGB24, 3000000);

    CoUninitialize();
}
