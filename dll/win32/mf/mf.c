/*
 * PROJECT:     ReactOS Media Foundation
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Media Foundation (mf.dll), not available yet (see mfplat.c)
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>

#define MF_E_PLATFORM_NOT_INITIALIZED ((HRESULT)0xC00D36B0L)

/* No capture devices */
HRESULT WINAPI MFEnumDeviceSources(void *Attributes, void ***Sources, UINT32 *Count)
{
    if (!Sources || !Count)
        return E_POINTER;
    *Sources = NULL;
    *Count = 0;
    return S_OK;
}

HRESULT WINAPI MFCreateDeviceSource(void *Attributes, void **Source)
{
    if (Source) *Source = NULL;
    return MF_E_PLATFORM_NOT_INITIALIZED;
}

HRESULT WINAPI MFGetService(IUnknown *Object, REFGUID Service, REFIID Riid, void **Out)
{
    if (Out) *Out = NULL;
    return E_NOINTERFACE;
}
