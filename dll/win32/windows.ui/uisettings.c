/*
 * PROJECT:     ReactOS WinRT UI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.UI.ViewManagement.UISettings
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * The UI settings that programs read through WinRT: the colors of the theme
 * (light or dark, and the accent), mostly, and some system metrics. Qt, the
 * installers of Advanced Installer and C++/WinRT programs ask for them, and
 * some fail when the class cannot be activated.
 */

#define COBJMACROS
#include <windows.h>
#include <winstring.h>
#include <inspectable.h>
#include <activation.h>
#include <olectl.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(uisettings);

#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif

static const WCHAR ClassName[] = L"Windows.UI.ViewManagement.UISettings";

/* The WinRT base interfaces (not in every uuid library) */
static const GUID IID_Inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_ActivationFactory = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_AgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

/* The interfaces (from the public WinRT metadata of Windows.UI.ViewManagement) */
static const GUID IID_IUISettings  = {0x85361600, 0x1c63, 0x4627, {0xbc, 0xb1, 0x3a, 0x89, 0xe0, 0xbc, 0x9c, 0x55}};
static const GUID IID_IUISettings2 = {0xbad82401, 0x2721, 0x44f9, {0xbb, 0x91, 0x2b, 0xb2, 0x28, 0xbe, 0x44, 0x2f}};
static const GUID IID_IUISettings3 = {0x03021be4, 0x5254, 0x4781, {0x81, 0x94, 0x51, 0x68, 0xf7, 0xd0, 0x6d, 0x7b}};
static const GUID IID_IUISettings4 = {0x52bb3002, 0x919b, 0x4d6b, {0x9b, 0x78, 0x8d, 0xd6, 0x6f, 0xf4, 0xb9, 0x3b}};
static const GUID IID_IUISettings5 = {0x5349d588, 0x0cb5, 0x5f05, {0xbd, 0x34, 0x70, 0x6b, 0x32, 0x31, 0xf0, 0xbd}};

typedef struct { INT64 value; } EventRegistrationToken;
typedef struct { FLOAT Width, Height; } WinRTSize;
typedef struct { BYTE A, R, G, B; } WinRTColor;

/* Windows.UI.ViewManagement.UIColorType */
enum
{
    UIColorType_Background, UIColorType_Foreground,
    UIColorType_AccentDark3, UIColorType_AccentDark2, UIColorType_AccentDark1,
    UIColorType_Accent,
    UIColorType_AccentLight1, UIColorType_AccentLight2, UIColorType_AccentLight3,
    UIColorType_Complement
};

typedef struct
{
    IInspectable IUISettings_iface;   /* the default interface */
    IInspectable IUISettings2_iface;
    IInspectable IUISettings3_iface;
    IInspectable IUISettings4_iface;
    IInspectable IUISettings5_iface;
    LONG refs;
} UISettings;

/* The five interfaces derive from IInspectable; one implementation of
 * IInspectable serves them all, with the object found from each.
 * (The parentheses matter: the CONTAINING_RECORD of the SDK casts its
 * address argument without them, so "iface - index" became "(ULONG_PTR)iface
 * - index", index bytes instead of index interfaces. AddRef and Release
 * through IUISettings2/3/4 then changed memory past the object: Firefox
 * crashed on it with page heap, and corrupted the heap without.) */
static UISettings *impl_from(IInspectable *iface, int index)
{
    return CONTAINING_RECORD((iface - index), UISettings, IUISettings_iface);
}

static HRESULT query_interface(UISettings *This, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_Inspectable) ||
        IsEqualGUID(riid, &IID_AgileObject) || IsEqualGUID(riid, &IID_IUISettings))
        *out = &This->IUISettings_iface;
    else if (IsEqualGUID(riid, &IID_IUISettings2))
        *out = &This->IUISettings2_iface;
    else if (IsEqualGUID(riid, &IID_IUISettings3))
        *out = &This->IUISettings3_iface;
    else if (IsEqualGUID(riid, &IID_IUISettings4))
        *out = &This->IUISettings4_iface;
    else if (IsEqualGUID(riid, &IID_IUISettings5))
        *out = &This->IUISettings5_iface;
    else
    {
        TRACE("no interface %s\n", debugstr_guid(riid));
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&This->refs);
    return S_OK;
}

