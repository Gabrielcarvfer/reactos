/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for Windows.Data.Json objects as values, and Stringify
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * PowerToys' setup custom actions (C++/WinRT) put a JsonObject into another
 * one with SetNamedValue, which takes an IJsonValue: a JsonObject (and a
 * JsonArray) must also be an IJsonValue. Its DllMain threw
 * winrt::hresult_no_interface (E_NOINTERFACE) and the install hung. Settings
 * are then written with Stringify.
 */

#include <apitest.h>
#define COBJMACROS
#include <objbase.h>
#include <winstring.h>
#include <initguid.h>
#include <roapi.h>
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include <windows.foundation.h>
#define WIDL_using_Windows_Data_Json
#include <windows.data.json.h>

static HSTRING Str(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

static IInspectable *Activate(const WCHAR *Class)
{
    IInspectable *obj = NULL;
    HSTRING name = Str(Class);
    HRESULT hr = RoActivateInstance(name, &obj);
    WindowsDeleteString(name);
    ok(hr == S_OK, "RoActivateInstance(%ls): 0x%08lx\n", Class, hr);
    return SUCCEEDED(hr) ? obj : NULL;
}

static BOOL Contains(HSTRING Text, const WCHAR *Part)
{
    return wcsstr(WindowsGetStringRawBuffer(Text, NULL), Part) != NULL;
}

START_TEST(JsonStringify)
{
    IActivationFactory *factory = NULL;
    IJsonValueStatics *statics = NULL;
    IInspectable *outer_obj, *inner_obj, *array_obj;
    IJsonObject *outer = NULL, *inner = NULL, *back = NULL;
    IJsonValue *inner_value = NULL, *array_value = NULL, *string_value = NULL, *number_value = NULL, *parsed = NULL;
    IJsonArray *array = NULL;
    JsonValueType type = -1;
    HSTRING name, text = NULL;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize: 0x%08lx\n", hr);

    outer_obj = Activate(L"Windows.Data.Json.JsonObject");
    inner_obj = Activate(L"Windows.Data.Json.JsonObject");
    array_obj = Activate(L"Windows.Data.Json.JsonArray");
    name = Str(L"Windows.Data.Json.JsonValue");
    hr = RoGetActivationFactory(name, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(name);
    ok(hr == S_OK, "RoGetActivationFactory(JsonValue): 0x%08lx\n", hr);
    if (!outer_obj || !inner_obj || !array_obj || !factory)
    {
        skip("Windows.Data.Json is not available\n");
        return;
    }
    hr = IActivationFactory_QueryInterface(factory, &IID_IJsonValueStatics, (void **)&statics);
    ok(hr == S_OK, "IJsonValueStatics: 0x%08lx\n", hr);
    IInspectable_QueryInterface(outer_obj, &IID_IJsonObject, (void **)&outer);
    IInspectable_QueryInterface(inner_obj, &IID_IJsonObject, (void **)&inner);
    IInspectable_QueryInterface(array_obj, &IID_IJsonArray, (void **)&array);

    /* A JsonObject and a JsonArray are JsonValues */
    hr = IJsonObject_QueryInterface(inner, &IID_IJsonValue, (void **)&inner_value);
    ok(hr == S_OK, "JsonObject as IJsonValue: 0x%08lx\n", hr);
    hr = IJsonArray_QueryInterface(array, &IID_IJsonValue, (void **)&array_value);
    ok(hr == S_OK, "JsonArray as IJsonValue: 0x%08lx\n", hr);
    if (!inner_value || !array_value || !statics)
        goto done;

    hr = IJsonValue_get_ValueType(inner_value, &type);
    ok(hr == S_OK && type == JsonValueType_Object, "object ValueType: 0x%08lx, %d\n", hr, type);
    hr = IJsonValue_get_ValueType(array_value, &type);
    ok(hr == S_OK && type == JsonValueType_Array, "array ValueType: 0x%08lx, %d\n", hr, type);
    hr = IJsonValue_GetObject(inner_value, &back);
    ok(hr == S_OK && back == inner, "GetObject: 0x%08lx, %p (object %p)\n", hr, back, inner);
    if (back) IJsonObject_Release(back);

    /* Build {"text":"a\"b","n":1.5,"child":{},"list":[]} */
    name = Str(L"a\"b");
    IJsonValueStatics_CreateStringValue(statics, name, &string_value);
    WindowsDeleteString(name);
    IJsonValueStatics_CreateNumberValue(statics, 1.5, &number_value);
    name = Str(L"text"); hr = IJsonObject_SetNamedValue(outer, name, string_value); WindowsDeleteString(name);
    ok(hr == S_OK, "SetNamedValue(string): 0x%08lx\n", hr);
    name = Str(L"n"); hr = IJsonObject_SetNamedValue(outer, name, number_value); WindowsDeleteString(name);
    ok(hr == S_OK, "SetNamedValue(number): 0x%08lx\n", hr);
    name = Str(L"child"); hr = IJsonObject_SetNamedValue(outer, name, inner_value); WindowsDeleteString(name);
    ok(hr == S_OK, "SetNamedValue(object): 0x%08lx\n", hr);
    name = Str(L"list"); hr = IJsonObject_SetNamedValue(outer, name, array_value); WindowsDeleteString(name);
    ok(hr == S_OK, "SetNamedValue(array): 0x%08lx\n", hr);

    /* The object put in comes back as an object */
    name = Str(L"child");
    hr = IJsonObject_GetNamedObject(outer, name, &back);
    WindowsDeleteString(name);
    ok(hr == S_OK && back != NULL, "GetNamedObject: 0x%08lx\n", hr);
    if (back) IJsonObject_Release(back);

    hr = IJsonValue_Stringify(string_value, &text);
    ok(hr == S_OK, "Stringify(string): 0x%08lx\n", hr);
    if (text) ok(!wcscmp(WindowsGetStringRawBuffer(text, NULL), L"\"a\\\"b\""), "string: %ls\n", WindowsGetStringRawBuffer(text, NULL));
    WindowsDeleteString(text); text = NULL;

    hr = IJsonValue_Stringify(array_value, &text);
    ok(hr == S_OK, "Stringify(array): 0x%08lx\n", hr);
    if (text) ok(!wcscmp(WindowsGetStringRawBuffer(text, NULL), L"[]"), "array: %ls\n", WindowsGetStringRawBuffer(text, NULL));
    WindowsDeleteString(text); text = NULL;

    {
        IJsonValue *outer_value = NULL;
        IJsonObject_QueryInterface(outer, &IID_IJsonValue, (void **)&outer_value);
        if (outer_value)
        {
            hr = IJsonValue_Stringify(outer_value, &text);
            ok(hr == S_OK, "Stringify(object): 0x%08lx\n", hr);
            if (text)
            {
                const WCHAR *raw = WindowsGetStringRawBuffer(text, NULL);
                ok(raw[0] == '{' && raw[wcslen(raw) - 1] == '}', "object: %ls\n", raw);
                ok(Contains(text, L"\"text\":\"a\\\"b\""), "no text member: %ls\n", raw);
                ok(Contains(text, L"\"n\":1.5"), "no number member: %ls\n", raw);
                ok(Contains(text, L"\"child\":{}"), "no child member: %ls\n", raw);
                ok(Contains(text, L"\"list\":[]"), "no list member: %ls\n", raw);

                /* and it reads back */
                hr = IJsonValueStatics_Parse(statics, text, &parsed);
                ok(hr == S_OK, "Parse(Stringify()): 0x%08lx\n", hr);
                if (parsed) IJsonValue_Release(parsed);
            }
            WindowsDeleteString(text);
            IJsonValue_Release(outer_value);
        }
    }

done:
    if (string_value) IJsonValue_Release(string_value);
    if (number_value) IJsonValue_Release(number_value);
    if (inner_value) IJsonValue_Release(inner_value);
    if (array_value) IJsonValue_Release(array_value);
    if (array) IJsonArray_Release(array);
    if (inner) IJsonObject_Release(inner);
    if (outer) IJsonObject_Release(outer);
    if (statics) IJsonValueStatics_Release(statics);
    IActivationFactory_Release(factory);
    IInspectable_Release(array_obj);
    IInspectable_Release(inner_obj);
    IInspectable_Release(outer_obj);
    RoUninitialize();
}
