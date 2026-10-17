/*
 * PROJECT:     ReactOS CoreMessaging
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Windows.System.DispatcherQueue and CreateDispatcherQueueController
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * A dispatcher queue runs callbacks ("work items") on one thread. That thread
 * is either the one that created the queue (DQTYPE_THREAD_CURRENT: the work
 * items then run from its message loop) or a thread of the queue's own
 * (DQTYPE_THREAD_DEDICATED). Both kinds use a message-only window: queuing a
 * work item posts a message to it.
 *
 * Paint.NET creates a queue on its UI thread, then uses
 * DispatcherQueue.GetForCurrentThread, TryEnqueue and HasThreadAccess.
 */

#define COBJMACROS
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "objbase.h"
#include "winstring.h"
#include "olectl.h"

#include "initguid.h"
#include "activation.h"
#include "asyncinfo.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"
#include "dispatcherqueue.h"

#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(coremessaging);

#define WM_DQ_WORK      (WM_USER + 1)
#define WM_DQ_SHUTDOWN  (WM_USER + 2)

static const WCHAR queue_class_name[] = L"ReactOSDispatcherQueue";

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#define DEFINE_IAGILEOBJECT( pfx, impl_type, base_iface )                                          \
    static inline impl_type *pfx##_from_IAgileObject( IAgileObject *iface )                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, IAgileObject_iface );                          \
    }                                                                                              \
    static HRESULT WINAPI pfx##_agile_QueryInterface( IAgileObject *iface, REFIID iid, void **out ) \
    {                                                                                              \
        return IInspectable_QueryInterface( (IInspectable *)&pfx##_from_IAgileObject( iface )->base_iface, iid, out ); \
    }                                                                                              \
    static ULONG WINAPI pfx##_agile_AddRef( IAgileObject *iface )                                  \
    {                                                                                              \
        return IInspectable_AddRef( (IInspectable *)&pfx##_from_IAgileObject( iface )->base_iface ); \
    }                                                                                              \
    static ULONG WINAPI pfx##_agile_Release( IAgileObject *iface )                                 \
    {                                                                                              \
        return IInspectable_Release( (IInspectable *)&pfx##_from_IAgileObject( iface )->base_iface ); \
    }                                                                                              \
    static const IAgileObjectVtbl pfx##_agile_vtbl =                                               \
    {                                                                                              \
        pfx##_agile_QueryInterface,                                                                \
        pfx##_agile_AddRef,                                                                        \
        pfx##_agile_Release,                                                                       \
    };

static HRESULT create_class_name( const WCHAR *name, HSTRING *out )
{
    return WindowsCreateString( name, lstrlenW( name ), out );
}

/* The queue of the current thread: one TLS slot, allocated on first use */
static DWORD tls_index = TLS_OUT_OF_INDEXES;

static DWORD get_tls_index(void)
{
    if (tls_index == TLS_OUT_OF_INDEXES)
    {
        DWORD index = TlsAlloc();
        if (InterlockedCompareExchange( (LONG *)&tls_index, index, TLS_OUT_OF_INDEXES ) != TLS_OUT_OF_INDEXES)
            TlsFree( index );
    }
    return tls_index;
}

/*
 * IAsyncAction of ShutdownQueueAsync
 */

struct async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    IAgileObject IAgileObject_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IAsyncActionCompletedHandler *handler;
    BOOL handler_set;
    AsyncStatus status;
};

static inline struct async_action *impl_from_IAsyncAction( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct async_action, IAsyncAction_iface );
}

static HRESULT WINAPI async_action_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
        *out = &impl->IAsyncAction_iface;
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
        *out = &impl->IAsyncInfo_iface;
    else if (IsEqualGUID( iid, &IID_IAgileObject ))
        *out = &impl->IAgileObject_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI async_action_AddRef( IAsyncAction *iface )
{
    return InterlockedIncrement( &impl_from_IAsyncAction( iface )->ref );
}

static ULONG WINAPI async_action_Release( IAsyncAction *iface )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handler) IAsyncActionCompletedHandler_Release( impl->handler );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_action_GetIids( IAsyncAction *iface, ULONG *iid_count, IID **iids )
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_action_GetRuntimeClassName( IAsyncAction *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Foundation.IAsyncAction", class_name );
}

