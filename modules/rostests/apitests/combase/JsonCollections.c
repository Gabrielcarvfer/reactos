/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for JsonObject as a map and JsonArray as a vector
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * PowerToys.exe (C++/WinRT) reads its settings through JsonObject's
 * IMap<HSTRING, IJsonValue>; without it the runner stopped at start (an
 * uncaught hresult_no_interface).
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

static IJsonValue *Number(IJsonValueStatics *statics, double n)
{
    IJsonValue *value = NULL;
    IJsonValueStatics_CreateNumberValue(statics, n, &value);
    return value;
}

static double ValueOf(IJsonValue *value)
{
    double n = -1;
    if (value) IJsonValue_GetNumber(value, &n);
    return n;
}

static void TestObject(IJsonValueStatics *statics)
{
    IInspectable *obj = Activate(L"Windows.Data.Json.JsonObject");
    IMap_HSTRING_IJsonValue *map = NULL;
    IIterable_IKeyValuePair_HSTRING_IJsonValue *iterable = NULL;
    IIterator_IKeyValuePair_HSTRING_IJsonValue *iterator = NULL;
    IJsonValue *one = Number(statics, 1), *two = Number(statics, 2), *value = NULL;
    boolean replaced = TRUE, found = FALSE, has_current = FALSE;
    HSTRING a = Str(L"a"), b = Str(L"b"), missing = Str(L"missing");
    UINT32 size = 0, seen = 0;
    double sum = 0;
    HRESULT hr;

    if (!obj) return;
    hr = IInspectable_QueryInterface(obj, &IID_IMap_HSTRING_IJsonValue, (void **)&map);
    ok(hr == S_OK, "IMap<HSTRING, IJsonValue>: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(obj, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue, (void **)&iterable);
    ok(hr == S_OK, "IIterable<IKeyValuePair<HSTRING, IJsonValue>>: 0x%08lx\n", hr);
    if (!map || !iterable) goto done;

    hr = IMap_HSTRING_IJsonValue_Insert(map, a, one, &replaced);
    ok(hr == S_OK && !replaced, "Insert(a): 0x%08lx, replaced %d\n", hr, replaced);
    hr = IMap_HSTRING_IJsonValue_Insert(map, b, two, &replaced);
    ok(hr == S_OK && !replaced, "Insert(b): 0x%08lx, replaced %d\n", hr, replaced);
    hr = IMap_HSTRING_IJsonValue_get_Size(map, &size);
    ok(hr == S_OK && size == 2, "Size: 0x%08lx, %u\n", hr, size);
    hr = IMap_HSTRING_IJsonValue_HasKey(map, b, &found);
    ok(hr == S_OK && found, "HasKey(b): 0x%08lx, %d\n", hr, found);
    hr = IMap_HSTRING_IJsonValue_HasKey(map, missing, &found);
    ok(hr == S_OK && !found, "HasKey(missing): 0x%08lx, %d\n", hr, found);
    hr = IMap_HSTRING_IJsonValue_Lookup(map, b, &value);
    ok(hr == S_OK && ValueOf(value) == 2, "Lookup(b): 0x%08lx, %f\n", hr, ValueOf(value));
    if (value) IJsonValue_Release(value);

    /* The members, as pairs */
    hr = IIterable_IKeyValuePair_HSTRING_IJsonValue_First(iterable, &iterator);
    ok(hr == S_OK, "First: 0x%08lx\n", hr);
    if (iterator)
    {
        hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_HasCurrent(iterator, &has_current);
        while (hr == S_OK && has_current && seen < 10)
        {
            IKeyValuePair_HSTRING_IJsonValue *pair = NULL;
            HSTRING key = NULL;

            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current(iterator, &pair);
            ok(hr == S_OK, "Current: 0x%08lx\n", hr);
            if (!pair) break;
            IKeyValuePair_HSTRING_IJsonValue_get_Key(pair, &key);
            IKeyValuePair_HSTRING_IJsonValue_get_Value(pair, &value);
            ok(!wcscmp(WindowsGetStringRawBuffer(key, NULL), ValueOf(value) == 1 ? L"a" : L"b"),
               "key %ls for %f\n", WindowsGetStringRawBuffer(key, NULL), ValueOf(value));
            sum += ValueOf(value);
            if (value) IJsonValue_Release(value);
            WindowsDeleteString(key);
            IKeyValuePair_HSTRING_IJsonValue_Release(pair);
            seen++;
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext(iterator, &has_current);
        }
        ok(seen == 2 && sum == 3, "%u members, sum %f\n", seen, sum);
        IIterator_IKeyValuePair_HSTRING_IJsonValue_Release(iterator);
    }

    /* With default values (PowerToys reads its settings so) */
    {
        IJsonObjectWithDefaultValues *defaults = NULL;
        HSTRING text = NULL, fallback = Str(L"fallback");
        double number = 0;
        boolean flag = FALSE;

        hr = IInspectable_QueryInterface(obj, &IID_IJsonObjectWithDefaultValues, (void **)&defaults);
        ok(hr == S_OK, "IJsonObjectWithDefaultValues: 0x%08lx\n", hr);
        if (defaults)
        {
            hr = IJsonObjectWithDefaultValues_GetNamedNumberOrDefault(defaults, b, 7, &number);
            ok(hr == S_OK && number == 2, "GetNamedNumberOrDefault(b): 0x%08lx, %f\n", hr, number);
            hr = IJsonObjectWithDefaultValues_GetNamedNumberOrDefault(defaults, missing, 7, &number);
            ok(hr == S_OK && number == 7, "GetNamedNumberOrDefault(missing): 0x%08lx, %f\n", hr, number);
            hr = IJsonObjectWithDefaultValues_GetNamedBooleanOrDefault(defaults, missing, TRUE, &flag);
            ok(hr == S_OK && flag == TRUE, "GetNamedBooleanOrDefault(missing): 0x%08lx, %d\n", hr, flag);
            hr = IJsonObjectWithDefaultValues_GetNamedStringOrDefault(defaults, missing, fallback, &text);
            ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(text, NULL), L"fallback"),
               "GetNamedStringOrDefault(missing): 0x%08lx, %ls\n", hr, WindowsGetStringRawBuffer(text, NULL));
            WindowsDeleteString(text);
            text = NULL;
            /* (a member of another type) */
            hr = IJsonObjectWithDefaultValues_GetNamedStringOrDefault(defaults, b, fallback, &text);
            ok(hr == E_ILLEGAL_METHOD_CALL, "GetNamedStringOrDefault(number): 0x%08lx\n", hr);
            WindowsDeleteString(text);
            hr = IJsonObjectWithDefaultValues_GetNamedValueOrDefault(defaults, missing, NULL, &value);
            ok(hr == S_OK && value == NULL, "GetNamedValueOrDefault(missing, NULL): 0x%08lx, %p\n", hr, value);
            IJsonObjectWithDefaultValues_Release(defaults);
        }
        WindowsDeleteString(fallback);
    }

    /* ToString is the JSON text */
    {
        IStringable *stringable = NULL;
        IJsonValue *as_value = NULL;
        HSTRING text = NULL, json = NULL;

        hr = IInspectable_QueryInterface(obj, &IID_IStringable, (void **)&stringable);
        ok(hr == S_OK, "IStringable: 0x%08lx\n", hr);
        IInspectable_QueryInterface(obj, &IID_IJsonValue, (void **)&as_value);
        if (stringable && as_value)
        {
            hr = IStringable_ToString(stringable, &text);
            ok(hr == S_OK, "ToString: 0x%08lx\n", hr);
            IJsonValue_Stringify(as_value, &json);
            ok(!wcscmp(WindowsGetStringRawBuffer(text, NULL), WindowsGetStringRawBuffer(json, NULL)),
               "ToString %ls, Stringify %ls\n", WindowsGetStringRawBuffer(text, NULL), WindowsGetStringRawBuffer(json, NULL));
        }
        WindowsDeleteString(text);
        WindowsDeleteString(json);
        if (as_value) IJsonValue_Release(as_value);
        if (stringable) IStringable_Release(stringable);
    }

    hr = IMap_HSTRING_IJsonValue_Remove(map, a);
    ok(hr == S_OK, "Remove(a): 0x%08lx\n", hr);
    hr = IMap_HSTRING_IJsonValue_get_Size(map, &size);
    ok(hr == S_OK && size == 1, "Size after Remove: 0x%08lx, %u\n", hr, size);

