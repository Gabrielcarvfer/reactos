/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     windows.data.pdf.dll must load and report that no PDF engine is there
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * Programs such as PdfCraft import PdfCreateRenderer from windows.data.pdf.dll
 * at load time: without the DLL they don't start at all.
 */

#include <apitest.h>
#include <winstring.h>

typedef HRESULT (WINAPI *PFN_PdfCreateRenderer)(IUnknown *, void **);
typedef HRESULT (WINAPI *PFN_DllGetActivationFactory)(HSTRING, IUnknown **);

START_TEST(PdfInterop)
{
    HMODULE hmod = LoadLibraryW(L"windows.data.pdf.dll");
    PFN_PdfCreateRenderer pPdfCreateRenderer;
    PFN_DllGetActivationFactory pDllGetActivationFactory;
    void *renderer = (void *)(ULONG_PTR)0xdeadbeef;
    IUnknown *factory = (IUnknown *)(ULONG_PTR)0xdeadbeef;
    HRESULT hr;

    ok(hmod != NULL, "windows.data.pdf.dll not loaded: %lu\n", GetLastError());
    if (!hmod)
        return;

    pPdfCreateRenderer = (PFN_PdfCreateRenderer)GetProcAddress(hmod, "PdfCreateRenderer");
    pDllGetActivationFactory = (PFN_DllGetActivationFactory)GetProcAddress(hmod, "DllGetActivationFactory");
    ok(pPdfCreateRenderer != NULL, "PdfCreateRenderer missing\n");
    ok(pDllGetActivationFactory != NULL, "DllGetActivationFactory missing\n");

    if (pPdfCreateRenderer)
    {
        /* No device: Windows fails too; the output pointer must not be left dangling */
        hr = pPdfCreateRenderer(NULL, &renderer);
        ok(FAILED(hr), "PdfCreateRenderer(NULL) returned 0x%lx\n", hr);
        ok(renderer == NULL, "renderer = %p\n", renderer);
    }
    if (pDllGetActivationFactory)
    {
        HSTRING name;
        WindowsCreateString(L"Windows.Data.Pdf.Bogus", 22, &name);
        hr = pDllGetActivationFactory(name, &factory);
        ok(hr == CLASS_E_CLASSNOTAVAILABLE, "hr = 0x%lx\n", hr);
        ok(factory == NULL, "factory = %p\n", factory);
        WindowsDeleteString(name);
    }
    FreeLibrary(hmod);
}
