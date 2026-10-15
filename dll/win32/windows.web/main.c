/* WinRT Windows.Web Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#include "initguid.h"
#include "private.h"

#include "wine/debug.h"
#ifdef __REACTOS__
#include <stdio.h>
#include <olectl.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(web);

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Data_Json_JsonArray ))
        IActivationFactory_QueryInterface( json_array_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, RuntimeClass_Windows_Data_Json_JsonObject ))
        IActivationFactory_QueryInterface( json_object_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, RuntimeClass_Windows_Data_Json_JsonValue ))
        IActivationFactory_QueryInterface( json_value_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}

#ifdef __REACTOS__
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

static const WCHAR *const classes[] =
{
    RuntimeClass_Windows_Data_Json_JsonArray,
    RuntimeClass_Windows_Data_Json_JsonObject,
    RuntimeClass_Windows_Data_Json_JsonValue,
};

#define CLASSES_KEY L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\"

/* The registration that RoActivateInstance looks up (Wine generates it from classes.idl) */
HRESULT WINAPI DllRegisterServer(void)
{
    static const WCHAR Path[] = L"%SystemRoot%\\system32\\Windows.Web.dll";
    WCHAR name[MAX_PATH];
    DWORD zero = 0;
    HKEY key;
    int i;

    for (i = 0; i < ARRAY_SIZE(classes); i++)
    {
        swprintf(name, ARRAY_SIZE(name), CLASSES_KEY L"%s", classes[i]);
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, name, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL))
            return SELFREG_E_CLASS;
        RegSetValueExW(key, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)Path, sizeof(Path));
        RegSetValueExW(key, L"ActivationType", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero)); /* in process */
        RegSetValueExW(key, L"Threading", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));      /* both */
        RegSetValueExW(key, L"TrustLevel", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));     /* base */
        RegCloseKey(key);
    }
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    WCHAR name[MAX_PATH];
    int i;

    for (i = 0; i < ARRAY_SIZE(classes); i++)
    {
        swprintf(name, ARRAY_SIZE(name), CLASSES_KEY L"%s", classes[i]);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, name);
    }
    return S_OK;
}
#endif
