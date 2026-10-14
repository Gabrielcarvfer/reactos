/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel AddressSanitizer (KASAN) for the pool
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * With the boot option KASAN, small pool blocks get red zones and freed blocks
 * are poisoned and kept in a quarantine for a while (like the user-mode heap,
 * see sdk/lib/rtl/asan.c). Modules built with KASAN_MODULES call
 * __asan_loadN_noabort/__asan_storeN_noabort(address) before each memory
 * access: an access to a red zone or to freed pool is reported.
 *
 * The shadow memory (one byte for 8 bytes of pool) is in chunks of 256 KB,
 * one for each 2 MB of the pool address ranges, allocated from nonpaged pool
 * when a tracked block first lands in that 2 MB. No chunk: nothing to check.
 *
 * Not tracked (given as they are): blocks of a page or more (callers rely on
 * their page alignment), cache-aligned, quota and session pool.
 */

#include <ntoskrnl.h>
#include <reactos/rtlasan.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#define KASAN_CHUNK_SHIFT       21                  /* 2 MB of pool... */
#define KASAN_CHUNK_SIZE        (1 << KASAN_CHUNK_SHIFT)
#define KASAN_SHADOW_CHUNK      (KASAN_CHUNK_SIZE >> ASAN_SHADOW_SCALE) /* ...256 KB of shadow */
#define KASAN_TAG               'nsaK'

/* The raw pool (expool.c) */
PVOID NTAPI ExpAllocatePoolWithTagRaw(IN POOL_TYPE PoolType, IN SIZE_T NumberOfBytes, IN ULONG Tag);
VOID NTAPI ExpFreePoolWithTagRaw(IN PVOID P, IN ULONG TagToFree);

BOOLEAN KasanEnabled = FALSE;

typedef struct _KASAN_RANGE
{
    ULONG_PTR Start, End;
    CHAR **Chunks;
} KASAN_RANGE;

static KASAN_RANGE KasanRanges[2];  /* nonpaged, paged */

typedef struct _KASAN_QUARANTINE
{
    KSPIN_LOCK Lock;
    PASAN_BLOCK_HEADER Blocks[2048];
    ULONG Head, Count;
    SIZE_T Bytes;
} KASAN_QUARANTINE;

static KASAN_QUARANTINE KasanQuarantine[2];  /* nonpaged, paged */

/* SHADOW MEMORY ************************************************************/

static KASAN_RANGE *
KasanGetRange(ULONG_PTR Address)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(KasanRanges); i++)
    {
        if (Address >= KasanRanges[i].Start && Address < KasanRanges[i].End)
            return &KasanRanges[i];
    }
    return NULL;
}

/* The shadow byte of Address; Create: allocate its chunk if needed */
static CHAR *
KasanShadow(ULONG_PTR Address, BOOLEAN Create)
{
    KASAN_RANGE *Range = KasanGetRange(Address);
    ULONG_PTR Index, Offset;
    CHAR *Chunk, *Old;

    if (!Range)
        return NULL;
    Offset = Address - Range->Start;
    Index = Offset >> KASAN_CHUNK_SHIFT;
    Chunk = Range->Chunks[Index];
    if (!Chunk)
    {
        if (!Create)
            return NULL;
        Chunk = ExpAllocatePoolWithTagRaw(NonPagedPool, KASAN_SHADOW_CHUNK, KASAN_TAG);
        if (!Chunk)
            return NULL;
        RtlZeroMemory(Chunk, KASAN_SHADOW_CHUNK);
        Old = InterlockedCompareExchangePointer((PVOID *)&Range->Chunks[Index], Chunk, NULL);
        if (Old)
        {
            ExpFreePoolWithTagRaw(Chunk, KASAN_TAG);
            Chunk = Old;
        }
    }
    return Chunk + ((Offset & (KASAN_CHUNK_SIZE - 1)) >> ASAN_SHADOW_SCALE);
}

static BOOLEAN
KasanPoison(ULONG_PTR Address, SIZE_T Size, CHAR Value)
{
    ULONG_PTR Granule;

    for (Granule = Address; Granule < Address + Size; Granule += ASAN_GRANULE)
    {
        CHAR *Shadow = KasanShadow(Granule, TRUE);
        if (!Shadow)
            return FALSE;
        *Shadow = Value;
    }
    return TRUE;
}