static ULONG release(UISettings *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, This);
    return refs;
}

static HRESULT get_iids(ULONG *count, IID **iids)
{
    IID *list = CoTaskMemAlloc(5 * sizeof(IID));
    if (!list) return E_OUTOFMEMORY;
    list[0] = IID_IUISettings;
    list[1] = IID_IUISettings2;
    list[2] = IID_IUISettings3;
    list[3] = IID_IUISettings4;
    list[4] = IID_IUISettings5;
    *count = 5;
    *iids = list;
    return S_OK;
}

#define INSPECTABLE_METHODS(prefix, index) \
static HRESULT WINAPI prefix##_QueryInterface(IInspectable *iface, REFIID riid, void **out) \
{ return query_interface(impl_from(iface, index), riid, out); } \
static ULONG WINAPI prefix##_AddRef(IInspectable *iface) \
{ return InterlockedIncrement(&impl_from(iface, index)->refs); } \
static ULONG WINAPI prefix##_Release(IInspectable *iface) \
{ return release(impl_from(iface, index)); } \
static HRESULT WINAPI prefix##_GetIids(IInspectable *iface, ULONG *count, IID **iids) \
{ return get_iids(count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(IInspectable *iface, HSTRING *name) \
{ return WindowsCreateString(ClassName, ARRAYSIZE(ClassName) - 1, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(IInspectable *iface, TrustLevel *level) \
{ *level = BaseTrust; return S_OK; }

INSPECTABLE_METHODS(settings, 0)
INSPECTABLE_METHODS(settings2, 1)
INSPECTABLE_METHODS(settings3, 2)
INSPECTABLE_METHODS(settings4, 3)
INSPECTABLE_METHODS(settings5, 4)

/* Events: nothing changes these settings while a program runs here, so the
 * handlers are accepted and never called */
static HRESULT WINAPI add_event(IInspectable *iface, IUnknown *handler, EventRegistrationToken *token)
{
    static LONG64 next;
    if (!handler || !token) return E_POINTER;
    token->value = InterlockedIncrement64(&next);
    return S_OK;
}

static HRESULT WINAPI remove_event(IInspectable *iface, EventRegistrationToken token)
{
    return S_OK;
}

static WinRTColor color_from_rgb(COLORREF rgb)
{
    WinRTColor color = { 0xff, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb) };
    return color;
}

/* IUISettings */

static HRESULT WINAPI settings_get_HandPreference(IInspectable *iface, INT32 *value)
{
    *value = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? 0 /* LeftHanded */ : 1 /* RightHanded */;
    return S_OK;
}

static HRESULT size_from_metrics(WinRTSize *value, int cx, int cy)
{
    value->Width = (FLOAT)GetSystemMetrics(cx);
    value->Height = (FLOAT)GetSystemMetrics(cy);
    return S_OK;
}

static HRESULT WINAPI settings_get_CursorSize(IInspectable *iface, WinRTSize *value)
{
    return size_from_metrics(value, SM_CXCURSOR, SM_CYCURSOR);
}

static HRESULT WINAPI settings_get_ScrollBarSize(IInspectable *iface, WinRTSize *value)
{
    return size_from_metrics(value, SM_CXVSCROLL, SM_CYHSCROLL);
}

static HRESULT WINAPI settings_get_ScrollBarArrowSize(IInspectable *iface, WinRTSize *value)
{
    return size_from_metrics(value, SM_CXHSCROLL, SM_CYVSCROLL);
}

static HRESULT WINAPI settings_get_ScrollBarThumbBoxSize(IInspectable *iface, WinRTSize *value)
{
    return size_from_metrics(value, SM_CXHTHUMB, SM_CYVTHUMB);
}

static HRESULT WINAPI settings_get_MessageDuration(IInspectable *iface, UINT32 *value)
{
    *value = 5; /* seconds */
    return S_OK;
}

static HRESULT WINAPI settings_get_AnimationsEnabled(IInspectable *iface, boolean *value)
{
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    *value = !!enabled;
    return S_OK;
}

static HRESULT WINAPI settings_get_CaretBrowsingEnabled(IInspectable *iface, boolean *value)
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI settings_get_CaretBlinkRate(IInspectable *iface, UINT32 *value)
{
    *value = GetCaretBlinkTime();
    return S_OK;
}

static HRESULT WINAPI settings_get_CaretWidth(IInspectable *iface, UINT32 *value)
{
    DWORD width = 1;
    SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
    *value = width;
    return S_OK;
}

static HRESULT WINAPI settings_get_DoubleClickTime(IInspectable *iface, UINT32 *value)
{
    *value = GetDoubleClickTime();
    return S_OK;
}

static HRESULT WINAPI settings_get_MouseHoverTime(IInspectable *iface, UINT32 *value)
{
    UINT time = 400;
    SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &time, 0);
    *value = time;
    return S_OK;
}

static HRESULT WINAPI settings_UIElementColor(IInspectable *iface, INT32 element, WinRTColor *value)
{
    /* Windows.UI.ViewManagement.UIElementType, in the order of these system colors */
    static const int Colors[] =
    {
        COLOR_ACTIVECAPTION, COLOR_BACKGROUND, COLOR_BTNFACE, COLOR_BTNTEXT, COLOR_CAPTIONTEXT,
        COLOR_GRAYTEXT, COLOR_HIGHLIGHT, COLOR_HIGHLIGHTTEXT, COLOR_HOTLIGHT, COLOR_INACTIVECAPTION,
        COLOR_INACTIVECAPTIONTEXT, COLOR_WINDOW, COLOR_WINDOWTEXT
    };

    if (!value) return E_POINTER;
    *value = color_from_rgb(GetSysColor(element >= 0 && element < ARRAYSIZE(Colors) ?
                                        Colors[element] : COLOR_HIGHLIGHT));
    return S_OK;
}

/* IUISettings2 */

static HRESULT WINAPI settings2_get_TextScaleFactor(IInspectable *iface, DOUBLE *value)
{
    *value = 1.0;
    return S_OK;
}

/* IUISettings3 */

static BOOL apps_use_dark_theme(void)
{
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &size);
    return !light;
}

static WinRTColor shade(WinRTColor color, int step)
{
    /* Lighter (step > 0) or darker (step < 0), by steps of a fifth */
    BYTE *channels[] = { &color.R, &color.G, &color.B };
    int i;
    for (i = 0; i < 3; i++)
    {
        int c = *channels[i];
        c = step > 0 ? c + (255 - c) * step / 5 : c + c * step / 5;
        *channels[i] = (BYTE)min(max(c, 0), 255);
    }
    return color;
}

static HRESULT WINAPI settings3_GetColorValue(IInspectable *iface, INT32 type, WinRTColor *value)
{
    WinRTColor accent = color_from_rgb(GetSysColor(COLOR_HIGHLIGHT));
    BOOL dark = apps_use_dark_theme();

    if (!value) return E_POINTER;
    switch (type)
    {
        case UIColorType_Background: *value = color_from_rgb(dark ? RGB(0, 0, 0) : RGB(255, 255, 255)); break;
        case UIColorType_Foreground: *value = color_from_rgb(dark ? RGB(255, 255, 255) : RGB(0, 0, 0)); break;
        case UIColorType_AccentDark3: *value = shade(accent, -3); break;
        case UIColorType_AccentDark2: *value = shade(accent, -2); break;
        case UIColorType_AccentDark1: *value = shade(accent, -1); break;
        case UIColorType_Accent: *value = accent; break;
        case UIColorType_AccentLight1: *value = shade(accent, 1); break;
        case UIColorType_AccentLight2: *value = shade(accent, 2); break;
        case UIColorType_AccentLight3: *value = shade(accent, 3); break;
        case UIColorType_Complement:
            *value = accent;
            value->R = 255 - accent.R; value->G = 255 - accent.G; value->B = 255 - accent.B;
            break;
        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

/* IUISettings4 */

static HRESULT WINAPI settings4_get_AdvancedEffectsEnabled(IInspectable *iface, boolean *value)
{
    *value = TRUE;
    return S_OK;
}

/* IUISettings5 (Windows 10 2004): the scroll bars of Windows 11 that hide
 * themselves; those here always show */

static HRESULT WINAPI settings5_get_AutoHideScrollBars(IInspectable *iface, boolean *value)
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

#define INSPECTABLE_VTBL(prefix) \
    prefix##_QueryInterface, prefix##_AddRef, prefix##_Release, \
    prefix##_GetIids, prefix##_GetRuntimeClassName, prefix##_GetTrustLevel

static const void *settings_vtbl[] =
{
    INSPECTABLE_VTBL(settings),
    settings_get_HandPreference,
    settings_get_CursorSize,
    settings_get_ScrollBarSize,
    settings_get_ScrollBarArrowSize,
    settings_get_ScrollBarThumbBoxSize,
    settings_get_MessageDuration,
    settings_get_AnimationsEnabled,
    settings_get_CaretBrowsingEnabled,
    settings_get_CaretBlinkRate,
    settings_get_CaretWidth,
    settings_get_DoubleClickTime,
    settings_get_MouseHoverTime,
    settings_UIElementColor,
};

static const void *settings2_vtbl[] =
{
    INSPECTABLE_VTBL(settings2),
    settings2_get_TextScaleFactor,
    add_event,      /* add_TextScaleFactorChanged */
    remove_event,
};

static const void *settings3_vtbl[] =
{
    INSPECTABLE_VTBL(settings3),
    settings3_GetColorValue,
    add_event,      /* add_ColorValuesChanged */
    remove_event,
};

static const void *settings4_vtbl[] =
{
    INSPECTABLE_VTBL(settings4),
    settings4_get_AdvancedEffectsEnabled,
    add_event,      /* add_AdvancedEffectsEnabledChanged */
    remove_event,
};

static const void *settings5_vtbl[] =
{
    INSPECTABLE_VTBL(settings5),
    settings5_get_AutoHideScrollBars,
    add_event,      /* add_AutoHideScrollBarsChanged */
    remove_event,
};

/* The activation factory */

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_Inspectable) ||
        IsEqualGUID(riid, &IID_AgileObject) || IsEqualGUID(riid, &IID_ActivationFactory))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    return WindowsCreateString(ClassName, ARRAYSIZE(ClassName) - 1, name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    UISettings *settings;

    if (!instance) return E_POINTER;
    settings = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*settings));
    if (!settings) return E_OUTOFMEMORY;
    settings->IUISettings_iface.lpVtbl = (void *)settings_vtbl;
    settings->IUISettings2_iface.lpVtbl = (void *)settings2_vtbl;
    settings->IUISettings3_iface.lpVtbl = (void *)settings3_vtbl;
    settings->IUISettings4_iface.lpVtbl = (void *)settings4_vtbl;
    settings->IUISettings5_iface.lpVtbl = (void *)settings5_vtbl;
    settings->refs = 1;
    *instance = &settings->IUISettings_iface;
    return S_OK;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static IActivationFactory Factory = { (IActivationFactoryVtbl *)&factory_vtbl };

