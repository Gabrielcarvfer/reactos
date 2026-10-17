/* WinRT Windows.Management.Deployment.PackageManager Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

struct package_manager
{
    IPackageManager IPackageManager_iface;
    IPackageManager2 IPackageManager2_iface;
#ifdef __REACTOS__
    IPackageManager9 IPackageManager9_iface;
#endif
    LONG ref;
};

static inline struct package_manager *impl_from_IPackageManager( IPackageManager *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager, IPackageManager_iface );
}

static HRESULT WINAPI package_manager_QueryInterface( IPackageManager *iface, REFIID iid, void **out )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPackageManager ))
    {
        *out = &impl->IPackageManager_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageManager2 ))
    {
        *out = &impl->IPackageManager2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

#ifdef __REACTOS__
    if (IsEqualGUID( iid, &IID_IPackageManager9 ))
    {
        *out = &impl->IPackageManager9_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
#endif

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_manager_AddRef( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_manager_Release( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_manager_GetIids( IPackageManager *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_GetRuntimeClassName( IPackageManager *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_GetTrustLevel( IPackageManager *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_AddPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri,
    IIterable_Uri *dependencies, DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
#ifdef __REACTOS__
    return deployment_stub_completed( "AddPackageAsync", uri, NULL, operation );
#else
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_UpdatePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
#ifdef __REACTOS__
    return deployment_stub_completed( "UpdatePackageAsync", uri, NULL, operation );
#else
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_RemovePackageAsync( IPackageManager *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_StagePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
#ifdef __REACTOS__
    return deployment_stub_completed( "StagePackageAsync", uri, NULL, operation );
#else
    FIXME( "iface %p, uri %p, dependencies %p, operation %p stub!\n", iface, uri, dependencies, operation );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_RegisterPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
#ifdef __REACTOS__
    return deployment_stub_completed( "RegisterPackageAsync", uri, NULL, operation );
#else
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackages( IPackageManager *iface, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, packages %p stub!\n", iface, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityId( IPackageManager *iface, HSTRING sid, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, packages %p stub!\n", iface, debugstr_hstring(sid), packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackagesByNamePublisher( IPackageManager *iface, HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, name %s, publisher %s, packages %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), packages );

    if (!name || !publisher) return E_INVALIDARG;
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdNamePublisher( IPackageManager *iface, HSTRING sid,
    HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, name %s, publisher %s, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindUsers( IPackageManager *iface, HSTRING name, IIterable_PackageUserInformation **users )
{
#ifdef __REACTOS__
    return no_users( users );
#else
    FIXME( "iface %p, name %s, users %p stub!\n", iface, debugstr_hstring(name), users );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_SetPackageState( IPackageManager *iface, HSTRING name, PackageState state )
{
    FIXME("iface %p, name %s, state %d stub!\n", iface, debugstr_hstring(name), state);
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackageByPackageFullName( IPackageManager *iface, HSTRING name, IPackage **package )
{
#ifdef __REACTOS__
    /* (no package is installed) */
    if (!package) return E_POINTER;
    *package = NULL;
    return S_OK;