static VOID
KasanUnpoison(ULONG_PTR Address, SIZE_T Size, BOOLEAN Create)
{
    ULONG_PTR Granule, End = Address + Size;

    for (Granule = Address; Granule < End; Granule += ASAN_GRANULE)
    {
        CHAR *Shadow = KasanShadow(Granule, Create);
        if (!Shadow)
            continue;
        *Shadow = (End - Granule >= ASAN_GRANULE) ? 0 : (CHAR)(End - Granule);
    }
}

/* THE BLOCKS ***************************************************************/

static PASAN_BLOCK_HEADER
KasanGetBlock(PVOID Ptr)
{
    PASAN_BLOCK_HEADER Header;

    /* Ours start well into a page (the raw block, then the left red zone) */
    if (!KasanEnabled || ((ULONG_PTR)Ptr & (2 * sizeof(PVOID) - 1)) ||
        BYTE_OFFSET(Ptr) < ASAN_LEFT_REDZONE)
    {
        return NULL;
    }
    Header = (PASAN_BLOCK_HEADER)Ptr - 1;
    if (Header->Magic != ASAN_BLOCK_MAGIC || (PUCHAR)Header->RawBlock + ASAN_LEFT_REDZONE != (PUCHAR)Ptr)
        return NULL;
    return Header;
}

/* Only the direct caller: walking the stack on every allocation is not safe
 * (it reads the headers of driver images, which may not be mapped at that
 * point, at any IRQL) */
static VOID
KasanCaptureStack(PVOID *Stack, PVOID Caller)
{
    RtlZeroMemory(Stack, ASAN_STACK_DEPTH * sizeof(PVOID));
    Stack[0] = Caller;
}

static BOOLEAN
KasanIsTracked(POOL_TYPE PoolType, SIZE_T Size)
{
    return KasanEnabled && Size &&
           Size + ASAN_LEFT_REDZONE + ASAN_RIGHT_REDZONE + 2 * sizeof(POOL_HEADER) < PAGE_SIZE &&
           !(PoolType & (CACHE_ALIGNED_POOL_MASK | QUOTA_POOL_MASK | SESSION_POOL_MASK));
}

/* QUARANTINE ***************************************************************/

static VOID
KasanRelease(PASAN_BLOCK_HEADER Header)
{
    /* Not tracked any more: no false reports when the pool reuses it */
    KasanUnpoison((ULONG_PTR)Header->RawBlock,
                  ASAN_LEFT_REDZONE + ((Header->UserSize + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1)) +
                  ASAN_RIGHT_REDZONE, FALSE);
    ExpFreePoolWithTagRaw(Header->RawBlock, (ULONG)(ULONG_PTR)Header->Heap /* the tag */);
}

static VOID
KasanQuarantinePush(ULONG Index, PASAN_BLOCK_HEADER Header)
{
    KASAN_QUARANTINE *Q = &KasanQuarantine[Index];
    PASAN_BLOCK_HEADER Release[16];
    ULONG Count = 0, i;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Q->Lock, &OldIrql);
    Q->Blocks[(Q->Head + Q->Count) % RTL_NUMBER_OF(Q->Blocks)] = Header;
    Q->Count++;
    Q->Bytes += Header->UserSize;
    while (Count < RTL_NUMBER_OF(Release) &&
           (Q->Count == RTL_NUMBER_OF(Q->Blocks) || Q->Bytes > (Index ? 1 : 4) * 1024 * 1024))
    {
        PASAN_BLOCK_HEADER Old = Q->Blocks[Q->Head];

        Q->Head = (Q->Head + 1) % RTL_NUMBER_OF(Q->Blocks);
        Q->Count--;
        Q->Bytes -= Old->UserSize;
        Release[Count++] = Old;
    }
    KeReleaseSpinLock(&Q->Lock, OldIrql);

    /* (at the IRQL of this free, which suits blocks of the same pool) */
    for (i = 0; i < Count; i++)
        KasanRelease(Release[i]);
}

/* THE POOL WITH KASAN ******************************************************/

