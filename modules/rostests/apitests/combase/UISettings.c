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
 *
 * Firefox activates it at start-up and asks for IUISettings5, 2, 3 and 4 in
 * turn, adds a change handler on each and releases them (WindowsUIUtils.cpp,
 * GetUISettings). AddRef and Release through every interface must count on
 * the same object.
 */

#include <apitest.h>
#include <objbase.h>

typedef struct { BYTE A, R, G, B; } WinRTColor;
typedef void *HSTRING_T;

static const GUID IID_IUISettings_  = {0x85361600, 0x1c63, 0x4627, {0xbc, 0xb1, 0x3a, 0x89, 0xe0, 0xbc, 0x9c, 0x55}};
static const GUID IID_IUISettings2_ = {0xbad82401, 0x2721, 0x44f9, {0xbb, 0x91, 0x2b, 0xb2, 0x28, 0xbe, 0x44, 0x2f}};
static const GUID IID_IUISettings3_ = {0x03021be4, 0x5254, 0x4781, {0x81, 0x94, 0x51, 0x68, 0xf7, 0xd0, 0x6d, 0x7b}};
static const GUID IID_IUISettings4_ = {0x52bb3002, 0x919b, 0x4d6b, {0x9b, 0x78, 0x8d, 0xd6, 0x6f, 0xf4, 0xb9, 0x3b}};
static const GUID IID_IUISettings5_ = {0x5349d588, 0x0cb5, 0x5f05, {0xbd, 0x34, 0x70, 0x6b, 0x32, 0x31, 0xf0, 0xbd}};

typedef struct { INT64 value; } EventToken;
typedef HRESULT (WINAPI *ADD_EVENT)(void *, IUnknown *, EventToken *);
typedef HRESULT (WINAPI *REMOVE_EVENT)(void *, EventToken);

/* A change handler (only its IUnknown is used here) */
static LONG HandlerRefs = 1;
static HRESULT WINAPI Handler_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI Handler_AddRef(IUnknown *iface) { return InterlockedIncrement(&HandlerRefs); }
static ULONG WINAPI Handler_Release(IUnknown *iface) { return InterlockedDecrement(&HandlerRefs); }
static IUnknownVtbl HandlerVtbl = { Handler_QueryInterface, Handler_AddRef, Handler_Release };
static IUnknown Handler = { &HandlerVtbl };

/* What Firefox does with one interface: QueryInterface, add a change handler
 * (the method right after the property; slot 7 on IUISettings2/4/5, whose
 * first method is a property, as on IUISettings3, whose first is
 * GetColorValue), remove it and release the interface */
static void test_interface(IUnknown *obj, REFIID riid, const char *name, BOOL optional)
{
    IUnknown *iface = NULL;
    EventToken token = { 0 };
    ULONG refs;
    HRESULT hr;

    hr = obj->lpVtbl->QueryInterface(obj, riid, (void **)&iface);
    if (optional && hr == E_NOINTERFACE)
    {
        skip("no %s\n", name);
        return;
    }
    ok(hr == S_OK && iface != NULL, "QueryInterface(%s): 0x%08lx\n", name, hr);
    if (FAILED(hr) || !iface)
        return;

    /* AddRef and Release through this interface count on the object */
    refs = iface->lpVtbl->AddRef(iface);
    ok(refs == 3, "%s AddRef: %lu, expected 3\n", name, refs);
    refs = obj->lpVtbl->Release(obj);
    ok(refs == 2, "IInspectable Release after %s AddRef: %lu, expected 2\n", name, refs);
    refs = obj->lpVtbl->AddRef(obj);
    ok(refs == 3, "IInspectable AddRef: %lu, expected 3\n", refs);
    refs = iface->lpVtbl->Release(iface);
    ok(refs == 2, "%s Release: %lu, expected 2\n", name, refs);

    hr = ((ADD_EVENT)((void **)*(void **)iface)[7])(iface, &Handler, &token);
    ok(hr == S_OK, "%s add_...Changed: 0x%08lx\n", name, hr);
    ok(token.value != 0, "%s: token 0\n", name);
    hr = ((REMOVE_EVENT)((void **)*(void **)iface)[8])(iface, token);
    ok(hr == S_OK, "%s remove_...Changed: 0x%08lx\n", name, hr);

    refs = iface->lpVtbl->Release(iface);
    ok(refs == 1, "%s final Release: %lu, expected 1\n", name, refs);
}

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

    /* Firefox's start-up */
    test_interface(obj, &IID_IUISettings5_, "IUISettings5", TRUE);
    test_interface(obj, &IID_IUISettings2_, "IUISettings2", FALSE);
    test_interface(obj, &IID_IUISettings3_, "IUISettings3", FALSE);
    test_interface(obj, &IID_IUISettings4_, "IUISettings4", FALSE);
    ok(HandlerRefs == 1, "the handler has %ld references, expected 1\n", HandlerRefs);

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
        ok(((IUnknown *)s3)->lpVtbl->Release((IUnknown *)s3) == 1, "IUISettings3 Release must leave 1 reference\n");
    }

    ok(obj->lpVtbl->Release(obj) == 0, "the last Release must return 0\n");

cleanup:
    if (str)
        pWindowsDeleteString(str);
    if (SUCCEEDED(hrInit))
        pRoUninitialize();
    FreeLibrary(hCombase);
}
