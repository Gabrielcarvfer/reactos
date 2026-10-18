/*
 * PROJECT:     ReactOS Windows.Web
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Iterating JsonObject members and JsonArray elements
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * A JsonObject is also an IMap<HSTRING, IJsonValue> and an
 * IIterable<IKeyValuePair<HSTRING, IJsonValue>>, a JsonArray an
 * IVector<IJsonValue> and an IIterable<IJsonValue> (C++/WinRT: range-for,
 * Insert, Lookup, Size). The iterators walk a snapshot taken by First().
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

/* IKeyValuePair<HSTRING, IJsonValue *> **************************************/

struct json_pair
{
    IKeyValuePair_HSTRING_IJsonValue IKeyValuePair_HSTRING_IJsonValue_iface;
    LONG ref;
    HSTRING key;
    IJsonValue *value;
};

static inline struct json_pair *impl_from_IKeyValuePair_HSTRING_IJsonValue( IKeyValuePair_HSTRING_IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_pair, IKeyValuePair_HSTRING_IJsonValue_iface );
}

static HRESULT WINAPI pair_QueryInterface( IKeyValuePair_HSTRING_IJsonValue *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IKeyValuePair_HSTRING_IJsonValue ))
    {
        *out = iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pair_AddRef( IKeyValuePair_HSTRING_IJsonValue *iface )
{
    struct json_pair *impl = impl_from_IKeyValuePair_HSTRING_IJsonValue( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pair_Release( IKeyValuePair_HSTRING_IJsonValue *iface )
{
    struct json_pair *impl = impl_from_IKeyValuePair_HSTRING_IJsonValue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->key );
        if (impl->value) IJsonValue_Release( impl->value );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI pair_GetIids( IKeyValuePair_HSTRING_IJsonValue *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_GetRuntimeClassName( IKeyValuePair_HSTRING_IJsonValue *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_GetTrustLevel( IKeyValuePair_HSTRING_IJsonValue *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_get_Key( IKeyValuePair_HSTRING_IJsonValue *iface, HSTRING *key )
{
    struct json_pair *impl = impl_from_IKeyValuePair_HSTRING_IJsonValue( iface );

    TRACE( "iface %p, key %p.\n", iface, key );

    if (!key) return E_POINTER;
    return WindowsDuplicateString( impl->key, key );
}

static HRESULT WINAPI pair_get_Value( IKeyValuePair_HSTRING_IJsonValue *iface, IJsonValue **value )
{
    struct json_pair *impl = impl_from_IKeyValuePair_HSTRING_IJsonValue( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->value)) IJsonValue_AddRef( *value );
    return S_OK;
}

static const struct IKeyValuePair_HSTRING_IJsonValueVtbl pair_vtbl =
{
    pair_QueryInterface,
    pair_AddRef,
    pair_Release,
    /* IInspectable methods */
    pair_GetIids,
    pair_GetRuntimeClassName,
    pair_GetTrustLevel,
    /* IKeyValuePair<HSTRING, IJsonValue *> methods */
    pair_get_Key,
    pair_get_Value,
};

/* A snapshot of keys and values, shared by the iterators ********************/

struct json_items
{
    LONG ref;
    UINT32 count;
    HSTRING *keys;          /* NULL for the elements of an array */
    IJsonValue **values;
};

static void items_release( struct json_items *items )
{
    UINT32 i;

    if (InterlockedDecrement( &items->ref )) return;
    for (i = 0; i < items->count; i++)
    {
        if (items->keys) WindowsDeleteString( items->keys[i] );
        if (items->values[i]) IJsonValue_Release( items->values[i] );
    }
    free( items->keys );
    free( items->values );
    free( items );
}

static struct json_items *items_create( UINT32 count, BOOL with_keys )
{
    struct json_items *items;

    if (!(items = calloc( 1, sizeof(*items) ))) return NULL;
    items->ref = 1;
    items->values = calloc( max( count, 1 ), sizeof(*items->values) );
    items->keys = with_keys ? calloc( max( count, 1 ), sizeof(*items->keys) ) : NULL;
    if (!items->values || (with_keys && !items->keys))
    {
        free( items->values );
        free( items->keys );
        free( items );
        return NULL;
    }
    return items;
}

/* IIterator<IKeyValuePair<HSTRING, IJsonValue *> *> and IIterator<IJsonValue *>
 * (one object; the vtables differ only in the type of the items) */
struct json_iterator
{
    IIterator_IKeyValuePair_HSTRING_IJsonValue IIterator_IKeyValuePair_HSTRING_IJsonValue_iface;
    IIterator_IJsonValue IIterator_IJsonValue_iface;
    LONG ref;
    struct json_items *items;
    UINT32 index;
};

static HRESULT pair_create( HSTRING key, IJsonValue *value, IKeyValuePair_HSTRING_IJsonValue **out )
{
    struct json_pair *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IKeyValuePair_HSTRING_IJsonValue_iface.lpVtbl = &pair_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( key, &impl->key )))
    {
        free( impl );
        return hr;
    }
    if ((impl->value = value)) IJsonValue_AddRef( value );
    *out = &impl->IKeyValuePair_HSTRING_IJsonValue_iface;
    return S_OK;
}

static inline struct json_iterator *impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue(
    IIterator_IKeyValuePair_HSTRING_IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_iterator, IIterator_IKeyValuePair_HSTRING_IJsonValue_iface );
}

static HRESULT WINAPI pair_iterator_QueryInterface( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_IKeyValuePair_HSTRING_IJsonValue ))
    {
        *out = iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pair_iterator_AddRef( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pair_iterator_Release( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        items_release( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI pair_iterator_GetIids( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_iterator_GetRuntimeClassName( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_iterator_GetTrustLevel( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI pair_iterator_get_Current( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface,
                                                 IKeyValuePair_HSTRING_IJsonValue **value )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->items->count) return E_BOUNDS;
    return pair_create( impl->items->keys[impl->index], impl->items->values[impl->index], value );
}

static HRESULT WINAPI pair_iterator_get_HasCurrent( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, boolean *value )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->index < impl->items->count;
    return S_OK;
}

static HRESULT WINAPI pair_iterator_MoveNext( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, boolean *value )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->index < impl->items->count) impl->index++;
    *value = impl->index < impl->items->count;
    return S_OK;
}

static HRESULT WINAPI pair_iterator_GetMany( IIterator_IKeyValuePair_HSTRING_IJsonValue *iface, UINT32 items_size,
                                             IKeyValuePair_HSTRING_IJsonValue **items, UINT32 *count )
{
    struct json_iterator *impl = impl_from_IIterator_IKeyValuePair_HSTRING_IJsonValue( iface );
    HRESULT hr = S_OK;
    UINT32 i;

    TRACE( "iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count );

    if (!count || (items_size && !items)) return E_POINTER;
    for (i = 0; i < items_size && impl->index < impl->items->count; i++, impl->index++)
    {
        if (FAILED(hr = pair_create( impl->items->keys[impl->index], impl->items->values[impl->index], &items[i] )))
            break;
    }
    *count = i;
    return hr;
}

static const struct IIterator_IKeyValuePair_HSTRING_IJsonValueVtbl pair_iterator_vtbl =
{
    pair_iterator_QueryInterface,
    pair_iterator_AddRef,
    pair_iterator_Release,
    /* IInspectable methods */
    pair_iterator_GetIids,
    pair_iterator_GetRuntimeClassName,
    pair_iterator_GetTrustLevel,
    /* IIterator<IKeyValuePair<HSTRING, IJsonValue *> *> methods */
    pair_iterator_get_Current,
    pair_iterator_get_HasCurrent,
    pair_iterator_MoveNext,
    pair_iterator_GetMany,
};

static inline struct json_iterator *impl_from_IIterator_IJsonValue( IIterator_IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_iterator, IIterator_IJsonValue_iface );
}

static HRESULT WINAPI value_iterator_QueryInterface( IIterator_IJsonValue *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_IJsonValue ))
    {
        *out = iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI value_iterator_AddRef( IIterator_IJsonValue *iface )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI value_iterator_Release( IIterator_IJsonValue *iface )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );
    return pair_iterator_Release( &impl->IIterator_IKeyValuePair_HSTRING_IJsonValue_iface );
}

static HRESULT WINAPI value_iterator_GetIids( IIterator_IJsonValue *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI value_iterator_GetRuntimeClassName( IIterator_IJsonValue *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI value_iterator_GetTrustLevel( IIterator_IJsonValue *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI value_iterator_get_Current( IIterator_IJsonValue *iface, IJsonValue **value )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->items->count) return E_BOUNDS;
    if ((*value = impl->items->values[impl->index])) IJsonValue_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI value_iterator_get_HasCurrent( IIterator_IJsonValue *iface, boolean *value )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );
    return pair_iterator_get_HasCurrent( &impl->IIterator_IKeyValuePair_HSTRING_IJsonValue_iface, value );
}

static HRESULT WINAPI value_iterator_MoveNext( IIterator_IJsonValue *iface, boolean *value )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );
    return pair_iterator_MoveNext( &impl->IIterator_IKeyValuePair_HSTRING_IJsonValue_iface, value );
}