PVOID
NTAPI
ExAllocatePoolWithTag(IN POOL_TYPE PoolType,
                      IN SIZE_T NumberOfBytes,
                      IN ULONG Tag)
{
    SIZE_T Rounded, Total;
    PASAN_BLOCK_HEADER Header;
    PUCHAR Raw, User;

    if (!KasanIsTracked(PoolType, NumberOfBytes))
    {
        /* (never poisoned memory: blocks leave the quarantine unpoisoned) */
        return ExpAllocatePoolWithTagRaw(PoolType, NumberOfBytes, Tag);
    }

    Rounded = (NumberOfBytes + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1);
    Total = ASAN_LEFT_REDZONE + Rounded + ASAN_RIGHT_REDZONE;
    Raw = ExpAllocatePoolWithTagRaw(PoolType, Total, Tag);
    if (!Raw)
        return NULL;
    User = Raw + ASAN_LEFT_REDZONE;

    Header = (PASAN_BLOCK_HEADER)User - 1;
    Header->Magic = ASAN_BLOCK_MAGIC;
    Header->State = ASAN_BLOCK_ALLOCATED;
    Header->UserSize = NumberOfBytes;
    Header->RawBlock = Raw;
    Header->Heap = (PVOID)(ULONG_PTR)Tag;  /* (the tag, for the raw free) */
    KasanCaptureStack(Header->AllocStack, _ReturnAddress());
    RtlZeroMemory(Header->FreeStack, sizeof(Header->FreeStack));

    if (!KasanPoison((ULONG_PTR)Raw, ASAN_LEFT_REDZONE, ASAN_HEAP_LEFT_REDZONE) ||
        !KasanPoison((ULONG_PTR)User + Rounded, ASAN_RIGHT_REDZONE, ASAN_HEAP_RIGHT_REDZONE))
    {
        /* no shadow memory: an ordinary block after all */
        KasanUnpoison((ULONG_PTR)Raw, Total, FALSE);
    }
    KasanUnpoison((ULONG_PTR)User, NumberOfBytes, FALSE);
    return User;
}

static VOID KasanPrintAddress(PCSTR Prefix, PVOID Address);
static VOID KasanPrintStack(PVOID *Stack, ULONG Count);

static VOID
KasanFree(PVOID P, ULONG TagToFree, PVOID Caller)
{
    PASAN_BLOCK_HEADER Header = KasanGetBlock(P);
    SIZE_T Rounded;

    if (!Header)
    {
        ExpFreePoolWithTagRaw(P, TagToFree);
        return;
    }
    if (Header->State != ASAN_BLOCK_ALLOCATED)
    {
        PVOID Stack[12];
        ULONG Count;

        DbgPrint("=================================================================\n");
        DbgPrint("KASAN: double free of the %Iu-byte block at %p\n", Header->UserSize, P);
        KasanPrintAddress("    by  ", Caller);
        if (KeGetCurrentIrql() <= APC_LEVEL)
        {
            Count = RtlWalkFrameChain(Stack, RTL_NUMBER_OF(Stack), 0);
            KasanPrintStack(Stack, Count);
        }
        DbgPrint("  allocated by:\n");
        KasanPrintStack(Header->AllocStack, ASAN_STACK_DEPTH);
        DbgPrint("  first freed by:\n");
        KasanPrintStack(Header->FreeStack, ASAN_STACK_DEPTH);
        DbgPrint("=================================================================\n");
        return;
    }
    Header->State = ASAN_BLOCK_FREED;
    KasanCaptureStack(Header->FreeStack, Caller);
    Rounded = (Header->UserSize + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1);
    KasanPoison((ULONG_PTR)P, Rounded, ASAN_HEAP_FREED);
    /* (one quarantine for each pool: blocks are released at the IRQL of a free
     * from the same pool) */
    if (!KasanGetRange((ULONG_PTR)P))
        KasanRelease(Header);
    else
        KasanQuarantinePush((ULONG)(KasanGetRange((ULONG_PTR)P) - KasanRanges), Header);
}

VOID
NTAPI
ExFreePoolWithTag(IN PVOID P,
                  IN ULONG TagToFree)
{
    KasanFree(P, TagToFree, _ReturnAddress());
}

VOID
NTAPI
ExFreePool(IN PVOID P)
{
    /* (without checking the tag) */
    KasanFree(P, 0, _ReturnAddress());
}

/* Is P a block of the KASAN layer (no pool header in front of it)? */
BOOLEAN
NTAPI
KasanIsBlock(IN PVOID P)
{
    return KasanGetBlock(P) != NULL;
}

/* REPORTS ******************************************************************/

