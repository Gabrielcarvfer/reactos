/*
 * Copyright 2017 Giannis Adamopoulos
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

#include "precomp.h"

HANDLE _CreateActCtxFromFile(LPCWSTR FileName, int line);
VOID _ActivateCtx(HANDLE h, ULONG_PTR *cookie, int line);
VOID _DeactivateCtx(ULONG_PTR cookie, int line);

typedef DWORD (WINAPI *LPGETVERSION)();

VOID _TestVesion(HANDLE dll, DWORD ExpectedVersion, int line)
{
    LPGETVERSION proc = (LPGETVERSION)GetProcAddress(dll, "GetVersion");
    DWORD version = proc();
    ok_(__FILE__, line)(version == ExpectedVersion, "Got version %lu, expected %lu\n", version, ExpectedVersion);
}

VOID TestDllRedirection()
{
    HANDLE dll1, dll2, h;
    ULONG_PTR cookie;

    /* Try to load the libraries without sxs */
    dll1 = LoadLibraryExW(L"kernel32test_versioned.dll",0 , 0);
    ok (dll1 != NULL, "LoadLibraryExW failed\n");
    dll2 = LoadLibraryExW(L"testdata\\kernel32test_versioned.dll",0 , 0);
    ok (dll2 != NULL, "LoadLibraryExW failed\n");

    ok (dll1 != dll2, "\n");
    _TestVesion(dll1, 1, __LINE__);
    _TestVesion(dll2, 2, __LINE__);

    FreeLibrary(dll1);
    FreeLibrary(dll2);

    dll1 = LoadLibraryExW(L"kernel32test_versioned.dll",0 , 0);
    ok (dll1 != NULL, "LoadLibraryExW failed\n");

    /* redir2dep.manifest defines an assembly with nothing but a dependency on redirtest2 assembly */
    /* redirtest2.manifest defines an assembly that contains kernel32test_versioned.dll */
    /* In win10 it is enought to load and activate redirtest2 */
    /* In win2k3 however the only way to trigger the redirection is to load and activate redir2dep */
    h = _CreateActCtxFromFile(L"testdata\\redir2dep.manifest", __LINE__);
    _ActivateCtx(h, &cookie, __LINE__);
    dll2 = LoadLibraryExW(L"kernel32test_versioned.dll",0 , 0);
    _DeactivateCtx(cookie, __LINE__);
    ok (dll2 != NULL, "LoadLibraryExW failed\n");

    ok (dll1 != dll2, "\n");
    _TestVesion(dll1, 1, __LINE__);
    _TestVesion(dll2, 2, __LINE__);

    FreeLibrary(dll1);
    FreeLibrary(dll2);

    dll1 = LoadLibraryExW(L"comctl32.dll",0 ,0);
    ok (dll1 != NULL, "LoadLibraryExW failed\n");

    h = _CreateActCtxFromFile(L"comctl32dep.manifest", __LINE__);
    _ActivateCtx(h, &cookie, __LINE__);
    dll2 = LoadLibraryExW(L"comctl32.dll",0 , 0);
    _DeactivateCtx(cookie, __LINE__);
    ok (dll2 != NULL, "LoadLibraryExW failed\n");

    ok (dll1 != dll2, "\n");

}

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

/* LOAD_LIBRARY_SEARCH_SYSTEM32 alone looks in System32 only: not in the
 * program's folder, nor in the folders of AddDllDirectory. The .NET runtime
 * looks for Windows' ICU this way and uses all of it when it is found. */
static VOID TestSearchSystem32(VOID)
{
    WCHAR SysDll[MAX_PATH], Copy[MAX_PATH], *Slash;
    PVOID (WINAPI *pAddDllDirectory)(PCWSTR);
    BOOL (WINAPI *pRemoveDllDirectory)(PVOID);
    HMODULE hMod, hIcu;
    PVOID Cookie = NULL;

    hMod = LoadLibraryExW(L"kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ok(hMod == GetModuleHandleW(L"kernel32.dll"), "kernel32: %p\n", hMod);
    hMod = LoadLibraryExW(L"version.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ok(hMod != NULL, "version.dll from System32: error %lu\n", GetLastError());
    if (hMod) FreeLibrary(hMod);

    /* A DLL only in the program's folder */
    GetSystemDirectoryW(SysDll, MAX_PATH);
    wcscat(SysDll, L"\\version.dll");
    GetModuleFileNameW(NULL, Copy, MAX_PATH);
    Slash = wcsrchr(Copy, L'\\');
    if (!Slash) { skip("no program folder\n"); return; }
    wcscpy(Slash + 1, L"sys32only_test.dll");
    if (!CopyFileW(SysDll, Copy, FALSE))
    {
        skip("cannot copy to the program's folder: %lu\n", GetLastError());
        return;
    }

    hMod = LoadLibraryExW(L"sys32only_test.dll", NULL, 0);
    ok(hMod != NULL, "program folder, usual search: error %lu\n", GetLastError());
    if (hMod) FreeLibrary(hMod);

    SetLastError(0xdeadbeef);
    hMod = LoadLibraryExW(L"sys32only_test.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ok(hMod == NULL, "found outside System32: %p\n", hMod);
    ok(GetLastError() == ERROR_MOD_NOT_FOUND, "error %lu\n", GetLastError());
    if (hMod) FreeLibrary(hMod);

    /* Nor in a folder added with AddDllDirectory */
    pAddDllDirectory = (PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory");
    pRemoveDllDirectory = (PVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "RemoveDllDirectory");
    if (pAddDllDirectory && pRemoveDllDirectory)
    {
        *Slash = UNICODE_NULL;
        Cookie = pAddDllDirectory(Copy);
        ok(Cookie != NULL, "AddDllDirectory: error %lu\n", GetLastError());
        hMod = LoadLibraryExW(L"sys32only_test.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        ok(hMod == NULL, "found in an added folder: %p\n", hMod);
        if (hMod) FreeLibrary(hMod);
        if (Cookie) pRemoveDllDirectory(Cookie);
        wcscpy(Slash, L"\\sys32only_test.dll");
    }
    DeleteFileW(Copy);

    /* An icu.dll in System32 must be all of ICU: .NET then uses it and aborts
     * on the first function it lacks */
    hIcu = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hIcu)
    {
        ok(GetProcAddress(hIcu, "u_charsToUChars") != NULL, "icu.dll in System32 without u_charsToUChars\n");
        ok(GetProcAddress(hIcu, "ucol_open") != NULL, "icu.dll in System32 without ucol_open\n");
        FreeLibrary(hIcu);
    }
}

START_TEST(LoadLibraryExW)
{
    /* (first: the redirection tests need their test data) */
    TestSearchSystem32();
    TestDllRedirection();
}
