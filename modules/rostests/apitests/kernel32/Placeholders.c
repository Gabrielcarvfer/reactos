/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Placeholders: VirtualAlloc2 (MEM_RESERVE_PLACEHOLDER,
 *              MEM_REPLACE_PLACEHOLDER), VirtualFree (MEM_PRESERVE_PLACEHOLDER,
 *              MEM_COALESCE_PLACEHOLDERS), MapViewOfFile3 and UnmapViewOfFile2
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * The .NET runtime (5+) loads the assemblies of a single-file program
 * (HandBrake) this way as soon as KernelBase has VirtualAlloc2 and
 * MapViewOfFile3: one placeholder for the image, split page by page, each
 * part replaced by a view of the file (page-aligned offsets) or by committed
 * memory. Our kernel refused MEM_RESERVE_PLACEHOLDER and every such program
 * died at startup (0xe0434352, E_INVALIDARG).
 */

#include "precomp.h"

typedef PVOID (WINAPI *PVIRTUALALLOC2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *PMAPVIEWOFFILE3)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef BOOL (WINAPI *PUNMAPVIEWOFFILE2)(HANDLE, PVOID, ULONG);

static PVIRTUALALLOC2 pVirtualAlloc2;
static PMAPVIEWOFFILE3 pMapViewOfFile3;
static PUNMAPVIEWOFFILE2 pUnmapViewOfFile2;

#define PAGE 0x1000
#define FILE_PAGES 6

/* State, type and size of the region at Address */
static void check_region_(int line, PVOID Address, DWORD State, DWORD Type, SIZE_T Size)
{
    MEMORY_BASIC_INFORMATION Info;

    ok_(__FILE__, line)(VirtualQuery(Address, &Info, sizeof(Info)) == sizeof(Info), "VirtualQuery failed\n");
    ok_(__FILE__, line)(Info.State == State, "%p: state %lx, expected %lx\n", Address, Info.State, State);
    if (Type)
        ok_(__FILE__, line)(Info.Type == Type, "%p: type %lx, expected %lx\n", Address, Info.Type, Type);
    if (Size)
        ok_(__FILE__, line)(Info.RegionSize == Size, "%p: size %Ix, expected %Ix\n", Address, Info.RegionSize, Size);
}
#define check_region(a, s, t, z) check_region_(__LINE__, a, s, t, z)

static HANDLE CreateTestFile(PWSTR Path)
{
    UCHAR Page[PAGE];
    HANDLE File;
    DWORD Written;
    ULONG i;

    GetTempPathW(MAX_PATH, Path);
    GetTempFileNameW(Path, L"plh", 0, Path);
    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;
    for (i = 0; i < FILE_PAGES; i++)
    {
        memset(Page, 'A' + i, sizeof(Page));
        WriteFile(File, Page, sizeof(Page), &Written, NULL);
    }
    return File;
}