static VOID
KasanPrintAddress(PCSTR Prefix, PVOID Address)
{
    PLIST_ENTRY Entry;

    /* (debug output only: no lock) */
    for (Entry = PsLoadedModuleList.Flink; Entry && Entry != &PsLoadedModuleList; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if ((ULONG_PTR)Address >= (ULONG_PTR)Module->DllBase &&
            (ULONG_PTR)Address < (ULONG_PTR)Module->DllBase + Module->SizeOfImage)
        {
            DbgPrint("%s%p %wZ+0x%Ix\n", Prefix, Address, &Module->BaseDllName,
                     (ULONG_PTR)Address - (ULONG_PTR)Module->DllBase);
            return;
        }
    }
    DbgPrint("%s%p\n", Prefix, Address);
}

static VOID
KasanPrintStack(PVOID *Stack, ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count && Stack[i]; i++)
        KasanPrintAddress("      ", Stack[i]);
}

static PASAN_BLOCK_HEADER
KasanFindBlock(ULONG_PTR Address)
{
    ULONG_PTR Granule = Address & ~(ULONG_PTR)(ASAN_GRANULE - 1);
    CHAR *Shadow;
    ULONG Steps;

    Shadow = KasanShadow(Granule, FALSE);
    if (Shadow && *Shadow == ASAN_HEAP_LEFT_REDZONE)
    {
        for (Steps = 0; Steps < ASAN_LEFT_REDZONE / ASAN_GRANULE + 1; Steps++)
        {
            Granule += ASAN_GRANULE;
            Shadow = KasanShadow(Granule, FALSE);
            if (!Shadow || *Shadow != ASAN_HEAP_LEFT_REDZONE)
                break;
        }
        return KasanGetBlock((PVOID)Granule);
    }
    /* (blocks are smaller than a page) */
    for (Steps = 0; Steps < PAGE_SIZE / ASAN_GRANULE; Steps++)
    {
        Shadow = KasanShadow(Granule - ASAN_GRANULE, FALSE);
        if (Shadow && *Shadow == ASAN_HEAP_LEFT_REDZONE)
            return KasanGetBlock((PVOID)Granule);
        Granule -= ASAN_GRANULE;
    }
    return NULL;
}

static VOID
KasanReport(ULONG_PTR Address, SIZE_T Size, BOOLEAN IsWrite, PVOID Pc)
{
    static PVOID Reported[256];
    static LONG InReport;
    PASAN_BLOCK_HEADER Header;
    PVOID Stack[12];
    ULONG Count, i;
    CHAR *ShadowByte = KasanShadow(Address, FALSE);
    CHAR Shadow = ShadowByte ? *ShadowByte : 0;
    PCSTR What;

    for (i = 0; i < RTL_NUMBER_OF(Reported) && Reported[i]; i++)
    {
        if (Reported[i] == Pc)
            return;
    }
    if (InterlockedExchange(&InReport, 1))
        return;
    if (i < RTL_NUMBER_OF(Reported))
        Reported[i] = Pc;

    What = Shadow == ASAN_HEAP_LEFT_REDZONE ? "pool-buffer-underflow" :
           Shadow == ASAN_HEAP_FREED ? "pool-use-after-free" : "pool-buffer-overflow";
    DbgPrint("=================================================================\n");
    DbgPrint("KASAN: %s: %s of %Iu bytes at %p (IRQL %u, thread %p)\n", What,
             IsWrite ? "write" : "read", Size, (PVOID)Address, KeGetCurrentIrql(), PsGetCurrentThread());
    KasanPrintAddress("    at  ", Pc);
    /* (the whole stack only where a page fault is allowed) */
    if (KeGetCurrentIrql() <= APC_LEVEL)
    {
        Count = RtlWalkFrameChain(Stack, RTL_NUMBER_OF(Stack), 0);
        KasanPrintStack(Stack, Count);
    }

    Header = KasanFindBlock(Address);
    if (Header)
    {
        ULONG_PTR User = (ULONG_PTR)(Header + 1);
        LONG_PTR Offset = (LONG_PTR)(Address - User);
        ULONG Tag = (ULONG)(ULONG_PTR)Header->Heap;

        DbgPrint("  The address is %Id bytes %s the %Iu-byte block at %p (tag '%.4s'), allocated by:\n",
                 Offset < 0 ? -Offset : (Offset >= (LONG_PTR)Header->UserSize ? Offset - (LONG_PTR)Header->UserSize : Offset),
                 Offset < 0 ? "before" : (Offset >= (LONG_PTR)Header->UserSize ? "after" : "inside"),
                 Header->UserSize, (PVOID)User, (PCHAR)&Tag);
        KasanPrintStack(Header->AllocStack, ASAN_STACK_DEPTH);
        if (Header->State == ASAN_BLOCK_FREED)
        {
            DbgPrint("  and freed by:\n");
            KasanPrintStack(Header->FreeStack, ASAN_STACK_DEPTH);
        }
    }
    DbgPrint("=================================================================\n");
    InterlockedExchange(&InReport, 0);
}

