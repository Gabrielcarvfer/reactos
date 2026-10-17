/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for the Windows.Foundation.Uri runtime class
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * PowerToys' setup (C++/WinRT) makes a Windows.Foundation.Uri from the path
 * of an MSIX file; the class was not registered and the custom action died
 * with hresult_class_not_registered.
 */

#include <apitest.h>
#define COBJMACROS
#include <objbase.h>
#include <winstring.h>
#include <initguid.h>
#include <roapi.h>
#define WIDL_using_Windows_Foundation
#include <windows.foundation.h>

static HSTRING Str(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

#define ok_prop(uri, prop, expected) ok_prop_(__LINE__, #prop, (uri), (uri)->lpVtbl->get_##prop, (expected))
static void ok_prop_(int line, const char *name, IUriRuntimeClass *uri,
                     HRESULT (STDMETHODCALLTYPE *get)(IUriRuntimeClass *, HSTRING *), const WCHAR *expected)
{
    HSTRING value = NULL;
    HRESULT hr = get(uri, &value);
    const WCHAR *raw = WindowsGetStringRawBuffer(value, NULL);

    ok_(__FILE__, line)(hr == S_OK, "%s: 0x%08lx\n", name, hr);
    ok_(__FILE__, line)(!wcscmp(raw, expected), "%s: '%ls', expected '%ls'\n", name, raw, expected);
    WindowsDeleteString(value);
}

START_TEST(UriRuntimeClass)
{
    IUriRuntimeClassFactory *factory = NULL;
    IUriRuntimeClass *uri = NULL;
    HSTRING name, text;
    INT32 port = 0;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize: 0x%08lx\n", hr);

    name = Str(L"Windows.Foundation.Uri");
    hr = RoGetActivationFactory(name, &IID_IUriRuntimeClassFactory, (void **)&factory);
    WindowsDeleteString(name);
    ok(hr == S_OK, "RoGetActivationFactory(Windows.Foundation.Uri): 0x%08lx\n", hr);
    if (!factory)
    {
        skip("no Windows.Foundation.Uri\n");
        RoUninitialize();
        return;
    }

    text = Str(L"https://user:pw@www.example.com:8080/dir/file.txt?q=1#frag");
    hr = IUriRuntimeClassFactory_CreateUri(factory, text, &uri);
    WindowsDeleteString(text);
    ok(hr == S_OK, "CreateUri: 0x%08lx\n", hr);
    if (uri)
    {
        ok_prop(uri, SchemeName, L"https");
        ok_prop(uri, Host, L"www.example.com");
        ok_prop(uri, Domain, L"example.com");
        ok_prop(uri, UserName, L"user");
        ok_prop(uri, Password, L"pw");
        ok_prop(uri, Path, L"/dir/file.txt");
        ok_prop(uri, Extension, L".txt");
        ok_prop(uri, Query, L"?q=1");
        ok_prop(uri, Fragment, L"#frag");
        ok_prop(uri, RawUri, L"https://user:pw@www.example.com:8080/dir/file.txt?q=1#frag");
        ok_prop(uri, AbsoluteUri, L"https://user:pw@www.example.com:8080/dir/file.txt?q=1#frag");
        hr = IUriRuntimeClass_get_Port(uri, &port);
        ok(hr == S_OK && port == 8080, "Port: 0x%08lx, %d\n", hr, port);
        IUriRuntimeClass_Release(uri);
        uri = NULL;
    }

    /* A file path, as PowerToys passes it */
    text = Str(L"C:\\Program Files\\PowerToys\\CmdPal.msix");
    hr = IUriRuntimeClassFactory_CreateUri(factory, text, &uri);
    WindowsDeleteString(text);
    ok(hr == S_OK, "CreateUri(file path): 0x%08lx\n", hr);
    if (uri)
    {
        ok_prop(uri, SchemeName, L"file");
        ok_prop(uri, RawUri, L"C:\\Program Files\\PowerToys\\CmdPal.msix");
        ok_prop(uri, AbsoluteUri, L"file:///C:/Program%20Files/PowerToys/CmdPal.msix");
        IUriRuntimeClass_Release(uri);
        uri = NULL;
    }

    /* Not a URI */
    text = Str(L"not a uri");
    hr = IUriRuntimeClassFactory_CreateUri(factory, text, &uri);
    WindowsDeleteString(text);
    ok(hr == E_INVALIDARG, "CreateUri(not a uri): 0x%08lx\n", hr);
    ok(uri == NULL, "got %p\n", uri);
    if (uri) IUriRuntimeClass_Release(uri);

    IUriRuntimeClassFactory_Release(factory);
    RoUninitialize();
}
