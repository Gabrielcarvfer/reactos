/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for activating Windows.UI.ViewManagement.UISettings through combase
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Chromium (and so VS Code, Spotify...) activates UISettings with
 * RoActivateInstance to read the accent and text colors and the double-click
 * time. The interfaces are called through their vtables, as no WinRT
 * headers are needed for that.
 */

#include <apitest.h>
#include <objbase.h>

typedef struct { BYTE A, R, G, B; } WinRTColor;
typedef void *HSTRING_T;

static const GUID IID_IUISettings_  = {0x85361600, 0x1c63, 0x4627, {0xbc, 0xb1, 0x3a, 0x89, 0xe0, 0xbc, 0x9c, 0x55}};
static const GUID IID_IUISettings3_ = {0x03021be4, 0x5254, 0x4781, {0x81, 0x94, 0x51, 0x68, 0xf7, 0xd0, 0x6d, 0x7b}};

/* IInspectable has 6 methods; IUISettings::get_DoubleClickTime is the 11th
 * of IUISettings, IUISettings3::GetColorValue the 1st of IUISettings3 */
#define IUISETTINGS_GET_DOUBLECLICKTIME 16
#define IUISETTINGS3_GETCOLORVALUE 6

typedef HRESULT (WINAPI *GET_UINT32)(void *, UINT32 *);
typedef HRESULT (WINAPI *GET_COLOR)(void *, INT32, WinRTColor *);

START_TEST(UISettings)
{
    static const WCHAR Name[] = L"Windows.UI.ViewManagement.UISettings";
    HMODULE hCombase;
    HRESULT (WINAPI *pRoInitialize)(int);
    void (WINAPI *pRoUninitialize)(void);
    HRESULT (WINAPI *pWindowsCreateString)(const WCHAR *, UINT32, HSTRING_T *);
    HRESULT (WINAPI *pWindowsDeleteString)(HSTRING_T);
    HRESULT (WINAPI *pRoActivateInstance)(HSTRING_T, IUnknown **);
    IUnknown *obj = NULL;
    void **s1 = NULL, **s3 = NULL;
    HSTRING_T str = NULL;
    HRESULT hr, hrInit;

    hCombase = LoadLibraryW(L"combase.dll");
    if (!hCombase)
    {
        skip("combase.dll not available (%lu)\n", GetLastError());
        return;
    }
    pRoInitialize = (void *)GetProcAddress(hCombase, "RoInitialize");
    pRoUninitialize = (void *)GetProcAddress(hCombase, "RoUninitialize");
    pWindowsCreateString = (void *)GetProcAddress(hCombase, "WindowsCreateString");
    pWindowsDeleteString = (void *)GetProcAddress(hCombase, "WindowsDeleteString");
    pRoActivateInstance = (void *)GetProcAddress(hCombase, "RoActivateInstance");
    if (!pRoInitialize || !pRoUninitialize || !pWindowsCreateString || !pWindowsDeleteString || !pRoActivateInstance)
    {
        skip("combase.dll has no WinRT activation functions\n");
        FreeLibrary(hCombase);
        return;
    }

    hrInit = pRoInitialize(1 /* RO_INIT_MULTITHREADED */);
    ok(SUCCEEDED(hrInit), "RoInitialize: 0x%08lx\n", hrInit);

    hr = pWindowsCreateString(Name, _countof(Name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString: 0x%08lx\n", hr);

    hr = pRoActivateInstance(str, &obj);
    ok(hr == S_OK && obj != NULL, "RoActivateInstance(UISettings): 0x%08lx\n", hr);
    if (FAILED(hr) || !obj)
        goto cleanup;

    hr = obj->lpVtbl->QueryInterface(obj, &IID_IUISettings_, (void **)&s1);
    ok(hr == S_OK, "QueryInterface(IUISettings): 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        UINT32 dbl = 0;
        GET_UINT32 get_DoubleClickTime = (GET_UINT32)((void **)*s1)[IUISETTINGS_GET_DOUBLECLICKTIME];
        hr = get_DoubleClickTime(s1, &dbl);
        ok(hr == S_OK && dbl == GetDoubleClickTime(), "get_DoubleClickTime: 0x%08lx, %u (GetDoubleClickTime %u)\n",
           hr, dbl, GetDoubleClickTime());
        ((IUnknown *)s1)->lpVtbl->Release((IUnknown *)s1);
    }

    hr = obj->lpVtbl->QueryInterface(obj, &IID_IUISettings3_, (void **)&s3);
    ok(hr == S_OK, "QueryInterface(IUISettings3): 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        GET_COLOR GetColorValue = (GET_COLOR)((void **)*s3)[IUISETTINGS3_GETCOLORVALUE];
        WinRTColor bg = {0}, fg = {0}, accent = {0};

        hr = GetColorValue(s3, 0 /* UIColorType_Background */, &bg);
        ok(hr == S_OK, "GetColorValue(Background): 0x%08lx\n", hr);
        hr = GetColorValue(s3, 1 /* UIColorType_Foreground */, &fg);
        ok(hr == S_OK, "GetColorValue(Foreground): 0x%08lx\n", hr);
        hr = GetColorValue(s3, 5 /* UIColorType_Accent */, &accent);
        ok(hr == S_OK, "GetColorValue(Accent): 0x%08lx\n", hr);
        ok(bg.A == 0xff && fg.A == 0xff && accent.A == 0xff, "alpha: background %02x foreground %02x accent %02x\n",
           bg.A, fg.A, accent.A);
        ok(bg.R != fg.R, "background %02x%02x%02x and foreground %02x%02x%02x must differ\n",
           bg.R, bg.G, bg.B, fg.R, fg.G, fg.B);
        trace("accent %02x%02x%02x\n", accent.R, accent.G, accent.B);
        ((IUnknown *)s3)->lpVtbl->Release((IUnknown *)s3);
    }

    obj->lpVtbl->Release(obj);

cleanup:
    if (str)
        pWindowsDeleteString(str);
    if (SUCCEEDED(hrInit))
        pRoUninitialize();
    FreeLibrary(hCombase);
}