/* THE CHECKS (called by instrumented code) *********************************/

static __inline BOOLEAN
KasanIsBad(ULONG_PTR Address, SIZE_T Size)
{
    ULONG_PTR Last = Address + Size - 1, Granule;

    if (!KasanEnabled || !Size || Last < Address)
        return FALSE;
    for (Granule = Address & ~(ULONG_PTR)(ASAN_GRANULE - 1); Granule <= Last; Granule += ASAN_GRANULE)
    {
        CHAR *ShadowByte = KasanShadow(Granule, FALSE);
        CHAR Shadow;

        if (!ShadowByte || !(Shadow = *ShadowByte))
            continue;
        if (Shadow < 0 || (LONG)(min(Last, Granule + ASAN_GRANULE - 1) - Granule) >= Shadow)
            return TRUE;
    }
    return FALSE;
}

#define KASAN_CHECK(Size, IsWrite, Name) \
    VOID __cdecl Name(ULONG_PTR Address) \
    { \
        if (KasanIsBad(Address, Size)) \
            KasanReport(Address, Size, IsWrite, _ReturnAddress()); \
    }

KASAN_CHECK(1, FALSE, __asan_load1_noabort)
KASAN_CHECK(2, FALSE, __asan_load2_noabort)
KASAN_CHECK(4, FALSE, __asan_load4_noabort)
KASAN_CHECK(8, FALSE, __asan_load8_noabort)
KASAN_CHECK(16, FALSE, __asan_load16_noabort)
KASAN_CHECK(1, TRUE, __asan_store1_noabort)
KASAN_CHECK(2, TRUE, __asan_store2_noabort)
KASAN_CHECK(4, TRUE, __asan_store4_noabort)
KASAN_CHECK(8, TRUE, __asan_store8_noabort)
KASAN_CHECK(16, TRUE, __asan_store16_noabort)

VOID __cdecl __asan_loadN_noabort(ULONG_PTR Address, SIZE_T Size)
{
    if (KasanIsBad(Address, Size))
        KasanReport(Address, Size, FALSE, _ReturnAddress());
}

VOID __cdecl __asan_storeN_noabort(ULONG_PTR Address, SIZE_T Size)
{
    if (KasanIsBad(Address, Size))
        KasanReport(Address, Size, TRUE, _ReturnAddress());
}

VOID __cdecl __asan_handle_no_return(VOID)
{
}

/* INITIALIZATION ***********************************************************/

/* Phase 1, with the boot option KASAN */
VOID
NTAPI
KasanInitialize(VOID)
{
    ULONG i;

    KasanRanges[0].Start = (ULONG_PTR)MmNonPagedPoolStart;
    KasanRanges[0].End = (ULONG_PTR)MmNonPagedPoolEnd;
    KasanRanges[1].Start = (ULONG_PTR)MmPagedPoolStart;
    KasanRanges[1].End = (ULONG_PTR)MmPagedPoolEnd + 1;

    for (i = 0; i < RTL_NUMBER_OF(KasanRanges); i++)
    {
        SIZE_T Size = (((KasanRanges[i].End - KasanRanges[i].Start) >> KASAN_CHUNK_SHIFT) + 1) * sizeof(CHAR *);

        KasanRanges[i].Chunks = ExpAllocatePoolWithTagRaw(NonPagedPool, Size, KASAN_TAG);
        if (!KasanRanges[i].Chunks)
            return;
        RtlZeroMemory(KasanRanges[i].Chunks, Size);
        KeInitializeSpinLock(&KasanQuarantine[i].Lock);
    }

    KasanEnabled = TRUE;
    DbgPrint("KASAN: on: nonpaged pool %p-%p, paged pool %p-%p\n",
             (PVOID)KasanRanges[0].Start, (PVOID)KasanRanges[0].End,
             (PVOID)KasanRanges[1].Start, (PVOID)KasanRanges[1].End);
}