done:
    if (iterable) IIterable_IKeyValuePair_HSTRING_IJsonValue_Release(iterable);
    if (map) IMap_HSTRING_IJsonValue_Release(map);
    WindowsDeleteString(a);
    WindowsDeleteString(b);
    WindowsDeleteString(missing);
    if (one) IJsonValue_Release(one);
    if (two) IJsonValue_Release(two);
    IInspectable_Release(obj);
}

static void TestArray(IJsonValueStatics *statics)
{
    IInspectable *obj = Activate(L"Windows.Data.Json.JsonArray");
    IVector_IJsonValue *vector = NULL;
    IIterable_IJsonValue *iterable = NULL;
    IIterator_IJsonValue *iterator = NULL;
    IJsonValue *value = NULL, *items[4] = { 0 };
    boolean has_current = FALSE;
    UINT32 size = 0, count = 0, i;
    double order[4] = { 0 };
    HRESULT hr;

    if (!obj) return;
    hr = IInspectable_QueryInterface(obj, &IID_IVector_IJsonValue, (void **)&vector);
    ok(hr == S_OK, "IVector<IJsonValue>: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(obj, &IID_IIterable_IJsonValue, (void **)&iterable);
    ok(hr == S_OK, "IIterable<IJsonValue>: 0x%08lx\n", hr);
    if (!vector || !iterable) goto done;

    for (i = 0; i < 3; i++)
    {
        value = Number(statics, i + 1);
        hr = IVector_IJsonValue_Append(vector, value);
        ok(hr == S_OK, "Append: 0x%08lx\n", hr);
        IJsonValue_Release(value);
    }
    value = Number(statics, 9);
    hr = IVector_IJsonValue_InsertAt(vector, 1, value);
    ok(hr == S_OK, "InsertAt: 0x%08lx\n", hr);
    IJsonValue_Release(value);
    hr = IVector_IJsonValue_get_Size(vector, &size);
    ok(hr == S_OK && size == 4, "Size: 0x%08lx, %u\n", hr, size);
    hr = IVector_IJsonValue_GetAt(vector, 1, &value);
    ok(hr == S_OK && ValueOf(value) == 9, "GetAt(1): 0x%08lx, %f\n", hr, ValueOf(value));
    if (value) IJsonValue_Release(value);
    hr = IVector_IJsonValue_GetAt(vector, 4, &value);
    ok(hr == E_BOUNDS, "GetAt(4): 0x%08lx\n", hr);
    hr = IVector_IJsonValue_RemoveAt(vector, 0);
    ok(hr == S_OK, "RemoveAt(0): 0x%08lx\n", hr);
    hr = IVector_IJsonValue_GetMany(vector, 0, 4, items, &count);
    ok(hr == S_OK && count == 3, "GetMany: 0x%08lx, %u\n", hr, count);
    for (i = 0; i < count; i++)
    {
        order[i] = ValueOf(items[i]);
        IJsonValue_Release(items[i]);
    }
    ok(order[0] == 9 && order[1] == 2 && order[2] == 3, "Order %f %f %f\n", order[0], order[1], order[2]);

    /* The elements, in order */
    hr = IIterable_IJsonValue_First(iterable, &iterator);
    ok(hr == S_OK, "First: 0x%08lx\n", hr);
    count = 0;
    if (iterator)
    {
        hr = IIterator_IJsonValue_get_HasCurrent(iterator, &has_current);
        while (hr == S_OK && has_current && count < 10)
        {
            hr = IIterator_IJsonValue_get_Current(iterator, &value);
            ok(hr == S_OK && ValueOf(value) == order[count], "element %u: %f\n", count, ValueOf(value));
            if (value) IJsonValue_Release(value);
            count++;
            hr = IIterator_IJsonValue_MoveNext(iterator, &has_current);
        }
        ok(count == 3, "%u elements\n", count);
        IIterator_IJsonValue_Release(iterator);
    }

done:
    if (iterable) IIterable_IJsonValue_Release(iterable);
    if (vector) IVector_IJsonValue_Release(vector);
    IInspectable_Release(obj);
}

START_TEST(JsonCollections)
{
    IActivationFactory *factory = NULL;
    IJsonValueStatics *statics = NULL;
    HSTRING name;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize: 0x%08lx\n", hr);
    name = Str(L"Windows.Data.Json.JsonValue");
    hr = RoGetActivationFactory(name, &IID_IJsonValueStatics, (void **)&statics);
    WindowsDeleteString(name);
    ok(hr == S_OK, "IJsonValueStatics: 0x%08lx\n", hr);
    if (!statics)
    {
        skip("Windows.Data.Json is not available\n");
        return;
    }
    TestObject(statics);
    TestArray(statics);
    IJsonValueStatics_Release(statics);
    (void)factory;
    RoUninitialize();
}
