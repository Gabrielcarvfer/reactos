/*
 * PROJECT:     ReactOS AppX deployment client
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Compatibility stubs of Windows.Management.Deployment
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * ReactOS cannot install or run MSIX/AppX packages. Installers that add an
 * optional package along with their classic files (PowerToys' setup adds its
 * Command Palette and a sparse identity package) wait for the deployment
 * with IAsyncOperationWithProgress::get() and treat any failure as fatal: a
 * C++/WinRT hresult_error that the installer does not catch ends the whole
 * MSI custom action process.
 *
 * COMPATIBILITY STUB: the deployment calls (add, stage, register, update)
 * return an operation that has already completed successfully, with a
 * DeploymentResult whose ExtendedErrorCode is S_OK and IsRegistered FALSE.
 * Nothing is installed: each call prints a FIXME with the package URI.
 * The FindPackages* calls return empty lists, so whoever checks whether a
 * package is installed sees the truth.
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

/* DeploymentResult ********************************************************/

struct deployment_result
{
    IDeploymentResult IDeploymentResult_iface;
    IDeploymentResult2 IDeploymentResult2_iface;
    LONG ref;
};

static inline struct deployment_result *impl_from_IDeploymentResult( IDeploymentResult *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_result, IDeploymentResult_iface );
}