static HRESULT WINAPI async_action_GetTrustLevel( IAsyncAction *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_action_put_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler *handler )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );
    AsyncStatus status;

    TRACE( "iface %p, handler %p.\n", iface, handler );

    EnterCriticalSection( &impl->cs );
    if (impl->handler_set)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    impl->handler_set = TRUE;
    status = impl->status;
    if (handler && status == Started)
    {
        impl->handler = handler;
        IAsyncActionCompletedHandler_AddRef( handler );
    }
    LeaveCriticalSection( &impl->cs );

    /* Already done: called at once */
    if (handler && status != Started)
        IAsyncActionCompletedHandler_Invoke( handler, iface, status );
    return S_OK;
}

static HRESULT WINAPI async_action_get_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler **handler )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );

    EnterCriticalSection( &impl->cs );
    if ((*handler = impl->handler)) IAsyncActionCompletedHandler_AddRef( *handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI async_action_GetResults( IAsyncAction *iface )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );
    return impl->status == Completed ? S_OK : E_ILLEGAL_METHOD_CALL;
}

static const IAsyncActionVtbl async_action_vtbl =
{
    async_action_QueryInterface,
    async_action_AddRef,
    async_action_Release,
    async_action_GetIids,
    async_action_GetRuntimeClassName,
    async_action_GetTrustLevel,
    async_action_put_Completed,
    async_action_get_Completed,
    async_action_GetResults,
};

DEFINE_IINSPECTABLE( async_info, IAsyncInfo, struct async_action, IAsyncAction_iface )

static HRESULT WINAPI async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    *status = impl_from_IAsyncInfo( iface )->status;
    return S_OK;
}

static HRESULT WINAPI async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error_code )
{
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface,
    async_info_AddRef,
    async_info_Release,
    async_info_GetIids,
    async_info_GetRuntimeClassName,
    async_info_GetTrustLevel,
    async_info_get_Id,
    async_info_get_Status,
    async_info_get_ErrorCode,
    async_info_Cancel,
    async_info_Close,
};

DEFINE_IAGILEOBJECT( async_action, struct async_action, IAsyncAction_iface )

static struct async_action *async_action_create(void)
{
    struct async_action *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IAsyncAction_iface.lpVtbl = &async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_info_vtbl;
    impl->IAgileObject_iface.lpVtbl = &async_action_agile_vtbl;
    impl->ref = 1;
    impl->status = Started;
    InitializeCriticalSection( &impl->cs );
    return impl;
}

static void async_action_complete( struct async_action *impl )
{
    IAsyncActionCompletedHandler *handler;

    EnterCriticalSection( &impl->cs );
    impl->status = Completed;
    handler = impl->handler;
    impl->handler = NULL;
    LeaveCriticalSection( &impl->cs );

    if (handler)
    {
        IAsyncActionCompletedHandler_Invoke( handler, &impl->IAsyncAction_iface, Completed );
        IAsyncActionCompletedHandler_Release( handler );
    }
}

/*
 * DispatcherQueue
 */

struct work_item
{
    struct list entry;
    IDispatcherQueueHandler *handler;
    INT32 priority;
};

struct event_handler
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

struct dispatcher_queue
{
    IDispatcherQueue IDispatcherQueue_iface;
    IDispatcherQueue2 IDispatcherQueue2_iface;
    IAgileObject IAgileObject_iface;
    LONG ref;

    CRITICAL_SECTION cs;
    struct list items;
    struct list starting_handlers;
    struct list completed_handlers;
    INT64 next_token;
    HWND hwnd;
    DWORD thread_id;
    BOOL dedicated;
    BOOL shutting_down;     /* no new work items */
    BOOL shut_down;
};

static inline struct dispatcher_queue *impl_from_IDispatcherQueue( IDispatcherQueue *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue, IDispatcherQueue_iface );
}

