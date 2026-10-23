/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     More than 1 GB of cache views at once
 *
 * Every open file the cache manager writes to gets a 256 KB view of kernel
 * address space. Keep thousands of written files open, so that the views
 * need more than 1 GB, and write to all of them again. 64-bit ReactOS placed
 * the views above KUSER_SHARED_DATA, where nobody created the page directory
 * beyond the first GB: writing to such a view crashed with
 * PAGE_FAULT_IN_NONPAGED_AREA (on a page table address), seen when Chrome's
 * updater wrote its files after a long session.
 */

#include "precomp.h"

#define FILES 6000

START_TEST(ManyCachedFiles)
{
    char temp[MAX_PATH], dir[MAX_PATH], name[MAX_PATH];
    static HANDLE Handles[FILES];
    DWORD w, i, Opened = 0, Written = 0;
    BYTE data[64];

    GetTempPathA(MAX_PATH, temp);
    StringCchPrintfA(dir, MAX_PATH, "%smanycached_apitest", temp);
    CreateDirectoryA(dir, NULL);
    memset(data, 0x5A, sizeof(data));

    for (i = 0; i < FILES; i++)
    {
        StringCchPrintfA(name, MAX_PATH, "%s\\f%04lu.bin", dir, i);
        Handles[i] = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (Handles[i] == INVALID_HANDLE_VALUE)
        {
            skip("Cannot create file %lu: %lu\n", i, GetLastError());
            break;
        }
        Opened++;
        if (WriteFile(Handles[i], data, sizeof(data), &w, NULL) && w == sizeof(data))
            Written++;
    }
    ok(Written == Opened, "%lu of %lu files written\n", Written, Opened);

    /* Once more, with all the views alive */
    Written = 0;
    for (i = 0; i < Opened; i++)
    {
        SetFilePointer(Handles[i], 4096, NULL, FILE_BEGIN);
        if (WriteFile(Handles[i], data, sizeof(data), &w, NULL) && w == sizeof(data))
            Written++;
    }
    ok(Written == Opened, "%lu of %lu files written again\n", Written, Opened);
    trace("%lu files open at once\n", Opened);

    for (i = 0; i < Opened; i++)
        CloseHandle(Handles[i]);
    ok(RemoveDirectoryA(dir), "RemoveDirectory: %lu\n", GetLastError());
}
