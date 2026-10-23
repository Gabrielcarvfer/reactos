/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Read the FAT of a mounted FAT volume from the disk (FatFreeClusters)
 */

#pragma once

typedef struct
{
    ULONG BytesPerSector;
    ULONG FatOffset;        /* bytes */
    ULONG FatBytes;
    ULONG Clusters;
    ULONG Bits;             /* 12, 16 or 32 */
} FATGEOMETRY;

static BOOL ReadAt(HANDLE Volume, ULONGLONG Offset, PVOID Buffer, ULONG Length)
{
    LARGE_INTEGER li;
    DWORD r;
    li.QuadPart = Offset;
    if (!SetFilePointerEx(Volume, li, NULL, FILE_BEGIN))
        return FALSE;
    return ReadFile(Volume, Buffer, Length, &r, NULL) && r == Length;
}

static BOOL GetGeometry(HANDLE Volume, FATGEOMETRY *g)
{
    BYTE *b = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    ULONG spc, reserved, fats, rootents, fatsz, totsec, datasec;
    BOOL ret = FALSE;

    if (!b)
        return FALSE;
    if (ReadAt(Volume, 0, b, 4096))
    {
        g->BytesPerSector = *(USHORT *)(b + 11);
        spc = b[13];
        reserved = *(USHORT *)(b + 14);
        fats = b[16];
        rootents = *(USHORT *)(b + 17);
        totsec = *(USHORT *)(b + 19);
        if (!totsec)
            totsec = *(ULONG *)(b + 32);
        fatsz = *(USHORT *)(b + 22);
        if (!fatsz)
            fatsz = *(ULONG *)(b + 36);
        if (g->BytesPerSector && spc && fats)
        {
            datasec = totsec - (reserved + fats * fatsz +
                                (rootents * 32 + g->BytesPerSector - 1) / g->BytesPerSector);
            g->Clusters = datasec / spc;
            g->Bits = g->Clusters < 4085 ? 12 : g->Clusters < 65525 ? 16 : 32;
            g->FatOffset = reserved * g->BytesPerSector;
            g->FatBytes = fatsz * g->BytesPerSector;
            /* (Like FastFAT: no more clusters than the FAT describes) */
            g->Clusters = min(g->Clusters, (ULONG)((ULONGLONG)g->FatBytes * 8 / g->Bits) - 2);
            ret = TRUE;
        }
    }
    VirtualFree(b, 0, MEM_RELEASE);
    return ret;
}

/* Free entries in the first FAT on the disk */
static LONG CountFreeOnDisk(HANDLE Volume, const FATGEOMETRY *g)
{
    BYTE *fat = VirtualAlloc(NULL, g->FatBytes, MEM_COMMIT, PAGE_READWRITE);
    ULONG i, off, chunk;
    LONG Free = 0;

    if (!fat)
        return -1;
    for (off = 0; off < g->FatBytes; off += chunk)
    {
        chunk = min(g->FatBytes - off, 1024 * 1024);
        if (!ReadAt(Volume, (ULONGLONG)g->FatOffset + off, fat + off, chunk))
        {
            VirtualFree(fat, 0, MEM_RELEASE);
            return -1;
        }
    }
    for (i = 2; i < g->Clusters + 2; i++)
    {
        ULONG e;
        if (g->Bits == 32)
            e = ((ULONG *)fat)[i] & 0x0FFFFFFF;
        else if (g->Bits == 16)
            e = ((USHORT *)fat)[i];
        else
            e = (*(USHORT *)(fat + i * 3 / 2) >> ((i & 1) * 4)) & 0xFFF;
        if (e == 0)
            Free++;
    }
    VirtualFree(fat, 0, MEM_RELEASE);
    return Free;
}

/* Opens the volume of VolumeRoot ("C:\\") for raw reads (and flushes) when it is a
 * FAT volume; INVALID_HANDLE_VALUE (after a skip) otherwise */
static HANDLE OpenFatVolume(const char *VolumeRoot, FATGEOMETRY *g)
{
    char fs[32], dev[8];
    HANDLE Volume;

    if (!GetVolumeInformationA(VolumeRoot, NULL, 0, NULL, NULL, NULL, fs, sizeof(fs)) ||
        _strnicmp(fs, "FAT", 3))
    {
        skip("%s is not a FAT volume (%s)\n", VolumeRoot, fs);
        return INVALID_HANDLE_VALUE;
    }
    StringCchPrintfA(dev, sizeof(dev), "\\\\.\\%c:", VolumeRoot[0]);
    Volume = CreateFileA(dev, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (Volume == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open %s: %lu\n", dev, GetLastError());
        return INVALID_HANDLE_VALUE;
    }
    if (!GetGeometry(Volume, g))
    {
        skip("Cannot read the boot sector of %s\n", dev);
        CloseHandle(Volume);
        return INVALID_HANDLE_VALUE;
    }
    trace("FAT%lu, %lu clusters, FAT at %lu, %lu bytes\n", g->Bits, g->Clusters, g->FatOffset, g->FatBytes);
    return Volume;
}
