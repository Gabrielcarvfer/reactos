/*
 * PROJECT:   Registry manipulation library
 * LICENSE:   GPL - See COPYING in the top level directory
 * COPYRIGHT: Copyright 2005 Filip Navara <navaraf@reactos.org>
 *            Copyright 2005 Hartmut Birr
 *            Copyright 2001 - 2005 Eric Kohl
 */

#include "cmlib.h"

/*
 * A block list is allocated with this header in front of it: its capacity
 * (it grows by doubling) and the list it replaced, which is only freed with
 * the hive (see HvpAddBin).
 */
typedef struct _HMAP_BLOCK_LIST_HEADER
{
    PHMAP_ENTRY Previous;
    SIZE_T Capacity;
} HMAP_BLOCK_LIST_HEADER, *PHMAP_BLOCK_LIST_HEADER;

#define HvpBlockListHeader(List) ((PHMAP_BLOCK_LIST_HEADER)(List) - 1)

#if defined(_MSC_VER)
#define HvpCompilerBarrier() _ReadWriteBarrier()
#else
#define HvpCompilerBarrier() __asm__ __volatile__("" ::: "memory")
#endif

PHMAP_ENTRY CMAPI
HvpAllocateBlockList(
    _In_ PHHIVE Hive,
    _In_ ULONG Capacity,
    _In_ BOOLEAN Paged)
{
    PHMAP_BLOCK_LIST_HEADER Header;

    Header = Hive->Allocate(sizeof(*Header) + Capacity * sizeof(HMAP_ENTRY), Paged, TAG_CM);
    if (Header == NULL)
        return NULL;
    Header->Previous = NULL;
    Header->Capacity = Capacity;
    return (PHMAP_ENTRY)(Header + 1);
}

VOID CMAPI
HvpFreeBlockList(
    _In_ PHHIVE Hive,
    _In_opt_ PHMAP_ENTRY BlockList)
{
    PHMAP_ENTRY Previous;

    while (BlockList != NULL)
    {
        Previous = HvpBlockListHeader(BlockList)->Previous;
        Hive->Free(HvpBlockListHeader(BlockList), 0);
        BlockList = Previous;
    }
}

PHBIN CMAPI
HvpAddBin(
    PHHIVE RegistryHive,
    ULONG Size,
    HSTORAGE_TYPE Storage)
{
    PHMAP_ENTRY BlockList;
    PHBIN Bin;
    ULONG BinSize;
    ULONG i;
    ULONG BitmapSize;
    ULONG BlockCount;
    ULONG OldBlockListSize;
    PHCELL Block;

    BinSize = ROUND_UP(Size + sizeof(HBIN), HBLOCK_SIZE);
    BlockCount = BinSize / HBLOCK_SIZE;

    Bin = RegistryHive->Allocate(BinSize, TRUE, TAG_CM);
    if (Bin == NULL)
        return NULL;
    RtlZeroMemory(Bin, BinSize);

    Bin->Signature = HV_HBIN_SIGNATURE;
    Bin->FileOffset = RegistryHive->Storage[Storage].Length *
                      HBLOCK_SIZE;
    Bin->Size = BinSize;

    OldBlockListSize = RegistryHive->Storage[Storage].Length;
    BlockList = RegistryHive->Storage[Storage].BlockList;
    if (!BlockList ||
        HvpBlockListHeader(BlockList)->Capacity < OldBlockListSize + BlockCount)
    {
        /* Grow the block list (doubling it, so that this is rare) */
        BlockList = HvpAllocateBlockList(RegistryHive,
                                         (OldBlockListSize + BlockCount > 2 * OldBlockListSize) ?
                                             OldBlockListSize + BlockCount : 2 * OldBlockListSize,
                                         TRUE);
        if (BlockList == NULL)
        {
            RegistryHive->Free(Bin, 0);
            return NULL;
        }

        if (OldBlockListSize > 0)
        {
            RtlCopyMemory(BlockList, RegistryHive->Storage[Storage].BlockList,
                          OldBlockListSize * sizeof(HMAP_ENTRY));
        }

        /*
         * Other threads read cells without the hive lock (HvGetCell under a
         * shared registry lock), and one of them may still use the old list:
         * keep it until the hive goes away (freeing it here made such a
         * reader fault in freed paged pool on 4 processors).
         */
        HvpBlockListHeader(BlockList)->Previous = RegistryHive->Storage[Storage].BlockList;
    }

    /* Fill in the new entries before anybody can use them */
    for (i = 0; i < BlockCount; i++)
    {
        BlockList[OldBlockListSize + i].BlockAddress = ((ULONG_PTR)Bin + (i * HBLOCK_SIZE));
        BlockList[OldBlockListSize + i].BinAddress = (ULONG_PTR)Bin;
    }

    HvpCompilerBarrier();
    RegistryHive->Storage[Storage].BlockList = BlockList;
    RegistryHive->Storage[Storage].Length += BlockCount;

    /* Initialize a free block in this heap. */
    Block = (PHCELL)(Bin + 1);
    Block->Size = (LONG)(BinSize - sizeof(HBIN));

    if (Storage == Stable)
    {
        /* Calculate bitmap size in bytes (always a multiple of 32 bits). */
        BitmapSize = ROUND_UP(RegistryHive->Storage[Stable].Length,
                              sizeof(ULONG) * 8) / 8;

        /* Grow bitmap if necessary. */
        if (BitmapSize > RegistryHive->DirtyVector.SizeOfBitMap / 8)
        {
            PULONG BitmapBuffer;

            BitmapBuffer = RegistryHive->Allocate(BitmapSize, TRUE, TAG_CM);
            RtlZeroMemory(BitmapBuffer, BitmapSize);
            if (RegistryHive->DirtyVector.SizeOfBitMap > 0)
            {
                ASSERT(RegistryHive->DirtyVector.Buffer);
                RtlCopyMemory(BitmapBuffer,
                              RegistryHive->DirtyVector.Buffer,
                              RegistryHive->DirtyVector.SizeOfBitMap / 8);
                RegistryHive->Free(RegistryHive->DirtyVector.Buffer, 0);
            }
            RtlInitializeBitMap(&RegistryHive->DirtyVector, BitmapBuffer,
                                BitmapSize * 8);
        }

        /* Mark new bin dirty. */
        RtlSetBits(&RegistryHive->DirtyVector,
                   Bin->FileOffset / HBLOCK_SIZE,
                   BlockCount);

        /* Update size in the base block */
        RegistryHive->BaseBlock->Length += BinSize;
    }

    return Bin;
}