static HRESULT WINAPI dispatcher_queue_QueryInterface( IDispatcherQueue *iface, REFIID iid, void **out )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IDispatcherQueue ))
        *out = &impl->IDispatcherQueue_iface;
    else if (IsEqualGUID( iid, &IID_IDispatcherQueue2 ))
        *out = &impl->IDispatcherQueue2_iface;
    else if (IsEqualGUID( iid, &IID_IAgileObject ))
        *out = &impl->IAgileObject_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI dispatcher_queue_AddRef( IDispatcherQueue *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static void free_event_handlers( struct list *list )
{
    struct event_handler *handler, *next;

    LIST_FOR_EACH_ENTRY_SAFE( handler, next, list, struct event_handler, entry )
    {
        list_remove( &handler->entry );
        IUnknown_Release( handler->handler );
        free( handler );
    }
}

static ULONG WINAPI dispatcher_queue_Release( IDispatcherQueue *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    struct work_item *item, *next;

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        /* (the window holds a reference until the queue is shut down) */
        LIST_FOR_EACH_ENTRY_SAFE( item, next, &impl->items, struct work_item, entry )
        {
            list_remove( &item->entry );
            IDispatcherQueueHandler_Release( item->handler );
            free( item );
        }
        free_event_handlers( &impl->starting_handlers );
        free_event_handlers( &impl->completed_handlers );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI dispatcher_queue_GetIids( IDispatcherQueue *iface, ULONG *iid_count, IID **iids )
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI dispatcher_queue_GetRuntimeClassName( IDispatcherQueue *iface, HSTRING *class_name )
{
    return create_class_name( RuntimeClass_Windows_System_DispatcherQueue, class_name );
}

static HRESULT WINAPI dispatcher_queue_GetTrustLevel( IDispatcherQueue *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dispatcher_queue_CreateTimer( IDispatcherQueue *iface, IDispatcherQueueTimer **result )
{
    FIXME( "iface %p, result %p stub!\n", iface, result );
    *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_queue_TryEnqueueWithPriority( IDispatcherQueue *iface, DispatcherQueuePriority priority,
                                                               IDispatcherQueueHandler *callback, boolean *result )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    struct work_item *item;

    TRACE( "iface %p, priority %d, callback %p, result %p.\n", iface, priority, callback, result );

    if (!callback || !result) return E_POINTER;
    *result = FALSE;
    if (!(item = calloc( 1, sizeof(*item) ))) return E_OUTOFMEMORY;
    item->handler = callback;
    item->priority = priority;
    IDispatcherQueueHandler_AddRef( callback );

    EnterCriticalSection( &impl->cs );
    if (!impl->shutting_down && PostMessageW( impl->hwnd, WM_DQ_WORK, 0, 0 ))
    {
        list_add_tail( &impl->items, &item->entry );
        item = NULL;
        *result = TRUE;
    }
    LeaveCriticalSection( &impl->cs );

    if (item)
    {
        IDispatcherQueueHandler_Release( callback );
        free( item );
    }
    return S_OK;
}

static HRESULT WINAPI dispatcher_queue_TryEnqueue( IDispatcherQueue *iface, IDispatcherQueueHandler *callback, boolean *result )
{
    return dispatcher_queue_TryEnqueueWithPriority( iface, DispatcherQueuePriority_Normal, callback, result );
}

static HRESULT add_event_handler( struct dispatcher_queue *impl, struct list *list, IUnknown *handler,
                                  EventRegistrationToken *token )
{
    struct event_handler *entry;

    if (!handler || !token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = handler;
    IUnknown_AddRef( handler );
    EnterCriticalSection( &impl->cs );
    entry->token.value = ++impl->next_token;
    list_add_tail( list, &entry->entry );
    LeaveCriticalSection( &impl->cs );
    *token = entry->token;
    return S_OK;
}

static HRESULT remove_event_handler( struct dispatcher_queue *impl, struct list *list, EventRegistrationToken token )
{
    struct event_handler *entry, *found = NULL;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY( entry, list, struct event_handler, entry )
    {
        if (entry->token.value == token.value)
        {
            list_remove( &entry->entry );
            found = entry;
            break;
        }
    }
    LeaveCriticalSection( &impl->cs );

    if (found)
    {
        IUnknown_Release( found->handler );
        free( found );
    }
    return S_OK;
}

static HRESULT WINAPI dispatcher_queue_add_ShutdownStarting( IDispatcherQueue *iface,
        ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs *handler, EventRegistrationToken *token )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return add_event_handler( impl, &impl->starting_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI dispatcher_queue_remove_ShutdownStarting( IDispatcherQueue *iface, EventRegistrationToken token )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    return remove_event_handler( impl, &impl->starting_handlers, token );
}

static HRESULT WINAPI dispatcher_queue_add_ShutdownCompleted( IDispatcherQueue *iface,
        ITypedEventHandler_DispatcherQueue_IInspectable *handler, EventRegistrationToken *token )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return add_event_handler( impl, &impl->completed_handlers, (IUnknown *)handler, token );
}

static HRESULT WINAPI dispatcher_queue_remove_ShutdownCompleted( IDispatcherQueue *iface, EventRegistrationToken token )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    return remove_event_handler( impl, &impl->completed_handlers, token );
}

static const IDispatcherQueueVtbl dispatcher_queue_vtbl =
{
    dispatcher_queue_QueryInterface,
    dispatcher_queue_AddRef,
    dispatcher_queue_Release,
    dispatcher_queue_GetIids,
    dispatcher_queue_GetRuntimeClassName,
    dispatcher_queue_GetTrustLevel,
    dispatcher_queue_CreateTimer,
    dispatcher_queue_TryEnqueue,
    dispatcher_queue_TryEnqueueWithPriority,
    dispatcher_queue_add_ShutdownStarting,
    dispatcher_queue_remove_ShutdownStarting,
    dispatcher_queue_add_ShutdownCompleted,
    dispatcher_queue_remove_ShutdownCompleted,
};

DEFINE_IINSPECTABLE( dispatcher_queue2, IDispatcherQueue2, struct dispatcher_queue, IDispatcherQueue_iface )

static HRESULT WINAPI dispatcher_queue2_get_HasThreadAccess( IDispatcherQueue2 *iface, boolean *value )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2( iface );

    if (!value) return E_POINTER;
    *value = impl->thread_id == GetCurrentThreadId() && !impl->shut_down;
    return S_OK;
}

static const IDispatcherQueue2Vtbl dispatcher_queue2_vtbl =
{
    dispatcher_queue2_QueryInterface,
    dispatcher_queue2_AddRef,
    dispatcher_queue2_Release,
    dispatcher_queue2_GetIids,
    dispatcher_queue2_GetRuntimeClassName,
    dispatcher_queue2_GetTrustLevel,
    dispatcher_queue2_get_HasThreadAccess,
};

DEFINE_IAGILEOBJECT( dispatcher_queue, struct dispatcher_queue, IDispatcherQueue_iface )

/* Runs the work item of the highest priority (the oldest one of that
 * priority). Each queued item posted one message, so all of them run. */
static void run_one_item( struct dispatcher_queue *impl )
{
    struct work_item *item, *best = NULL;
    HRESULT hr;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY( item, &impl->items, struct work_item, entry )
        if (!best || item->priority > best->priority) best = item;
    if (best) list_remove( &best->entry );
    LeaveCriticalSection( &impl->cs );

    if (!best) return;
    TRACE( "running %p (priority %d)\n", best->handler, best->priority );
    hr = IDispatcherQueueHandler_Invoke( best->handler );
    if (FAILED(hr)) WARN( "work item %p failed, hr %#lx\n", best->handler, hr );
    IDispatcherQueueHandler_Release( best->handler );
    free( best );
}

/* On the queue's thread: runs what is left, raises ShutdownCompleted and
 * destroys the window (which drops its reference) */
static void shutdown_queue( struct dispatcher_queue *impl, struct async_action *action )
{
    struct event_handler *entry;
    struct list handlers = LIST_INIT( handlers );
    BOOL empty;

    TRACE( "queue %p, action %p\n", impl, action );

    if (!impl->shut_down)
    {
        EnterCriticalSection( &impl->cs );
        impl->shutting_down = TRUE;
        LeaveCriticalSection( &impl->cs );

        if (!list_empty( &impl->starting_handlers ))
            FIXME( "ShutdownStarting handlers are not called\n" );

        for (;;)
        {
            EnterCriticalSection( &impl->cs );
            empty = list_empty( &impl->items );
            LeaveCriticalSection( &impl->cs );
            if (empty) break;
            run_one_item( impl );
        }

        impl->shut_down = TRUE;
        if (TlsGetValue( get_tls_index() ) == impl)
            TlsSetValue( get_tls_index(), NULL );

        EnterCriticalSection( &impl->cs );
        list_move_tail( &handlers, &impl->completed_handlers );
        LeaveCriticalSection( &impl->cs );
        LIST_FOR_EACH_ENTRY( entry, &handlers, struct event_handler, entry )
            ITypedEventHandler_DispatcherQueue_IInspectable_Invoke( (ITypedEventHandler_DispatcherQueue_IInspectable *)entry->handler,
                                                                    &impl->IDispatcherQueue_iface, NULL );
        free_event_handlers( &handlers );

        SetWindowLongPtrW( impl->hwnd, GWLP_USERDATA, 0 );
        DestroyWindow( impl->hwnd );
        if (impl->dedicated) PostQuitMessage( 0 );
        IDispatcherQueue_Release( &impl->IDispatcherQueue_iface );   /* the window's reference */
    }

    if (action)
    {
        async_action_complete( action );
        IAsyncAction_Release( &action->IAsyncAction_iface );
    }
}

static LRESULT CALLBACK queue_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct dispatcher_queue *impl = (struct dispatcher_queue *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    switch (msg)
    {
    case WM_DQ_WORK:
        if (impl)
        {
            /* (a work item may shut the queue down: keep it alive) */
            IDispatcherQueue_AddRef( &impl->IDispatcherQueue_iface );
            run_one_item( impl );
            IDispatcherQueue_Release( &impl->IDispatcherQueue_iface );
        }
        return 0;
    case WM_DQ_SHUTDOWN:
        if (impl)
        {
            IDispatcherQueue_AddRef( &impl->IDispatcherQueue_iface );
            shutdown_queue( impl, (struct async_action *)lparam );
            IDispatcherQueue_Release( &impl->IDispatcherQueue_iface );
        }
        else if (lparam)
        {
            async_action_complete( (struct async_action *)lparam );
            IAsyncAction_Release( &((struct async_action *)lparam)->IAsyncAction_iface );
        }
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static HINSTANCE get_module(void)
{
    HMODULE module = NULL;
    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (const WCHAR *)queue_wndproc, &module );
    return module;
}

static void register_queue_class(void)
{
    static LONG registered;
    WNDCLASSEXW wc = { sizeof(wc) };

    if (registered) return;
    wc.lpfnWndProc = queue_wndproc;
    wc.hInstance = get_module();
    wc.lpszClassName = queue_class_name;
    if (RegisterClassExW( &wc ) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        registered = 1;
}

/* Creates a queue on the current thread */
static HRESULT dispatcher_queue_create( BOOL dedicated, struct dispatcher_queue **out )
{
    struct dispatcher_queue *impl;

    *out = NULL;
    if (get_tls_index() == TLS_OUT_OF_INDEXES) return E_OUTOFMEMORY;
    if (TlsGetValue( tls_index )) return HRESULT_FROM_WIN32( ERROR_ALREADY_EXISTS );

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IDispatcherQueue_iface.lpVtbl = &dispatcher_queue_vtbl;
    impl->IDispatcherQueue2_iface.lpVtbl = &dispatcher_queue2_vtbl;
    impl->IAgileObject_iface.lpVtbl = &dispatcher_queue_agile_vtbl;
    impl->ref = 2;   /* the caller's and the window's */
    InitializeCriticalSection( &impl->cs );
    list_init( &impl->items );
    list_init( &impl->starting_handlers );
    list_init( &impl->completed_handlers );
    impl->thread_id = GetCurrentThreadId();
    impl->dedicated = dedicated;

    register_queue_class();
    impl->hwnd = CreateWindowExW( 0, queue_class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, get_module(), NULL );
    if (!impl->hwnd)
    {
        HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
        ERR( "no window, error %lu\n", GetLastError() );
        DeleteCriticalSection( &impl->cs );
        free( impl );
        return FAILED(hr) ? hr : E_FAIL;
    }
    SetWindowLongPtrW( impl->hwnd, GWLP_USERDATA, (LONG_PTR)impl );
    TlsSetValue( tls_index, impl );

    TRACE( "queue %p, thread %#lx, dedicated %d\n", impl, impl->thread_id, dedicated );
    *out = impl;
    return S_OK;
}

/*
 * DispatcherQueueController
 */

struct dispatcher_queue_controller
{
    IDispatcherQueueController IDispatcherQueueController_iface;
    IAgileObject IAgileObject_iface;
    LONG ref;
    struct dispatcher_queue *queue;
    HANDLE thread;
};

static inline struct dispatcher_queue_controller *impl_from_IDispatcherQueueController( IDispatcherQueueController *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_controller, IDispatcherQueueController_iface );
}

static HRESULT WINAPI controller_QueryInterface( IDispatcherQueueController *iface, REFIID iid, void **out )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IDispatcherQueueController ))
        *out = &impl->IDispatcherQueueController_iface;
    else if (IsEqualGUID( iid, &IID_IAgileObject ))
        *out = &impl->IAgileObject_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI controller_AddRef( IDispatcherQueueController *iface )
{
    return InterlockedIncrement( &impl_from_IDispatcherQueueController( iface )->ref );
}