#else
    FIXME( "iface %p, name %s, package %p stub!\n", iface, debugstr_hstring(name), package );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_CleanupPackageForUserAsync( IPackageManager *iface, HSTRING name, HSTRING sid,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, sid %s, operation %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(sid), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByPackageFamilyName( IPackageManager *iface, HSTRING family_name,
    IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, family_name %s, packages %p stub!\n", iface, debugstr_hstring(family_name), packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdPackageFamilyName( IPackageManager *iface, HSTRING sid,
    HSTRING family_name, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, family_name %s, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager_FindPackageByUserSecurityIdPackageFullName( IPackageManager *iface, HSTRING sid, HSTRING name, IPackage **package )
{
#ifdef __REACTOS__
    /* (no package is installed) */
    if (!package) return E_POINTER;
    *package = NULL;
    return S_OK;
#else
    FIXME( "iface %p, sid %s, name %s, package %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), package );
    return E_NOTIMPL;
#endif
}

static const struct IPackageManagerVtbl package_manager_vtbl =
{
    package_manager_QueryInterface,
    package_manager_AddRef,
    package_manager_Release,
    /* IInspectable methods */
    package_manager_GetIids,
    package_manager_GetRuntimeClassName,
    package_manager_GetTrustLevel,
    /* IPackageManager methods */
    package_manager_AddPackageAsync,
    package_manager_UpdatePackageAsync,
    package_manager_RemovePackageAsync,
    package_manager_StagePackageAsync,
    package_manager_RegisterPackageAsync,
    package_manager_FindPackages,
    package_manager_FindPackagesByUserSecurityId,
    package_manager_FindPackagesByNamePublisher,
    package_manager_FindPackagesByUserSecurityIdNamePublisher,
    package_manager_FindUsers,
    package_manager_SetPackageState,
    package_manager_FindPackageByPackageFullName,
    package_manager_CleanupPackageForUserAsync,
    package_manager_FindPackagesByPackageFamilyName,
    package_manager_FindPackagesByUserSecurityIdPackageFamilyName,
    package_manager_FindPackageByUserSecurityIdPackageFullName
};

DEFINE_IINSPECTABLE( package_manager2, IPackageManager2, struct package_manager, IPackageManager_iface );

static HRESULT WINAPI package_manager2_RemovePackageWithOptionsAsync( IPackageManager2 *iface, HSTRING name, RemovalOptions options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, options %d, operation %p stub!\n", iface, debugstr_hstring(name), options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_StagePackageWithOptionsAsync( IPackageManager2 *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
#ifdef __REACTOS__
    return deployment_stub_completed( "StagePackageWithOptionsAsync", uri, NULL, operation );
#else
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_RegisterPackageByFullNameAsync( IPackageManager2 *iface, HSTRING name, IIterable_HSTRING *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, dependencies %p, options %d, operation %p stub!\n", iface, debugstr_hstring(name), dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesWithPackageTypes( IPackageManager2 *iface, PackageTypes types, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, types %d, packages %p stub!\n", iface, types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdWithPackageTypes( IPackageManager2 *iface, HSTRING sid,
    PackageTypes types, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_FindPackagesByNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING name, HSTRING publisher,
    PackageTypes types, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, name %s, publisher %s, types %d, packages %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING name,
    HSTRING publisher, PackageTypes types, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, name %s, publisher %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING family_name, PackageTypes types,
   IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, family_name %s, types %d, packages %p stub!\n", iface, debugstr_hstring(family_name), types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING family_name,
    PackageTypes types, IIterable_Package **packages )
{
#ifdef __REACTOS__
    return no_packages( packages );
#else
    FIXME( "iface %p, sid %s, family_name %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), types, packages );
    return E_NOTIMPL;
#endif
}

static HRESULT WINAPI package_manager2_StageUserDataAsync( IPackageManager2 *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static const struct IPackageManager2Vtbl package_manager2_vtbl =
{
    package_manager2_QueryInterface,
    package_manager2_AddRef,
    package_manager2_Release,
    /* IInspectable methods */
    package_manager2_GetIids,
    package_manager2_GetRuntimeClassName,
    package_manager2_GetTrustLevel,
    /* IPackageManager2 methods */
    package_manager2_RemovePackageWithOptionsAsync,
    package_manager2_StagePackageWithOptionsAsync,
    package_manager2_RegisterPackageByFullNameAsync,
    package_manager2_FindPackagesWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdWithPackageTypes,
    package_manager2_FindPackagesByNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes,
    package_manager2_StageUserDataAsync,
};

#ifdef __REACTOS__

DEFINE_IINSPECTABLE( package_manager9, IPackageManager9, struct package_manager, IPackageManager_iface );

static HRESULT WINAPI package_manager9_FindProvisionedPackages( IPackageManager9 *iface, IVector_Package **result )
{
    FIXME( "iface %p, result %p stub!\n", iface, result );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_AddPackageByUriAsync( IPackageManager9 *iface, IUriRuntimeClass *uri, IAddPackageOptions *options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    TRACE( "iface %p, uri %p, options %p, operation %p.\n", iface, uri, options, operation );
    return deployment_stub_completed( "AddPackageByUriAsync", uri, NULL, operation );
}

static HRESULT WINAPI package_manager9_StagePackageByUriAsync( IPackageManager9 *iface, IUriRuntimeClass *uri, IStagePackageOptions *options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    TRACE( "iface %p, uri %p, options %p, operation %p.\n", iface, uri, options, operation );
    return deployment_stub_completed( "StagePackageByUriAsync", uri, NULL, operation );
}

static HRESULT WINAPI package_manager9_RegisterPackageByUriAsync( IPackageManager9 *iface, IUriRuntimeClass *uri, IRegisterPackageOptions *options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    TRACE( "iface %p, uri %p, options %p, operation %p.\n", iface, uri, options, operation );
    return deployment_stub_completed( "RegisterPackageByUriAsync", uri, NULL, operation );
}

static HRESULT WINAPI package_manager9_RegisterPackagesByFullNameAsync( IPackageManager9 *iface, IIterable_HSTRING *names,
    IRegisterPackageOptions *options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, names %p, options %p, operation %p stub!\n", iface, names, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_SetPackageStubPreference( IPackageManager9 *iface, HSTRING family_name, PackageStubPreference value )
{
    FIXME( "iface %p, family_name %s, value %d stub!\n", iface, debugstr_hstring(family_name), value );
    return S_OK;
}

static HRESULT WINAPI package_manager9_GetPackageStubPreference( IPackageManager9 *iface, HSTRING family_name, PackageStubPreference *value )
{
    FIXME( "iface %p, family_name %s, value %p stub!\n", iface, debugstr_hstring(family_name), value );
    if (!value) return E_POINTER;
    *value = PackageStubPreference_Full;
    return S_OK;
}

static const struct IPackageManager9Vtbl package_manager9_vtbl =
{
    package_manager9_QueryInterface,
    package_manager9_AddRef,
    package_manager9_Release,
    /* IInspectable methods */
    package_manager9_GetIids,
    package_manager9_GetRuntimeClassName,
    package_manager9_GetTrustLevel,
    /* IPackageManager9 methods */
    package_manager9_FindProvisionedPackages,
    package_manager9_AddPackageByUriAsync,
    package_manager9_StagePackageByUriAsync,
    package_manager9_RegisterPackageByUriAsync,
    package_manager9_RegisterPackagesByFullNameAsync,
    package_manager9_SetPackageStubPreference,
    package_manager9_GetPackageStubPreference,
};

#endif /* __REACTOS__ */

struct package_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct package_manager_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct package_manager *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IPackageManager_iface.lpVtbl = &package_manager_vtbl;
    impl->IPackageManager2_iface.lpVtbl = &package_manager2_vtbl;
#ifdef __REACTOS__
    impl->IPackageManager9_iface.lpVtbl = &package_manager9_vtbl;
#endif
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IPackageManager_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct package_manager_statics package_manager_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *package_manager_factory = &package_manager_statics.IActivationFactory_iface;
