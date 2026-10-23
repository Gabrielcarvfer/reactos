/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FlushFileBuffers on a FAT volume must write the FAT
 *
 * Allocate (and free) clusters, flush the volume, and read the FAT from the
 * disk right away: its free entries must be the free space the driver
 * reports. FastFAT flushes the FAT by pinning each FAT page with PIN_IF_BCB,
 * i.e. only the pages that have pinned changes the cache hasn't written yet.
 * The ReactOS cache manager forgot those as soon as the pin was released, so
 * the flush wrote nothing and the FAT only reached the disk when the lazy
 * writer came by.
 */

#include "precomp.h"
#include <stdio.h>
#include "fatdisk.h"

#define ROUNDS 10

/* The FAT on the disk right after a flush; one more flush and read if
 * someone else changed the volume in between */
static BOOL FatMatchesAfterFlush(HANDLE Volume, const char *VolumeRoot, const FATGEOMETRY *g, int round, const char *what)
{
    DWORD spc, bps, free1, free2, total;
    LONG ondisk = -1;
    int attempt;

    for (attempt = 0; attempt < 2; attempt++)
    {
        if (!FlushFileBuffers(Volume))
        {
            trace("FlushFileBuffers: %lu\n", GetLastError());
            return FALSE;
        }
        GetDiskFreeSpaceA(VolumeRoot, &spc, &bps, &free1, &total);
        ondisk = CountFreeOnDisk(Volume, g);
        GetDiskFreeSpaceA(VolumeRoot, &spc, &bps, &free2, &total);
        if (free1 == free2 && ondisk == (LONG)free1)
            return TRUE;
    }
    trace("round %d, %s: the driver says %lu free clusters, the FAT on the disk %ld\n",
          round, what, free1, ondisk);
    return FALSE;
}

START_TEST(FatFlushVolume)
{
    char VolumeRoot[4] = "C:\\", temp[MAX_PATH], name[MAX_PATH];
    HANDLE Volume, h;
    FATGEOMETRY g;
    BYTE *buf;
    DWORD w;
    int round, Bad = 0;

    GetTempPathA(MAX_PATH, temp);
    VolumeRoot[0] = temp[0];
    Volume = OpenFatVolume(VolumeRoot, &g);
    if (Volume == INVALID_HANDLE_VALUE)
        return;

    buf = VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    if (!buf)
    {
        skip("No memory\n");
        CloseHandle(Volume);
        return;
    }
    memset(buf, 0xA5, 65536);

    for (round = 0; round < ROUNDS; round++)
    {
        int i;

        StringCchPrintfA(name, MAX_PATH, "%sfatflush_apitest_%d.bin", temp, round);
        h = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok(h != INVALID_HANDLE_VALUE, "create %s: %lu\n", name, GetLastError());
        if (h == INVALID_HANDLE_VALUE)
            break;
        for (i = 0; i < 4 * (round + 1); i++)
            WriteFile(h, buf, 65536, &w, NULL);
        CloseHandle(h);
        if (!FatMatchesAfterFlush(Volume, VolumeRoot, &g, round, "allocated"))
            Bad++;

        DeleteFileA(name);
        if (!FatMatchesAfterFlush(Volume, VolumeRoot, &g, round, "freed"))
            Bad++;
    }
    ok(Bad == 0, "%d of %d flushes left the FAT on the disk behind\n", Bad, 2 * ROUNDS);

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(Volume);
}