static ULONG WINAPI controller_Release( IDispatcherQueueController *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        /* A dedicated thread has nobody left to give it work: stop it */
        if (impl->thread)
        {
            EnterCriticalSection( &impl->queue->cs );
            if (!impl->queue->shutting_down)
            {
                impl->queue->shutting_down = TRUE;
                PostMessageW( impl->queue->hwnd, WM_DQ_SHUTDOWN, 0, 0 );
            }
            LeaveCriticalSection( &impl->queue->cs );
            CloseHandle( impl->thread );
        }
        IDispatcherQueue_Release( &impl->queue->IDispatcherQueue_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI controller_GetIids( IDispatcherQueueController *iface, ULONG *iid_count, IID **iids )
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI controller_GetRuntimeClassName( IDispatcherQueueController *iface, HSTRING *class_name )
{
    return create_class_name( RuntimeClass_Windows_System_DispatcherQueueController, class_name );
}

static HRESULT WINAPI controller_GetTrustLevel( IDispatcherQueueController *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI controller_get_DispatcherQueue( IDispatcherQueueController *iface, IDispatcherQueue **value )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = &impl->queue->IDispatcherQueue_iface;
    IDispatcherQueue_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI controller_ShutdownQueueAsync( IDispatcherQueueController *iface, IAsyncAction **operation )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );
    struct dispatcher_queue *queue = impl->queue;
    struct async_action *action;
    BOOL posted = FALSE;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    if (!(action = async_action_create())) return E_OUTOFMEMORY;

    /* The window gets its own reference to the action */
    IAsyncAction_AddRef( &action->IAsyncAction_iface );
    EnterCriticalSection( &queue->cs );
    if (!queue->shutting_down)
    {
        queue->shutting_down = TRUE;
        posted = PostMessageW( queue->hwnd, WM_DQ_SHUTDOWN, 0, (LPARAM)action );
    }
    LeaveCriticalSection( &queue->cs );

    if (!posted)
    {
        /* already shut down (or shutting down): done */
        async_action_complete( action );
        IAsyncAction_Release( &action->IAsyncAction_iface );
    }
    *operation = &action->IAsyncAction_iface;
    return S_OK;
}

static const IDispatcherQueueControllerVtbl controller_vtbl =
{
    controller_QueryInterface,
    controller_AddRef,
    controller_Release,
    controller_GetIids,
    controller_GetRuntimeClassName,
    controller_GetTrustLevel,
    controller_get_DispatcherQueue,
    controller_ShutdownQueueAsync,
};

DEFINE_IAGILEOBJECT( controller, struct dispatcher_queue_controller, IDispatcherQueueController_iface )

static HRESULT controller_create( struct dispatcher_queue *queue, HANDLE thread, IDispatcherQueueController **out )
{
    struct dispatcher_queue_controller *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IDispatcherQueueController_iface.lpVtbl = &controller_vtbl;
    impl->IAgileObject_iface.lpVtbl = &controller_agile_vtbl;
    impl->ref = 1;
    impl->queue = queue;   /* takes the caller's reference */
    impl->thread = thread;
    *out = &impl->IDispatcherQueueController_iface;
    return S_OK;
}

struct thread_params
{
    DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment;
    HANDLE ready;
    struct dispatcher_queue *queue;
    HRESULT hr;
};

static DWORD WINAPI dedicated_thread( void *arg )
{
    struct thread_params *params = arg;
    BOOL com = FALSE;
    MSG msg;

    if (params->apartment != DQTAT_COM_NONE)
        com = SUCCEEDED(CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ));

    params->hr = dispatcher_queue_create( TRUE, &params->queue );
    SetEvent( params->ready );   /* (params is gone after this) */

    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }

    if (com) CoUninitialize();
    TRACE( "thread %#lx done\n", GetCurrentThreadId() );
    return 0;
}