static HRESULT WINAPI deployment_result_QueryInterface( IDeploymentResult *iface, REFIID iid, void **out )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDeploymentResult ))
    {
        *out = &impl->IDeploymentResult_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDeploymentResult2 ))
    {
        *out = &impl->IDeploymentResult2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI deployment_result_AddRef( IDeploymentResult *iface )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI deployment_result_Release( IDeploymentResult *iface )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI deployment_result_GetIids( IDeploymentResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI deployment_result_GetRuntimeClassName( IDeploymentResult *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI deployment_result_GetTrustLevel( IDeploymentResult *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI deployment_result_get_ErrorText( IDeploymentResult *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI deployment_result_get_ActivityId( IDeploymentResult *iface, GUID *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = GUID_NULL;
    return S_OK;
}

static HRESULT WINAPI deployment_result_get_ExtendedErrorCode( IDeploymentResult *iface, HRESULT *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = S_OK;
    return S_OK;
}

static const struct IDeploymentResultVtbl deployment_result_vtbl =
{
    deployment_result_QueryInterface,
    deployment_result_AddRef,
    deployment_result_Release,
    /* IInspectable methods */
    deployment_result_GetIids,
    deployment_result_GetRuntimeClassName,
    deployment_result_GetTrustLevel,
    /* IDeploymentResult methods */
    deployment_result_get_ErrorText,
    deployment_result_get_ActivityId,
    deployment_result_get_ExtendedErrorCode,
};

DEFINE_IINSPECTABLE( deployment_result2, IDeploymentResult2, struct deployment_result, IDeploymentResult_iface )

static HRESULT WINAPI deployment_result2_get_IsRegistered( IDeploymentResult2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    /* (nothing was registered) */
    *value = FALSE;
    return S_OK;
}

static const struct IDeploymentResult2Vtbl deployment_result2_vtbl =
{
    deployment_result2_QueryInterface,
    deployment_result2_AddRef,
    deployment_result2_Release,
    /* IInspectable methods */
    deployment_result2_GetIids,
    deployment_result2_GetRuntimeClassName,
    deployment_result2_GetTrustLevel,
    /* IDeploymentResult2 methods */
    deployment_result2_get_IsRegistered,
};

/* The operation (already completed) ***************************************/

struct deployment_operation
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IDeploymentResult *result;
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *completed;
    IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *progress;
    BOOL closed;
};

static inline struct deployment_operation *impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress(
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_operation, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface );
}

static HRESULT WINAPI operation_QueryInterface( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                                REFIID iid, void **out )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress ))
    {
        *out = &impl->IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI operation_AddRef( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI operation_Release( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->completed) IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Release( impl->completed );
        if (impl->progress) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
        IDeploymentResult_Release( impl->result );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI operation_GetIids( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                         ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI operation_GetRuntimeClassName( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                                     HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI operation_GetTrustLevel( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                               TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI operation_put_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                              IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if (handler) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
    if (impl->progress) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
    impl->progress = handler;
    return S_OK;
}

static HRESULT WINAPI operation_get_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                              IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (!handler) return E_POINTER;
    if ((*handler = impl->progress)) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI operation_put_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                               IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if (impl->completed) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    if (!handler) return S_OK;

    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
    impl->completed = handler;

    /* The operation is already complete: the handler runs now */
    return IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Invoke( handler, iface, Completed );
}

static HRESULT WINAPI operation_get_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                               IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (!handler) return E_POINTER;
    if ((*handler = impl->completed)) IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI operation_GetResults( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
                                            IDeploymentResult **results )
{
    struct deployment_operation *impl = impl_from_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress( iface );

    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    *results = impl->result;
    IDeploymentResult_AddRef( *results );
    return S_OK;
}

static const struct IAsyncOperationWithProgress_DeploymentResult_DeploymentProgressVtbl operation_vtbl =
{
    operation_QueryInterface,
    operation_AddRef,
    operation_Release,
    /* IInspectable methods */
    operation_GetIids,
    operation_GetRuntimeClassName,
    operation_GetTrustLevel,
    /* IAsyncOperationWithProgress<DeploymentResult *, DeploymentProgress> methods */
    operation_put_Progress,
    operation_get_Progress,
    operation_put_Completed,
    operation_get_Completed,
    operation_GetResults,
};

DEFINE_IINSPECTABLE( operation_info, IAsyncInfo, struct deployment_operation,
                     IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface )

static HRESULT WINAPI operation_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    TRACE( "iface %p, id %p.\n", iface, id );
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI operation_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    TRACE( "iface %p, status %p.\n", iface, status );
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI operation_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error_code )
{
    TRACE( "iface %p, error_code %p.\n", iface, error_code );
    if (!error_code) return E_POINTER;
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI operation_info_Cancel( IAsyncInfo *iface )
{
    TRACE( "iface %p.\n", iface );
    /* (too late, it is complete) */
    return S_OK;
}

static HRESULT WINAPI operation_info_Close( IAsyncInfo *iface )
{
    struct deployment_operation *impl = impl_from_IAsyncInfo( iface );

    TRACE( "iface %p.\n", iface );

    impl->closed = TRUE;
    return S_OK;
}

static const struct IAsyncInfoVtbl operation_info_vtbl =
{
    operation_info_QueryInterface,
    operation_info_AddRef,
    operation_info_Release,
    /* IInspectable methods */
    operation_info_GetIids,
    operation_info_GetRuntimeClassName,
    operation_info_GetTrustLevel,
    /* IAsyncInfo */
    operation_info_get_Id,
    operation_info_get_Status,
    operation_info_get_ErrorCode,
    operation_info_Cancel,
    operation_info_Close,
};

/* COMPATIBILITY STUB: report the deployment of what (a package at uri, or
 * named name) as done, without doing anything */
HRESULT deployment_stub_completed( const char *what, IUriRuntimeClass *uri, HSTRING name,
                                   IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    struct deployment_operation *impl;
    struct deployment_result *result;
    HSTRING text = NULL;

    if (uri) IUriRuntimeClass_get_RawUri( uri, &text );
    FIXME( "%s %s: ReactOS does not install MSIX/AppX packages, reporting success without installing it\n",
           what, debugstr_hstring( uri ? text : name ) );
    WindowsDeleteString( text );

    if (!operation) return E_POINTER;
    *operation = NULL;

    if (!(result = calloc( 1, sizeof(*result) ))) return E_OUTOFMEMORY;
    result->IDeploymentResult_iface.lpVtbl = &deployment_result_vtbl;
    result->IDeploymentResult2_iface.lpVtbl = &deployment_result2_vtbl;
    result->ref = 1;

    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        free( result );
        return E_OUTOFMEMORY;
    }
    impl->IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface.lpVtbl = &operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &operation_info_vtbl;
    impl->ref = 1;
    impl->result = &result->IDeploymentResult_iface;

    *operation = &impl->IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_iface;
    return S_OK;
}

/* Empty lists of packages and of users *************************************/

struct empty_iterable
{
    IIterable_Package IIterable_Package_iface;
    IIterator_Package IIterator_Package_iface;
    const IID *iterable_iid, *iterator_iid;
    LONG ref;
};

static inline struct empty_iterable *impl_from_IIterable_Package( IIterable_Package *iface )
{
    return CONTAINING_RECORD( iface, struct empty_iterable, IIterable_Package_iface );
}

static HRESULT WINAPI empty_iterable_QueryInterface( IIterable_Package *iface, REFIID iid, void **out )
{
    struct empty_iterable *impl = impl_from_IIterable_Package( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iterable_iid ))
    {
        *out = &impl->IIterable_Package_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI empty_iterable_AddRef( IIterable_Package *iface )
{
    struct empty_iterable *impl = impl_from_IIterable_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI empty_iterable_Release( IIterable_Package *iface )
{
    struct empty_iterable *impl = impl_from_IIterable_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI empty_iterable_GetIids( IIterable_Package *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterable_GetRuntimeClassName( IIterable_Package *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterable_GetTrustLevel( IIterable_Package *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterable_First( IIterable_Package *iface, IIterator_Package **value )
{
    struct empty_iterable *impl = impl_from_IIterable_Package( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    /* (the iterator has no state: it lives in the iterable) */
    *value = &impl->IIterator_Package_iface;
    IIterator_Package_AddRef( *value );
    return S_OK;
}

static const struct IIterable_PackageVtbl empty_iterable_vtbl =
{
    empty_iterable_QueryInterface,
    empty_iterable_AddRef,
    empty_iterable_Release,
    /* IInspectable methods */
    empty_iterable_GetIids,
    empty_iterable_GetRuntimeClassName,
    empty_iterable_GetTrustLevel,
    /* IIterable<T> methods */
    empty_iterable_First,
};

static inline struct empty_iterable *impl_from_IIterator_Package( IIterator_Package *iface )
{
    return CONTAINING_RECORD( iface, struct empty_iterable, IIterator_Package_iface );
}

static HRESULT WINAPI empty_iterator_QueryInterface( IIterator_Package *iface, REFIID iid, void **out )
{
    struct empty_iterable *impl = impl_from_IIterator_Package( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, impl->iterator_iid ))
    {
        *out = &impl->IIterator_Package_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI empty_iterator_AddRef( IIterator_Package *iface )
{
    struct empty_iterable *impl = impl_from_IIterator_Package( iface );
    return IIterable_Package_AddRef( &impl->IIterable_Package_iface );
}

static ULONG WINAPI empty_iterator_Release( IIterator_Package *iface )
{
    struct empty_iterable *impl = impl_from_IIterator_Package( iface );
    return IIterable_Package_Release( &impl->IIterable_Package_iface );
}

static HRESULT WINAPI empty_iterator_GetIids( IIterator_Package *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterator_GetRuntimeClassName( IIterator_Package *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterator_GetTrustLevel( IIterator_Package *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_iterator_get_Current( IIterator_Package *iface, IPackage **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI empty_iterator_get_HasCurrent( IIterator_Package *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI empty_iterator_MoveNext( IIterator_Package *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return E_BOUNDS;
}

static HRESULT WINAPI empty_iterator_GetMany( IIterator_Package *iface, UINT32 items_size, IPackage **items, UINT32 *count )
{
    TRACE( "iface %p, items_size %u, items %p, count %p.\n", iface, items_size, items, count );
    if (!count) return E_POINTER;
    *count = 0;
    return S_OK;
}

static const struct IIterator_PackageVtbl empty_iterator_vtbl =
{
    empty_iterator_QueryInterface,
    empty_iterator_AddRef,
    empty_iterator_Release,
    /* IInspectable methods */
    empty_iterator_GetIids,
    empty_iterator_GetRuntimeClassName,
    empty_iterator_GetTrustLevel,
    /* IIterator<T> methods */
    empty_iterator_get_Current,
    empty_iterator_get_HasCurrent,
    empty_iterator_MoveNext,
    empty_iterator_GetMany,
};

/* An empty IIterable<T *> (of packages or of users: same layout) */
static HRESULT empty_iterable_create( const IID *iterable_iid, const IID *iterator_iid, void **out )
{
    struct empty_iterable *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IIterable_Package_iface.lpVtbl = &empty_iterable_vtbl;
    impl->IIterator_Package_iface.lpVtbl = &empty_iterator_vtbl;
    impl->iterable_iid = iterable_iid;
    impl->iterator_iid = iterator_iid;
    impl->ref = 1;
    *out = &impl->IIterable_Package_iface;
    return S_OK;
}

HRESULT no_packages( IIterable_Package **packages )
{
    TRACE( "no packages are installed\n" );
    return empty_iterable_create( &IID_IIterable_Package, &IID_IIterator_Package, (void **)packages );
}

HRESULT no_users( IIterable_PackageUserInformation **users )
{
    return empty_iterable_create( &IID_IIterable_PackageUserInformation, &IID_IIterator_PackageUserInformation, (void **)users );
}

/* AddPackageOptions and StagePackageOptions *********************************/

/* The options both classes have; the rest are booleans */
struct package_options
{
    IAddPackageOptions IAddPackageOptions_iface;
    IStagePackageOptions IStagePackageOptions_iface;
    LONG ref;
    BOOL stage;
    IUriRuntimeClass *external_location;
    IPackageVolume *target_volume;
    StubPackageOption stub_option;
    boolean flags[10];
};

/* flags[] */
enum { DEVELOPER_MODE, FORCE_APP_SHUTDOWN, FORCE_TARGET_APP_SHUTDOWN, FORCE_UPDATE, INSTALL_ALL_RESOURCES,
       REQUIRED_CONTENT_GROUP_ONLY, RETAIN_FILES_ON_FAILURE, STAGE_IN_PLACE, ALLOW_UNSIGNED, DEFER_REGISTRATION };

static inline struct package_options *impl_from_IAddPackageOptions( IAddPackageOptions *iface )
{
    return CONTAINING_RECORD( iface, struct package_options, IAddPackageOptions_iface );
}

static HRESULT WINAPI add_options_QueryInterface( IAddPackageOptions *iface, REFIID iid, void **out )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = impl->stage ? (void *)&impl->IStagePackageOptions_iface : (void *)&impl->IAddPackageOptions_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (!impl->stage && IsEqualGUID( iid, &IID_IAddPackageOptions ))
    {
        *out = &impl->IAddPackageOptions_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (impl->stage && IsEqualGUID( iid, &IID_IStagePackageOptions ))
    {
        *out = &impl->IStagePackageOptions_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI add_options_AddRef( IAddPackageOptions *iface )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI add_options_Release( IAddPackageOptions *iface )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->external_location) IUriRuntimeClass_Release( impl->external_location );
        if (impl->target_volume) IPackageVolume_Release( impl->target_volume );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI add_options_GetIids( IAddPackageOptions *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_GetRuntimeClassName( IAddPackageOptions *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_GetTrustLevel( IAddPackageOptions *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_get_DependencyPackageUris( IAddPackageOptions *iface, IVector_Uri **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_get_TargetVolume( IAddPackageOptions *iface, IPackageVolume **value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->target_volume)) IPackageVolume_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI add_options_put_TargetVolume( IAddPackageOptions *iface, IPackageVolume *value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IPackageVolume_AddRef( value );
    if (impl->target_volume) IPackageVolume_Release( impl->target_volume );
    impl->target_volume = value;
    return S_OK;
}

static HRESULT WINAPI add_options_get_OptionalPackageFamilyNames( IAddPackageOptions *iface, IVector_HSTRING **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_get_OptionalPackageUris( IAddPackageOptions *iface, IVector_Uri **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_get_RelatedPackageUris( IAddPackageOptions *iface, IVector_Uri **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI add_options_get_ExternalLocationUri( IAddPackageOptions *iface, IUriRuntimeClass **value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if ((*value = impl->external_location)) IUriRuntimeClass_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI add_options_put_ExternalLocationUri( IAddPackageOptions *iface, IUriRuntimeClass *value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IUriRuntimeClass_AddRef( value );
    if (impl->external_location) IUriRuntimeClass_Release( impl->external_location );
    impl->external_location = value;
    return S_OK;
}

static HRESULT WINAPI add_options_get_StubPackageOption( IAddPackageOptions *iface, StubPackageOption *value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->stub_option;
    return S_OK;
}

static HRESULT WINAPI add_options_put_StubPackageOption( IAddPackageOptions *iface, StubPackageOption value )
{
    struct package_options *impl = impl_from_IAddPackageOptions( iface );
    TRACE( "iface %p, value %d.\n", iface, value );
    impl->stub_option = value;
    return S_OK;
}

#define BOOL_PROPERTY( pfx, iface_type, name, flag )                                  \
    static HRESULT WINAPI pfx##_get_##name( iface_type *iface, boolean *value )      \
    {                                                                                 \
        struct package_options *impl = impl_from_##iface_type( iface );               \
        TRACE( "iface %p, value %p.\n", iface, value );                               \
        if (!value) return E_POINTER;                                                 \
        *value = impl->flags[flag];                                                   \
        return S_OK;                                                                  \
    }                                                                                 \
    static HRESULT WINAPI pfx##_put_##name( iface_type *iface, boolean value )       \
    {                                                                                 \
        struct package_options *impl = impl_from_##iface_type( iface );               \
        TRACE( "iface %p, value %d.\n", iface, value );                               \
        impl->flags[flag] = value;                                                    \
        return S_OK;                                                                  \
    }

BOOL_PROPERTY( add_options, IAddPackageOptions, DeveloperMode, DEVELOPER_MODE )
BOOL_PROPERTY( add_options, IAddPackageOptions, ForceAppShutdown, FORCE_APP_SHUTDOWN )
BOOL_PROPERTY( add_options, IAddPackageOptions, ForceTargetAppShutdown, FORCE_TARGET_APP_SHUTDOWN )
BOOL_PROPERTY( add_options, IAddPackageOptions, ForceUpdateFromAnyVersion, FORCE_UPDATE )
BOOL_PROPERTY( add_options, IAddPackageOptions, InstallAllResources, INSTALL_ALL_RESOURCES )
BOOL_PROPERTY( add_options, IAddPackageOptions, RequiredContentGroupOnly, REQUIRED_CONTENT_GROUP_ONLY )
BOOL_PROPERTY( add_options, IAddPackageOptions, RetainFilesOnFailure, RETAIN_FILES_ON_FAILURE )
BOOL_PROPERTY( add_options, IAddPackageOptions, StageInPlace, STAGE_IN_PLACE )
BOOL_PROPERTY( add_options, IAddPackageOptions, AllowUnsigned, ALLOW_UNSIGNED )
BOOL_PROPERTY( add_options, IAddPackageOptions, DeferRegistrationWhenPackagesAreInUse, DEFER_REGISTRATION )

static const struct IAddPackageOptionsVtbl add_options_vtbl =
{
    add_options_QueryInterface,
    add_options_AddRef,
    add_options_Release,
    /* IInspectable methods */
    add_options_GetIids,
    add_options_GetRuntimeClassName,
    add_options_GetTrustLevel,
    /* IAddPackageOptions methods */
    add_options_get_DependencyPackageUris,
    add_options_get_TargetVolume,
    add_options_put_TargetVolume,
    add_options_get_OptionalPackageFamilyNames,
    add_options_get_OptionalPackageUris,
    add_options_get_RelatedPackageUris,
    add_options_get_ExternalLocationUri,
    add_options_put_ExternalLocationUri,
    add_options_get_StubPackageOption,
    add_options_put_StubPackageOption,
    add_options_get_DeveloperMode,
    add_options_put_DeveloperMode,
    add_options_get_ForceAppShutdown,
    add_options_put_ForceAppShutdown,
    add_options_get_ForceTargetAppShutdown,
    add_options_put_ForceTargetAppShutdown,
    add_options_get_ForceUpdateFromAnyVersion,
    add_options_put_ForceUpdateFromAnyVersion,
    add_options_get_InstallAllResources,
    add_options_put_InstallAllResources,
    add_options_get_RequiredContentGroupOnly,
    add_options_put_RequiredContentGroupOnly,
    add_options_get_RetainFilesOnFailure,
    add_options_put_RetainFilesOnFailure,
    add_options_get_StageInPlace,
    add_options_put_StageInPlace,
    add_options_get_AllowUnsigned,
    add_options_put_AllowUnsigned,
    add_options_get_DeferRegistrationWhenPackagesAreInUse,
    add_options_put_DeferRegistrationWhenPackagesAreInUse,
};

/* IStagePackageOptions: the same object, the same properties */
static inline struct package_options *impl_from_IStagePackageOptions( IStagePackageOptions *iface )
{
    return CONTAINING_RECORD( iface, struct package_options, IStagePackageOptions_iface );
}

#define FORWARD_STAGE( name, type )                                                   \
    static HRESULT WINAPI stage_options_##name( IStagePackageOptions *iface, type value ) \
    {                                                                                 \
        return add_options_##name( &impl_from_IStagePackageOptions( iface )->IAddPackageOptions_iface, value ); \
    }

static HRESULT WINAPI stage_options_QueryInterface( IStagePackageOptions *iface, REFIID iid, void **out )
{
    return add_options_QueryInterface( &impl_from_IStagePackageOptions( iface )->IAddPackageOptions_iface, iid, out );
}

static ULONG WINAPI stage_options_AddRef( IStagePackageOptions *iface )
{
    return add_options_AddRef( &impl_from_IStagePackageOptions( iface )->IAddPackageOptions_iface );
}

static ULONG WINAPI stage_options_Release( IStagePackageOptions *iface )
{
    return add_options_Release( &impl_from_IStagePackageOptions( iface )->IAddPackageOptions_iface );
}

static HRESULT WINAPI stage_options_GetIids( IStagePackageOptions *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

FORWARD_STAGE( GetRuntimeClassName, HSTRING * )
FORWARD_STAGE( GetTrustLevel, TrustLevel * )
FORWARD_STAGE( get_DependencyPackageUris, IVector_Uri ** )
FORWARD_STAGE( get_TargetVolume, IPackageVolume ** )
FORWARD_STAGE( put_TargetVolume, IPackageVolume * )
FORWARD_STAGE( get_OptionalPackageFamilyNames, IVector_HSTRING ** )
FORWARD_STAGE( get_OptionalPackageUris, IVector_Uri ** )
FORWARD_STAGE( get_RelatedPackageUris, IVector_Uri ** )
FORWARD_STAGE( get_ExternalLocationUri, IUriRuntimeClass ** )
FORWARD_STAGE( put_ExternalLocationUri, IUriRuntimeClass * )
FORWARD_STAGE( get_StubPackageOption, StubPackageOption * )
FORWARD_STAGE( put_StubPackageOption, StubPackageOption )
BOOL_PROPERTY( stage_options, IStagePackageOptions, DeveloperMode, DEVELOPER_MODE )
BOOL_PROPERTY( stage_options, IStagePackageOptions, ForceUpdateFromAnyVersion, FORCE_UPDATE )
BOOL_PROPERTY( stage_options, IStagePackageOptions, InstallAllResources, INSTALL_ALL_RESOURCES )
BOOL_PROPERTY( stage_options, IStagePackageOptions, RequiredContentGroupOnly, REQUIRED_CONTENT_GROUP_ONLY )
BOOL_PROPERTY( stage_options, IStagePackageOptions, StageInPlace, STAGE_IN_PLACE )
BOOL_PROPERTY( stage_options, IStagePackageOptions, AllowUnsigned, ALLOW_UNSIGNED )

static const struct IStagePackageOptionsVtbl stage_options_vtbl =
{
    stage_options_QueryInterface,
    stage_options_AddRef,
    stage_options_Release,
    /* IInspectable methods */
    stage_options_GetIids,
    stage_options_GetRuntimeClassName,
    stage_options_GetTrustLevel,
    /* IStagePackageOptions methods */
    stage_options_get_DependencyPackageUris,
    stage_options_get_TargetVolume,
    stage_options_put_TargetVolume,
    stage_options_get_OptionalPackageFamilyNames,
    stage_options_get_OptionalPackageUris,
    stage_options_get_RelatedPackageUris,
    stage_options_get_ExternalLocationUri,
    stage_options_put_ExternalLocationUri,
    stage_options_get_StubPackageOption,
    stage_options_put_StubPackageOption,
    stage_options_get_DeveloperMode,
    stage_options_put_DeveloperMode,
    stage_options_get_ForceUpdateFromAnyVersion,
    stage_options_put_ForceUpdateFromAnyVersion,
    stage_options_get_InstallAllResources,
    stage_options_put_InstallAllResources,
    stage_options_get_RequiredContentGroupOnly,
    stage_options_put_RequiredContentGroupOnly,
    stage_options_get_StageInPlace,
    stage_options_put_StageInPlace,
    stage_options_get_AllowUnsigned,
    stage_options_put_AllowUnsigned,
};

/* The factories of both classes */
struct options_factory
{
    IActivationFactory IActivationFactory_iface;
    BOOL stage;
};

static inline struct options_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct options_factory, IActivationFactory_iface );
}

static HRESULT WINAPI options_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI options_factory_AddRef( IActivationFactory *iface )
{
    return 2;
}

static ULONG WINAPI options_factory_Release( IActivationFactory *iface )
{
    return 1;
}

static HRESULT WINAPI options_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct options_factory *factory = impl_from_IActivationFactory( iface );
    struct package_options *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }
    impl->IAddPackageOptions_iface.lpVtbl = &add_options_vtbl;
    impl->IStagePackageOptions_iface.lpVtbl = &stage_options_vtbl;
    impl->ref = 1;
    impl->stage = factory->stage;

    *instance = factory->stage ? (IInspectable *)&impl->IStagePackageOptions_iface
                               : (IInspectable *)&impl->IAddPackageOptions_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl options_factory_vtbl =
{
    options_factory_QueryInterface,
    options_factory_AddRef,
    options_factory_Release,
    /* IInspectable methods */
    options_factory_GetIids,
    options_factory_GetRuntimeClassName,
    options_factory_GetTrustLevel,
    /* IActivationFactory methods */
    options_factory_ActivateInstance,
};

static struct options_factory add_options_factory_impl = { {&options_factory_vtbl}, FALSE };
static struct options_factory stage_options_factory_impl = { {&options_factory_vtbl}, TRUE };

IActivationFactory *add_package_options_factory = &add_options_factory_impl.IActivationFactory_iface;
IActivationFactory *stage_package_options_factory = &stage_options_factory_impl.IActivationFactory_iface;