/* The order of the .NET runtime (peimagelayout.cpp, LoadImageByMappingParts) */
static void Test_CoreclrSequence(HANDLE Mapping)
{
    PUCHAR Reserved, End, Part, Mapped;
    SIZE_T Size = 8 * PAGE;

    /* AllocPlaceholder */
    Reserved = pVirtualAlloc2(GetCurrentProcess(), NULL, Size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                              PAGE_NOACCESS, NULL, 0);
    ok(Reserved != NULL, "placeholder: %lu\n", GetLastError());
    if (!Reserved)
        return;
    End = Reserved + Size;
    check_region(Reserved, MEM_RESERVE, MEM_PRIVATE, Size);

    /* A placeholder can't be committed */
    ok(VirtualAlloc(Reserved, PAGE, MEM_COMMIT, PAGE_READWRITE) == NULL, "committed a placeholder\n");

    /* Headers: split one page off, map the first page of the file there */
    ok(VirtualFree(Reserved, PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split 1: %lu\n", GetLastError());
    check_region(Reserved, MEM_RESERVE, 0, PAGE);
    check_region(Reserved + PAGE, MEM_RESERVE, 0, Size - PAGE);
    Mapped = pMapViewOfFile3(Mapping, GetCurrentProcess(), Reserved, 0, PAGE, MEM_REPLACE_PLACEHOLDER,
                             PAGE_READONLY, NULL, 0);
    ok(Mapped == Reserved, "map headers: %p %lu\n", Mapped, GetLastError());
    if (Mapped) ok(Mapped[0] == 'A' && Mapped[PAGE - 1] == 'A', "headers: %c\n", Mapped[0]);
    check_region(Reserved, MEM_COMMIT, MEM_MAPPED, PAGE);

    /* Code: two pages at a page-aligned (not 64 KB) file offset, executable */
    Part = Reserved + PAGE;
    ok(VirtualFree(Part, 2 * PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split 2: %lu\n", GetLastError());
    Mapped = pMapViewOfFile3(Mapping, GetCurrentProcess(), Part, PAGE, 2 * PAGE, MEM_REPLACE_PLACEHOLDER,
                             PAGE_EXECUTE_READ, NULL, 0);
    ok(Mapped == Part, "map code: %p %lu\n", Mapped, GetLastError());
    if (Mapped) ok(Mapped[0] == 'B' && Mapped[PAGE] == 'C', "code: %c %c\n", Mapped[0], Mapped[PAGE]);

    /* Data: copy-on-write */
    Part += 2 * PAGE;
    ok(VirtualFree(Part, PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split 3: %lu\n", GetLastError());
    Mapped = pMapViewOfFile3(Mapping, GetCurrentProcess(), Part, 3 * PAGE, PAGE, MEM_REPLACE_PLACEHOLDER,
                             PAGE_WRITECOPY, NULL, 0);
    ok(Mapped == Part, "map data: %p %lu\n", Mapped, GetLastError());
    if (Mapped)
    {
        ok(Mapped[0] == 'D', "data: %c\n", Mapped[0]);
        Mapped[0] = 'x';
        ok(Mapped[0] == 'x', "copy-on-write: %c\n", Mapped[0]);
    }

    /* The rest of a section that is not in the file: committed memory */
    Part += PAGE;
    ok(VirtualFree(Part, PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split 4: %lu\n", GetLastError());
    Mapped = pVirtualAlloc2(GetCurrentProcess(), Part, PAGE, MEM_COMMIT | MEM_RESERVE | MEM_REPLACE_PLACEHOLDER,
                            PAGE_READWRITE, NULL, 0);
    ok(Mapped == Part, "commit into placeholder: %p %lu\n", Mapped, GetLastError());
    if (Mapped)
    {
        ok(Mapped[0] == 0, "not zeroed\n");
        Mapped[0] = 1;
        check_region(Part, MEM_COMMIT, MEM_PRIVATE, PAGE);
    }

    /* What is left stays a placeholder */
    Part += PAGE;
    check_region(Part, MEM_RESERVE, MEM_PRIVATE, End - Part);

    /* A replacement must be the exact placeholder */
    ok(pMapViewOfFile3(Mapping, GetCurrentProcess(), Part, 0, PAGE, MEM_REPLACE_PLACEHOLDER,
                       PAGE_READONLY, NULL, 0) == NULL, "replaced part of a placeholder\n");
    ok(pVirtualAlloc2(GetCurrentProcess(), Reserved + 3 * PAGE, PAGE,
                      MEM_COMMIT | MEM_RESERVE | MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0) == NULL,
       "replaced a view\n");
    /* A placeholder is not released in parts */
    ok(!VirtualFree(Part, PAGE, MEM_RELEASE), "released part of a placeholder\n");

    /* FreeImageParts */
    ok(UnmapViewOfFile(Reserved), "unmap headers: %lu\n", GetLastError());
    ok(UnmapViewOfFile(Reserved + PAGE), "unmap code: %lu\n", GetLastError());
    ok(UnmapViewOfFile(Reserved + 3 * PAGE), "unmap data: %lu\n", GetLastError());
    ok(VirtualFree(Reserved + 4 * PAGE, 0, MEM_RELEASE), "free committed: %lu\n", GetLastError());
    ok(VirtualFree(Part, 0, MEM_RELEASE), "free placeholder: %lu\n", GetLastError());
    check_region(Reserved, MEM_FREE, 0, 0);
    check_region(Part, MEM_FREE, 0, 0);
}

static void Test_CoalesceAndPreserve(HANDLE Mapping)
{
    PUCHAR Reserved, Mapped;
    SIZE_T Size = 4 * PAGE;

    Reserved = pVirtualAlloc2(NULL, NULL, Size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
    ok(Reserved != NULL, "placeholder: %lu\n", GetLastError());
    if (!Reserved)
        return;

    /* Split in the middle, then join the three parts again */
    ok(VirtualFree(Reserved + PAGE, PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split: %lu\n", GetLastError());
    check_region(Reserved, MEM_RESERVE, 0, PAGE);
    check_region(Reserved + PAGE, MEM_RESERVE, 0, PAGE);
    check_region(Reserved + 2 * PAGE, MEM_RESERVE, 0, 2 * PAGE);
    ok(VirtualFree(Reserved, Size, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS), "coalesce: %lu\n", GetLastError());
    check_region(Reserved, MEM_RESERVE, MEM_PRIVATE, Size);

    /* A view unmapped with MEM_PRESERVE_PLACEHOLDER gives the placeholder back */
    ok(VirtualFree(Reserved, 2 * PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "split: %lu\n", GetLastError());
    Mapped = pMapViewOfFile3(Mapping, NULL, Reserved, 2 * PAGE, 2 * PAGE, MEM_REPLACE_PLACEHOLDER,
                             PAGE_READONLY, NULL, 0);
    ok(Mapped == Reserved, "map: %p %lu\n", Mapped, GetLastError());
    if (Mapped)
    {
        ok(Mapped[0] == 'C', "view: %c\n", Mapped[0]);
        ok(pUnmapViewOfFile2(GetCurrentProcess(), Mapped, MEM_PRESERVE_PLACEHOLDER), "unmap: %lu\n", GetLastError());
        check_region(Reserved, MEM_RESERVE, MEM_PRIVATE, 2 * PAGE);
        Mapped = pMapViewOfFile3(Mapping, NULL, Reserved, 0, 2 * PAGE, MEM_REPLACE_PLACEHOLDER,
                                 PAGE_READONLY, NULL, 0);
        ok(Mapped == Reserved, "map again: %p %lu\n", Mapped, GetLastError());
        if (Mapped)
        {
            ok(Mapped[0] == 'A', "view: %c\n", Mapped[0]);
            UnmapViewOfFile(Mapped);
        }
    }

    /* Committed memory can become a placeholder again */
    Mapped = pVirtualAlloc2(NULL, Reserved + 2 * PAGE, 2 * PAGE, MEM_COMMIT | MEM_RESERVE | MEM_REPLACE_PLACEHOLDER,
                            PAGE_READWRITE, NULL, 0);
    ok(Mapped == Reserved + 2 * PAGE, "commit: %p %lu\n", Mapped, GetLastError());
    if (Mapped)
    {
        Mapped[0] = 1;
        ok(VirtualFree(Mapped, 2 * PAGE, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER), "back to placeholder: %lu\n", GetLastError());
        check_region(Mapped, MEM_RESERVE, MEM_PRIVATE, 2 * PAGE);
        VirtualFree(Mapped, 0, MEM_RELEASE);
    }
    /* (If mapping again failed, the first half is still a placeholder) */
    VirtualFree(Reserved, 0, MEM_RELEASE);
    check_region(Reserved, MEM_FREE, 0, 0);
}

START_TEST(Placeholders)
{
    WCHAR Path[MAX_PATH];
    HMODULE KernelBase;
    HANDLE File, Mapping;

    KernelBase = LoadLibraryW(L"KernelBase.dll");
    if (KernelBase)
    {
        pVirtualAlloc2 = (PVIRTUALALLOC2)GetProcAddress(KernelBase, "VirtualAlloc2");
        pMapViewOfFile3 = (PMAPVIEWOFFILE3)GetProcAddress(KernelBase, "MapViewOfFile3");
        pUnmapViewOfFile2 = (PUNMAPVIEWOFFILE2)GetProcAddress(KernelBase, "UnmapViewOfFile2");
    }
    if (!pVirtualAlloc2 || !pMapViewOfFile3 || !pUnmapViewOfFile2)
    {
        skip("No VirtualAlloc2 / MapViewOfFile3 / UnmapViewOfFile2\n");
        return;
    }

    File = CreateTestFile(Path);
    ok(File != NULL, "test file: %lu\n", GetLastError());
    if (!File)
        return;
    /* As the .NET runtime does, so that views can be executable */
    Mapping = CreateFileMappingW(File, NULL, PAGE_EXECUTE_READ, 0, 0, NULL);
    ok(Mapping != NULL, "CreateFileMapping: %lu\n", GetLastError());
    if (Mapping)
    {
        Test_CoreclrSequence(Mapping);
        Test_CoalesceAndPreserve(Mapping);
        CloseHandle(Mapping);
    }
    CloseHandle(File);
}