static HRESULT create_controller( DISPATCHERQUEUE_THREAD_TYPE type, DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment,
                                  IDispatcherQueueController **out )
{
    struct dispatcher_queue *queue;
    HRESULT hr;

    *out = NULL;

    if (type == DQTYPE_THREAD_CURRENT)
    {
        /* (Paint.NET asks for an STA here; its UI thread already is one) */
        if (apartment != DQTAT_COM_NONE)
            CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
        if (FAILED(hr = dispatcher_queue_create( FALSE, &queue ))) return hr;
        if (FAILED(hr = controller_create( queue, NULL, out )))
        {
            EnterCriticalSection( &queue->cs );
            queue->shutting_down = TRUE;
            PostMessageW( queue->hwnd, WM_DQ_SHUTDOWN, 0, 0 );
            LeaveCriticalSection( &queue->cs );
            IDispatcherQueue_Release( &queue->IDispatcherQueue_iface );
        }
        return hr;
    }
    else if (type == DQTYPE_THREAD_DEDICATED)
    {
        struct thread_params params = { apartment };
        HANDLE thread;

        if (!(params.ready = CreateEventW( NULL, TRUE, FALSE, NULL ))) return HRESULT_FROM_WIN32( GetLastError() );
        if (!(thread = CreateThread( NULL, 0, dedicated_thread, &params, 0, NULL )))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            CloseHandle( params.ready );
            return hr;
        }
        WaitForSingleObject( params.ready, INFINITE );
        CloseHandle( params.ready );
        if (FAILED(params.hr))
        {
            WaitForSingleObject( thread, INFINITE );
            CloseHandle( thread );
            return params.hr;
        }
        queue = params.queue;
        if (FAILED(hr = controller_create( queue, thread, out )))
        {
            EnterCriticalSection( &queue->cs );
            queue->shutting_down = TRUE;
            PostMessageW( queue->hwnd, WM_DQ_SHUTDOWN, 0, 0 );
            LeaveCriticalSection( &queue->cs );
            IDispatcherQueue_Release( &queue->IDispatcherQueue_iface );
            CloseHandle( thread );
        }
        return hr;
    }
    return E_INVALIDARG;
}