static HRESULT WINAPI value_iterator_GetMany( IIterator_IJsonValue *iface, UINT32 items_size,
                                              IJsonValue **items, UINT32 *count )
{
    struct json_iterator *impl = impl_from_IIterator_IJsonValue( iface );
    UINT32 i;

    TRACE( "iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count );

    if (!count || (items_size && !items)) return E_POINTER;
    for (i = 0; i < items_size && impl->index < impl->items->count; i++, impl->index++)
    {
        if ((items[i] = impl->items->values[impl->index])) IJsonValue_AddRef( items[i] );
    }
    *count = i;
    return S_OK;
}

static const struct IIterator_IJsonValueVtbl value_iterator_vtbl =
{
    value_iterator_QueryInterface,
    value_iterator_AddRef,
    value_iterator_Release,
    /* IInspectable methods */
    value_iterator_GetIids,
    value_iterator_GetRuntimeClassName,
    value_iterator_GetTrustLevel,
    /* IIterator<IJsonValue *> methods */
    value_iterator_get_Current,
    value_iterator_get_HasCurrent,
    value_iterator_MoveNext,
    value_iterator_GetMany,
};

static struct json_iterator *iterator_create( struct json_items *items )
{
    struct json_iterator *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IIterator_IKeyValuePair_HSTRING_IJsonValue_iface.lpVtbl = &pair_iterator_vtbl;
    impl->IIterator_IJsonValue_iface.lpVtbl = &value_iterator_vtbl;
    impl->ref = 1;
    impl->items = items;
    return impl;
}

/* The members of an object (a map of HSTRING to IJsonValue, stored as
 * IInspectable): an iterator over a snapshot of them */
HRESULT json_object_iterator_create( IMap_HSTRING_IInspectable *members,
                                     IIterator_IKeyValuePair_HSTRING_IJsonValue **out )
{
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable;
    IIterator_IKeyValuePair_HSTRING_IInspectable *iterator;
    struct json_iterator *impl;
    struct json_items *items;
    boolean has_current = FALSE;
    UINT32 size = 0, i = 0;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    if (FAILED(hr = IMap_HSTRING_IInspectable_get_Size( members, &size ))) return hr;
    if (!(items = items_create( size, TRUE ))) return E_OUTOFMEMORY;

    hr = IMap_HSTRING_IInspectable_QueryInterface( members, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                   (void **)&iterable );
    if (SUCCEEDED(hr))
    {
        hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, &iterator );
        IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );
    }
    if (SUCCEEDED(hr))
    {
        hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_HasCurrent( iterator, &has_current );
        while (SUCCEEDED(hr) && has_current && i < size)
        {
            IKeyValuePair_HSTRING_IInspectable *pair;
            IInspectable *inspectable;

            if (FAILED(hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current( iterator, &pair ))) break;
            if (SUCCEEDED(hr = IKeyValuePair_HSTRING_IInspectable_get_Key( pair, &items->keys[i] )) &&
                SUCCEEDED(hr = IKeyValuePair_HSTRING_IInspectable_get_Value( pair, &inspectable )))
            {
                hr = IInspectable_QueryInterface( inspectable, &IID_IJsonValue, (void **)&items->values[i] );
                IInspectable_Release( inspectable );
                if (SUCCEEDED(hr)) items->count = ++i;
                else WindowsDeleteString( items->keys[i] );
            }
            IKeyValuePair_HSTRING_IInspectable_Release( pair );
            if (SUCCEEDED(hr)) hr = IIterator_IKeyValuePair_HSTRING_IInspectable_MoveNext( iterator, &has_current );
        }
        IIterator_IKeyValuePair_HSTRING_IInspectable_Release( iterator );
    }

    if (FAILED(hr) || !(impl = iterator_create( items )))
    {
        items_release( items );
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    *out = &impl->IIterator_IKeyValuePair_HSTRING_IJsonValue_iface;
    return S_OK;
}

/* The elements of an array: an iterator over a snapshot of them */
HRESULT json_array_iterator_create( IJsonValue **elements, UINT32 length, IIterator_IJsonValue **out )
{
    struct json_iterator *impl;
    struct json_items *items;
    UINT32 i;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(items = items_create( length, FALSE ))) return E_OUTOFMEMORY;
    for (i = 0; i < length; i++)
    {
        if ((items->values[i] = elements[i])) IJsonValue_AddRef( elements[i] );
    }
    items->count = length;

    if (!(impl = iterator_create( items )))
    {
        items_release( items );
        return E_OUTOFMEMORY;
    }
    *out = &impl->IIterator_IJsonValue_iface;
    return S_OK;
}
