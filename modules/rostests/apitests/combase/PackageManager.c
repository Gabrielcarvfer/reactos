/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Tests for Windows.Management.Deployment.PackageManager
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * PowerToys' setup creates a PackageManager, StagePackageOptions with an
 * ExternalLocationUri, and asks whether packages are installed. (On ReactOS
 * the deployment itself is a compatibility stub, not tested here: it differs
 * from Windows on purpose.)
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
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include <windows.management.deployment.h>

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

START_TEST(PackageManager)
{
    IInspectable *manager_obj, *options_obj;
    IPackageManager *manager = NULL;
    IPackageManager9 *manager9 = NULL;
    IStagePackageOptions *options = NULL;
    IUriRuntimeClassFactory *uri_factory = NULL;
    IUriRuntimeClass *uri = NULL, *back = NULL;
    IIterable_Package *packages = NULL;
    IIterator_Package *iterator = NULL;
    IPackage *package = (IPackage *)0xdeadbeef;
    boolean has_current = TRUE;
    HSTRING name, family, sid;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize: 0x%08lx\n", hr);

    manager_obj = Activate(L"Windows.Management.Deployment.PackageManager");
    options_obj = Activate(L"Windows.Management.Deployment.StagePackageOptions");
    if (!manager_obj || !options_obj)
    {
        skip("no PackageManager\n");
        if (manager_obj) IInspectable_Release(manager_obj);
        if (options_obj) IInspectable_Release(options_obj);
        RoUninitialize();
        return;
    }

    hr = IInspectable_QueryInterface(manager_obj, &IID_IPackageManager, (void **)&manager);
    ok(hr == S_OK, "IPackageManager: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(manager_obj, &IID_IPackageManager9, (void **)&manager9);
    ok(hr == S_OK, "IPackageManager9: 0x%08lx\n", hr);
    hr = IInspectable_QueryInterface(options_obj, &IID_IStagePackageOptions, (void **)&options);
    ok(hr == S_OK, "IStagePackageOptions: 0x%08lx\n", hr);

    /* The options keep the external location */
    name = Str(L"Windows.Foundation.Uri");
    hr = RoGetActivationFactory(name, &IID_IUriRuntimeClassFactory, (void **)&uri_factory);
    WindowsDeleteString(name);
    ok(hr == S_OK, "Uri factory: 0x%08lx\n", hr);
    if (uri_factory && options)
    {
        name = Str(L"C:\\Program Files\\PowerToys\\WinUI3Apps\\");
        hr = IUriRuntimeClassFactory_CreateUri(uri_factory, name, &uri);
        WindowsDeleteString(name);
        ok(hr == S_OK, "CreateUri: 0x%08lx\n", hr);
        hr = IStagePackageOptions_get_ExternalLocationUri(options, &back);
        ok(hr == S_OK && back == NULL, "initial ExternalLocationUri: 0x%08lx, %p\n", hr, back);
        hr = IStagePackageOptions_put_ExternalLocationUri(options, uri);
        ok(hr == S_OK, "put_ExternalLocationUri: 0x%08lx\n", hr);
        hr = IStagePackageOptions_get_ExternalLocationUri(options, &back);
        ok(hr == S_OK && back == uri, "ExternalLocationUri: 0x%08lx, %p (put %p)\n", hr, back, uri);
        if (back) IUriRuntimeClass_Release(back);
        if (uri) IUriRuntimeClass_Release(uri);
    }

    /* A package that does not exist is not found (for the current user) */
    family = Str(L"ReactOS.NoSuchPackage_0000000000000");
    sid = NULL;
    if (manager)
    {
        hr = IPackageManager_FindPackagesByUserSecurityIdPackageFamilyName(manager, sid, family, &packages);
        ok(hr == S_OK, "FindPackagesByUserSecurityIdPackageFamilyName: 0x%08lx\n", hr);
        if (packages)
        {
            hr = IIterable_Package_First(packages, &iterator);
            ok(hr == S_OK, "First: 0x%08lx\n", hr);
            if (iterator)
            {
                hr = IIterator_Package_get_HasCurrent(iterator, &has_current);
                ok(hr == S_OK && !has_current, "HasCurrent: 0x%08lx, %d\n", hr, has_current);
                IIterator_Package_Release(iterator);
            }
            IIterable_Package_Release(packages);
        }

        name = Str(L"ReactOS.NoSuchPackage_1.0.0.0_x64__0000000000000");
        hr = IPackageManager_FindPackageByUserSecurityIdPackageFullName(manager, sid, name, &package);
        WindowsDeleteString(name);
        ok(hr == S_OK && package == NULL, "FindPackageByUserSecurityIdPackageFullName: 0x%08lx, %p\n", hr, package);
    }
    WindowsDeleteString(family);

    if (uri_factory) IUriRuntimeClassFactory_Release(uri_factory);
    if (options) IStagePackageOptions_Release(options);
    if (manager9) IPackageManager9_Release(manager9);
    if (manager) IPackageManager_Release(manager);
    IInspectable_Release(options_obj);
    IInspectable_Release(manager_obj);
    RoUninitialize();
}