/***********************************************************************
 *      CreateDispatcherQueueController (coremessaging.@)
 */
HRESULT WINAPI CreateDispatcherQueueController( DispatcherQueueOptions options, PDISPATCHERQUEUECONTROLLER *controller )
{
    TRACE( "size %lu, thread type %d, apartment %d, controller %p\n", options.dwSize, options.threadType,
           options.apartmentType, controller );

    if (!controller) return E_POINTER;
    *controller = NULL;
    if (options.dwSize != sizeof(options)) return E_INVALIDARG;
    if (options.apartmentType > DQTAT_COM_STA) return E_INVALIDARG;
    return create_controller( options.threadType, options.apartmentType, controller );
}

/*
 * Activation factories (statics)
 */

struct queue_statics
{
    IActivationFactory IActivationFactory_iface;
    IDispatcherQueueStatics IDispatcherQueueStatics_iface;
    LONG ref;
};

static inline struct queue_statics *impl_from_queue_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct queue_statics, IActivationFactory_iface );
}

static HRESULT WINAPI queue_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct queue_statics *impl = impl_from_queue_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IDispatcherQueueStatics ))
        *out = &impl->IDispatcherQueueStatics_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI queue_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_queue_factory( iface )->ref );
}

static ULONG WINAPI queue_factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_queue_factory( iface )->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI queue_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return create_class_name( RuntimeClass_Windows_System_DispatcherQueue, class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    /* (neither class can be created with "new") */
    *instance = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl queue_factory_vtbl =
{
    queue_factory_QueryInterface,
    queue_factory_AddRef,
    queue_factory_Release,
    factory_GetIids,
    queue_factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( queue_statics, IDispatcherQueueStatics, struct queue_statics, IActivationFactory_iface )

static HRESULT WINAPI queue_statics_GetForCurrentThread( IDispatcherQueueStatics *iface, IDispatcherQueue **result )
{
    struct dispatcher_queue *queue = NULL;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (get_tls_index() != TLS_OUT_OF_INDEXES) queue = TlsGetValue( tls_index );
    if (queue)
    {
        *result = &queue->IDispatcherQueue_iface;
        IDispatcherQueue_AddRef( *result );
    }
    else *result = NULL;
    return S_OK;
}

static const IDispatcherQueueStaticsVtbl queue_statics_vtbl =
{
    queue_statics_QueryInterface,
    queue_statics_AddRef,
    queue_statics_Release,
    queue_statics_GetIids,
    queue_statics_GetRuntimeClassName,
    queue_statics_GetTrustLevel,
    queue_statics_GetForCurrentThread,
};

static struct queue_statics queue_statics =
{
    {&queue_factory_vtbl},
    {&queue_statics_vtbl},
    1,
};

struct controller_statics
{
    IActivationFactory IActivationFactory_iface;
    IDispatcherQueueControllerStatics IDispatcherQueueControllerStatics_iface;
    LONG ref;
};

static inline struct controller_statics *impl_from_controller_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct controller_statics, IActivationFactory_iface );
}

