/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for read-only section handles that cannot be made writable again
 *
 * Chromium's read-only shared memory: an unnamed section with an empty DACL,
 * reduced to FILE_MAP_READ | SECTION_QUERY by DuplicateHandle. Duplicating
 * that handle with FILE_MAP_WRITE must fail (as on Windows 8+): Chromium
 * checks it and refuses to run otherwise.
 */

#include "precomp.h"

START_TEST(ReadOnlySection)
{
    ACL Dacl;
    SECURITY_DESCRIPTOR Sd;
    SECURITY_ATTRIBUTES Sa = { sizeof(Sa), &Sd, FALSE };
    HANDLE hProcess = GetCurrentProcess();
    HANDLE hSection, hReduced = NULL, hReadOnly = NULL, hWritable = NULL;
    PVOID View;
    BOOL Ret;

    ok(InitializeAcl(&Dacl, sizeof(Dacl), ACL_REVISION), "InitializeAcl failed with %lu\n", GetLastError());
    ok(InitializeSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION),
       "InitializeSecurityDescriptor failed with %lu\n", GetLastError());
    ok(SetSecurityDescriptorDacl(&Sd, TRUE, &Dacl, FALSE),
       "SetSecurityDescriptorDacl failed with %lu\n", GetLastError());

    hSection = CreateFileMappingW(INVALID_HANDLE_VALUE, &Sa, PAGE_READWRITE, 0, 65536, NULL);
    ok(hSection != NULL, "CreateFileMapping failed with %lu\n", GetLastError());
    if (!hSection)
        return;

    Ret = DuplicateHandle(hProcess, hSection, hProcess, &hReduced,
                          FILE_MAP_READ | FILE_MAP_WRITE | SECTION_QUERY | READ_CONTROL, FALSE, 0);
    ok(Ret, "Duplicating with read/write access failed with %lu\n", GetLastError());

    if (Ret)
    {
        Ret = DuplicateHandle(hProcess, hReduced, hProcess, &hReadOnly, FILE_MAP_READ | SECTION_QUERY, FALSE, 0);
        ok(Ret, "Duplicating with read-only access failed with %lu\n", GetLastError());
    }

    if (hReadOnly)
    {
        SetLastError(0xdeadbeef);
        Ret = DuplicateHandle(hProcess, hReadOnly, hProcess, &hWritable, FILE_MAP_WRITE, FALSE, 0);
        ok(!Ret, "Duplicating the read-only handle with FILE_MAP_WRITE succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "Error %lu, expected ERROR_ACCESS_DENIED\n", GetLastError());

        View = MapViewOfFile(hReadOnly, FILE_MAP_READ, 0, 0, 0);
        ok(View != NULL, "Mapping the read-only handle for reading failed with %lu\n", GetLastError());
        if (View)
            UnmapViewOfFile(View);
        View = MapViewOfFile(hReadOnly, FILE_MAP_WRITE, 0, 0, 0);
        ok(View == NULL, "Mapping the read-only handle for writing succeeded\n");
        if (View)
            UnmapViewOfFile(View);
    }

    if (hWritable) CloseHandle(hWritable);
    if (hReadOnly) CloseHandle(hReadOnly);
    if (hReduced) CloseHandle(hReduced);
    CloseHandle(hSection);
}