HRESULT compositor_controller_get_factory(const WCHAR *name, IActivationFactory **factory);

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("%s\n", debugstr_w(name));
    *factory = NULL;
    if (!name)
        return CLASS_E_CLASSNOTAVAILABLE;
    if (wcscmp(name, ClassName))
        return compositor_controller_get_factory(name, factory);
    *factory = &Factory;
    return S_OK;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

#define CLASS_KEY L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.ViewManagement.UISettings"
#define COMPOSITION_KEY L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Core.CompositorController"

/* The registration that RoActivateInstance looks up (C++/WinRT programs
 * also find this DLL by its name) */
HRESULT WINAPI DllRegisterServer(void)
{
    static const WCHAR Path[] = L"%SystemRoot%\\system32\\Windows.UI.dll";
    DWORD zero = 0;
    HKEY key;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, CLASS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
        return SELFREG_E_CLASS;
    RegSetValueExW(key, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)Path, sizeof(Path));
    RegSetValueExW(key, L"ActivationType", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero)); /* in process */
    RegSetValueExW(key, L"Threading", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));      /* both */
    RegSetValueExW(key, L"TrustLevel", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));     /* base */
    RegCloseKey(key);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, COMPOSITION_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
        return SELFREG_E_CLASS;
    RegSetValueExW(key, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)Path, sizeof(Path));
    RegSetValueExW(key, L"ActivationType", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
    RegSetValueExW(key, L"Threading", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
    RegSetValueExW(key, L"TrustLevel", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
    RegCloseKey(key);
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, COMPOSITION_KEY);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, CLASS_KEY);
    return S_OK;
}