static HRESULT WINAPI controller_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct controller_statics *impl = impl_from_controller_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IDispatcherQueueControllerStatics ))
        *out = &impl->IDispatcherQueueControllerStatics_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI controller_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_controller_factory( iface )->ref );
}

static ULONG WINAPI controller_factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_controller_factory( iface )->ref );
}

static HRESULT WINAPI controller_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return create_class_name( RuntimeClass_Windows_System_DispatcherQueueController, class_name );
}

static const IActivationFactoryVtbl controller_factory_vtbl =
{
    controller_factory_QueryInterface,
    controller_factory_AddRef,
    controller_factory_Release,
    factory_GetIids,
    controller_factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( controller_statics, IDispatcherQueueControllerStatics, struct controller_statics, IActivationFactory_iface )

static HRESULT WINAPI controller_statics_CreateOnDedicatedThread( IDispatcherQueueControllerStatics *iface,
                                                                  IDispatcherQueueController **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    return create_controller( DQTYPE_THREAD_DEDICATED, DQTAT_COM_STA, result );
}

static const IDispatcherQueueControllerStaticsVtbl controller_statics_vtbl =
{
    controller_statics_QueryInterface,
    controller_statics_AddRef,
    controller_statics_Release,
    controller_statics_GetIids,
    controller_statics_GetRuntimeClassName,
    controller_statics_GetTrustLevel,
    controller_statics_CreateOnDedicatedThread,
};

static struct controller_statics controller_statics =
{
    {&controller_factory_vtbl},
    {&controller_statics_vtbl},
    1,
};

/***********************************************************************
 *      DllGetActivationFactory (coremessaging.@)
 */
HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_w( name ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;
    if (!name) return CLASS_E_CLASSNOTAVAILABLE;

    if (!wcscmp( name, RuntimeClass_Windows_System_DispatcherQueue ))
        *factory = &queue_statics.IActivationFactory_iface;
    else if (!wcscmp( name, RuntimeClass_Windows_System_DispatcherQueueController ))
        *factory = &controller_statics.IActivationFactory_iface;
    else
        return CLASS_E_CLASSNOTAVAILABLE;

    IActivationFactory_AddRef( *factory );
    return S_OK;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    /* (dedicated threads run code of this DLL) */
    return S_FALSE;
}

static const WCHAR *const class_names[] =
{
    RuntimeClass_Windows_System_DispatcherQueue,
    RuntimeClass_Windows_System_DispatcherQueueController,
};

#define CLASS_ROOT L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\"

/* The registration that RoGetActivationFactory looks up */
HRESULT WINAPI DllRegisterServer(void)
{
    static const WCHAR path[] = L"%SystemRoot%\\system32\\CoreMessaging.dll";
    WCHAR key_name[MAX_PATH];
    DWORD zero = 0;
    unsigned int i;
    HKEY key;

    for (i = 0; i < ARRAY_SIZE(class_names); i++)
    {
        lstrcpyW( key_name, CLASS_ROOT );
        lstrcatW( key_name, class_names[i] );
        if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL ))
            return SELFREG_E_CLASS;
        RegSetValueExW( key, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)path, sizeof(path) );
        RegSetValueExW( key, L"ActivationType", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero) ); /* in process */
        RegSetValueExW( key, L"Threading", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero) );      /* both */
        RegSetValueExW( key, L"TrustLevel", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero) );     /* base */
        RegCloseKey( key );
    }
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR key_name[MAX_PATH];
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(class_names); i++)
    {
        lstrcpyW( key_name, CLASS_ROOT );
        lstrcatW( key_name, class_names[i] );
        RegDeleteKeyW( HKEY_LOCAL_MACHINE, key_name );
    }
    return S_OK;
}
