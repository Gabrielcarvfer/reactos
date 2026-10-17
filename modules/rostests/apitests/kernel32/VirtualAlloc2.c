/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KernelBase.dll by name, VirtualAlloc2 and MapViewOfFile3 with
 *              MEM_ADDRESS_REQUIREMENTS
 *
 * Cygwin/MSYS2 (Git for Windows' bash.exe) load KernelBase.dll by name and
 * place thread stacks and mmap regions with these when Windows reports build
 * 17134+. KernelBase.dll did not load: their main thread stack ended up
 * broken and every MSYS2 program died of a stack overflow (exit 128).
 */

#include "precomp.h"

typedef PVOID (WINAPI *PVIRTUALALLOC2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *PMAPVIEWOFFILE3)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);

START_TEST(VirtualAlloc2)
{
#ifdef _WIN64
    const ULONG_PTR Low = 0x6000000000ULL, High = 0x7000000000ULL - 1;
    MEM_ADDRESS_REQUIREMENTS Requirements = { (PVOID)Low, (PVOID)High, 0x100000 };
    MEM_EXTENDED_PARAMETER Parameter = { 0 };
    PVIRTUALALLOC2 pVirtualAlloc2;
    PMAPVIEWOFFILE3 pMapViewOfFile3;
    PVOID First, Second, Bottom, View;
    HMODULE KernelBase;
    HANDLE Mapping;

    KernelBase = LoadLibraryW(L"KernelBase.dll");
    ok(KernelBase != NULL, "KernelBase.dll not loaded: %lu\n", GetLastError());
    if (!KernelBase)
        return;
    pVirtualAlloc2 = (PVIRTUALALLOC2)GetProcAddress(KernelBase, "VirtualAlloc2");
    pMapViewOfFile3 = (PMAPVIEWOFFILE3)GetProcAddress(KernelBase, "MapViewOfFile3");
    ok(GetProcAddress(KernelBase, "QueryInterruptTimePrecise") != NULL, "no QueryInterruptTimePrecise\n");
    if (!pVirtualAlloc2 || !pMapViewOfFile3)
    {
        skip("No VirtualAlloc2 / MapViewOfFile3\n");
        return;
    }

    Parameter.Type = MemExtendedParameterAddressRequirements;
    Parameter.Pointer = &Requirements;

    First = pVirtualAlloc2(GetCurrentProcess(), NULL, 0x200000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE, &Parameter, 1);
    ok(First == (PVOID)(High + 1 - 0x200000), "top-down: %p\n", First);
    Second = pVirtualAlloc2(NULL, NULL, 0x200000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE, &Parameter, 1);
    ok(Second == (PVOID)(High + 1 - 0x400000), "second top-down: %p\n", Second);
    Bottom = pVirtualAlloc2(NULL, NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &Parameter, 1);
    ok(Bottom == (PVOID)Low, "bottom-up: %p\n", Bottom);
    if (Bottom)
        *(volatile ULONG *)Bottom = 1;

    /* A base address does not go with address requirements */
    ok(pVirtualAlloc2(NULL, (PVOID)Low, 0x10000, MEM_RESERVE, PAGE_READWRITE, &Parameter, 1) == NULL,
       "allocated at a given address with requirements\n");

    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x30000, NULL);
    ok(Mapping != NULL, "CreateFileMapping failed: %lu\n", GetLastError());
    View = pMapViewOfFile3(Mapping, GetCurrentProcess(), NULL, 0, 0, MEM_TOP_DOWN, PAGE_READWRITE, &Parameter, 1);
    ok(View != NULL && (ULONG_PTR)View >= Low && (ULONG_PTR)View + 0x30000 - 1 <= High &&
       !((ULONG_PTR)View & 0xfffff), "MapViewOfFile3: %p (%lu)\n", View, GetLastError());
    if (View)
    {
        ((volatile UCHAR *)View)[0x2ffff] = 1;
        UnmapViewOfFile(View);
    }
    CloseHandle(Mapping);

    if (First) VirtualFree(First, 0, MEM_RELEASE);
    if (Second) VirtualFree(Second, 0, MEM_RELEASE);
    if (Bottom) VirtualFree(Bottom, 0, MEM_RELEASE);
#else
    skip("64-bit address ranges\n");
#endif
}
