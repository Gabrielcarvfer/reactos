/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AddressSanitizer: shadow memory and the heap in ASan mode
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * In ASan mode (only in user mode, see dll/ntdll/rtl/asan.c), every heap
 * block gets red zones: a left one (which ends with ASAN_BLOCK_HEADER) and a
 * right one, poisoned in the shadow memory. A freed block is poisoned and
 * kept in a quarantine for a while, so that a use after free is caught too.
 *
 *  raw block:  | left red zone ... header | user data | right red zone |
 *                                          ^ the pointer the program gets
 *
 * When ASan is off (always in the kernel), these functions only call the
 * heap below.
 */

#include <rtl.h>
#include <reactos/rtlasan.h>

#define NDEBUG
#include <debug.h>

BOOLEAN RtlpAsanEnabled = FALSE;
ULONG RtlpAsanFlags = 0;
ULONG_PTR RtlpAsanShadowBase = 0;
ULONG_PTR RtlpAsanShadowLimit = 0;
PRTLP_ASAN_REPORT RtlpAsanReport = NULL;
BOOLEAN RtlpAsanCaptureStacks = FALSE;

/* SHADOW MEMORY ************************************************************/

VOID
NTAPI
RtlpAsanPoison(ULONG_PTR Address, SIZE_T Size, CHAR Value)
{
    /* (whole granules: the red zones and blocks are aligned) */
    if (!RtlpAsanEnabled || Address + Size > RtlpAsanShadowLimit)
        return;
    RtlFillMemory(RtlpAsanShadow(Address), (Size + ASAN_GRANULE - 1) >> ASAN_SHADOW_SCALE, Value);
}

VOID
NTAPI
RtlpAsanUnpoison(ULONG_PTR Address, SIZE_T Size)
{
    SIZE_T Whole = Size & ~(SIZE_T)(ASAN_GRANULE - 1);

    if (!RtlpAsanEnabled || Address + Size > RtlpAsanShadowLimit)
        return;
    RtlZeroMemory(RtlpAsanShadow(Address), Whole >> ASAN_SHADOW_SCALE);
    /* the last, partial granule: how many of its bytes may be used */
    if (Size & (ASAN_GRANULE - 1))
        *RtlpAsanShadow(Address + Whole) = (CHAR)(Size & (ASAN_GRANULE - 1));
}

/* THE BLOCKS ***************************************************************/

/* The header of an ASan block, or NULL if Ptr is not one (a block from
 * before ASan was turned on, from HeapWalk...) */
PASAN_BLOCK_HEADER
NTAPI
RtlpAsanGetBlock(PVOID Ptr)
{
    PASAN_BLOCK_HEADER Header;

    /* (heap blocks are aligned on 8 bytes in 32-bit code, 16 in 64-bit) */
    if (!Ptr || ((ULONG_PTR)Ptr & (2 * sizeof(PVOID) - 1)) || (ULONG_PTR)Ptr < ASAN_LEFT_REDZONE)
        return NULL;

    Header = (PASAN_BLOCK_HEADER)Ptr - 1;
    _SEH2_TRY
    {
        if (Header->Magic != ASAN_BLOCK_MAGIC ||
            (PUCHAR)Header->RawBlock + ASAN_LEFT_REDZONE != (PUCHAR)Ptr)
        {
            Header = NULL;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Header = NULL;
    }
    _SEH2_END;
    return Header;
}

/* What the heap knows a block by (RtlGetUserInfoHeap, RtlValidateHeap...) */
PVOID
NTAPI
RtlpAsanToRawPointer(PVOID Ptr)
{
    PASAN_BLOCK_HEADER Header;

    if (!RtlpAsanEnabled || !(Header = RtlpAsanGetBlock(Ptr)))
        return Ptr;
    return Header->RawBlock;
}

static VOID
RtlpAsanCaptureStack(PVOID *Stack)
{
    RtlZeroMemory(Stack, ASAN_STACK_DEPTH * sizeof(PVOID));
    /* (not while the process starts: walking the first frames is not safe) */
    if (RtlpAsanCaptureStacks)
        RtlCaptureStackBackTrace(2, ASAN_STACK_DEPTH, Stack, NULL);
}

/* QUARANTINE ***************************************************************/

/* Freed blocks wait here (poisoned) before they really go back to the heap */
#define ASAN_QUARANTINE_BLOCKS      4096
#define ASAN_QUARANTINE_BYTES       (16 * 1024 * 1024)

static PASAN_BLOCK_HEADER RtlpAsanQuarantine[ASAN_QUARANTINE_BLOCKS];
static ULONG RtlpAsanQuarantineHead, RtlpAsanQuarantineCount;
static SIZE_T RtlpAsanQuarantineBytes;
static LONG RtlpAsanQuarantineLock;

static VOID
RtlpAsanReleaseBlock(PASAN_BLOCK_HEADER Header)
{
    /* (stays poisoned as freed until it is allocated again) */
    RtlpFreeHeapRaw(Header->Heap, 0, Header->RawBlock);
}

static VOID
RtlpAsanQuarantinePush(PASAN_BLOCK_HEADER Header)
{
    PASAN_BLOCK_HEADER Release[16];
    ULONG Count = 0, i;

    if (RtlpAsanFlags & ASAN_FLAG_NO_QUARANTINE)
    {
        RtlpAsanReleaseBlock(Header);
        return;
    }

    while (InterlockedCompareExchange(&RtlpAsanQuarantineLock, 1, 0))
        YieldProcessor();

    RtlpAsanQuarantine[(RtlpAsanQuarantineHead + RtlpAsanQuarantineCount) % ASAN_QUARANTINE_BLOCKS] = Header;
    RtlpAsanQuarantineCount++;
    RtlpAsanQuarantineBytes += Header->UserSize;

    /* The oldest ones go back, outside the lock */
    while (Count < ARRAYSIZE(Release) &&
           (RtlpAsanQuarantineCount == ASAN_QUARANTINE_BLOCKS ||
            RtlpAsanQuarantineBytes > ASAN_QUARANTINE_BYTES))
    {
        PASAN_BLOCK_HEADER Old = RtlpAsanQuarantine[RtlpAsanQuarantineHead];

        RtlpAsanQuarantineHead = (RtlpAsanQuarantineHead + 1) % ASAN_QUARANTINE_BLOCKS;
        RtlpAsanQuarantineCount--;
        RtlpAsanQuarantineBytes -= Old->UserSize;
        Release[Count++] = Old;
    }

    InterlockedExchange(&RtlpAsanQuarantineLock, 0);

    for (i = 0; i < Count; i++)
        RtlpAsanReleaseBlock(Release[i]);
}

/* THE HEAP IN ASAN MODE ****************************************************/

static PVOID
RtlpAsanAllocate(PVOID Heap, ULONG Flags, SIZE_T Size)
{
    SIZE_T Rounded = (Size + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1);
    SIZE_T Total = ASAN_LEFT_REDZONE + Rounded + ASAN_RIGHT_REDZONE;
    PASAN_BLOCK_HEADER Header;
    PUCHAR Raw, User;

    if (Total < Size)
        return NULL;
    Raw = RtlpAllocateHeapRaw(Heap, Flags, Total);
    if (!Raw)
        return NULL;
    User = Raw + ASAN_LEFT_REDZONE;

    Header = (PASAN_BLOCK_HEADER)User - 1;
    Header->Magic = ASAN_BLOCK_MAGIC;
    Header->State = ASAN_BLOCK_ALLOCATED;
    Header->UserSize = Size;
    Header->RawBlock = Raw;
    Header->Heap = Heap;
    RtlpAsanCaptureStack(Header->AllocStack);
    RtlZeroMemory(Header->FreeStack, sizeof(Header->FreeStack));

    RtlpAsanPoison((ULONG_PTR)Raw, ASAN_LEFT_REDZONE, ASAN_HEAP_LEFT_REDZONE);
    RtlpAsanUnpoison((ULONG_PTR)User, Size);
    RtlpAsanPoison((ULONG_PTR)User + Rounded, ASAN_RIGHT_REDZONE, ASAN_HEAP_RIGHT_REDZONE);
    if (Size & (ASAN_GRANULE - 1))
    {
        /* (the right red zone starts in the middle of the last granule) */
    }
    return User;
}

static BOOLEAN
RtlpAsanFree(PASAN_BLOCK_HEADER Header, PVOID Ptr)
{
    SIZE_T Rounded = (Header->UserSize + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1);

    if (Header->State != ASAN_BLOCK_ALLOCATED)
    {
        if (RtlpAsanReport)
            RtlpAsanReport((ULONG_PTR)Ptr, 0, TRUE, _ReturnAddress(), "double free");
        return FALSE;
    }
    Header->State = ASAN_BLOCK_FREED;
    RtlpAsanCaptureStack(Header->FreeStack);
    RtlpAsanPoison((ULONG_PTR)Ptr, Rounded, ASAN_HEAP_FREED);
    RtlpAsanQuarantinePush(Header);
    return TRUE;
}

/* PUBLIC FUNCTIONS (the heap itself is in heap.c) **************************/

/* The heap called back from inside the ASan layer itself (capturing a stack
 * can allocate): those blocks are plain ones. (A spare byte of the TEB, set
 * while in the layer) */
#if (NTDDI_VERSION >= NTDDI_WIN7)
#define ASAN_IN_LAYER()     (NtCurrentTeb()->SpareBytes[0])
#else
#define ASAN_IN_LAYER()     (NtCurrentTeb()->SpareBytes1[0])
#endif

PVOID
NTAPI
RtlAllocateHeap(IN PVOID HeapPtr,
                IN ULONG Flags,
                IN SIZE_T Size)
{
    PVOID Ptr;

    if (!RtlpAsanEnabled || ASAN_IN_LAYER())
        return RtlpAllocateHeapRaw(HeapPtr, Flags, Size);
    ASAN_IN_LAYER() = 1;
    Ptr = RtlpAsanAllocate(HeapPtr, Flags, Size);
    ASAN_IN_LAYER() = 0;
    return Ptr;
}

BOOLEAN
NTAPI
RtlFreeHeap(HANDLE HeapPtr,
            ULONG Flags,
            PVOID Ptr)
{
    PASAN_BLOCK_HEADER Header;

    if (!RtlpAsanEnabled || !(Header = RtlpAsanGetBlock(Ptr)))
        return RtlpFreeHeapRaw(HeapPtr, Flags, Ptr);
    return RtlpAsanFree(Header, Ptr);
}

PVOID
NTAPI
RtlReAllocateHeap(HANDLE HeapPtr,
                  ULONG Flags,
                  PVOID Ptr,
                  SIZE_T Size)
{
    PASAN_BLOCK_HEADER Header;
    PVOID NewPtr;

    if (!RtlpAsanEnabled || !(Header = RtlpAsanGetBlock(Ptr)))
        return RtlpReAllocateHeapRaw(HeapPtr, Flags, Ptr, Size);

    if (Header->State != ASAN_BLOCK_ALLOCATED)
    {
        if (RtlpAsanReport)
            RtlpAsanReport((ULONG_PTR)Ptr, 0, TRUE, _ReturnAddress(), "realloc of a freed block");
        return NULL;
    }

    /* Smaller, or in place within the rounded size: the same block */
    if (Size <= ((Header->UserSize + ASAN_GRANULE - 1) & ~(SIZE_T)(ASAN_GRANULE - 1)))
    {
        SIZE_T OldSize = Header->UserSize;

        if ((Flags & HEAP_ZERO_MEMORY) && Size > OldSize)
            RtlZeroMemory((PUCHAR)Ptr + OldSize, Size - OldSize);
        Header->UserSize = Size;
        RtlpAsanPoison((ULONG_PTR)Ptr, OldSize, ASAN_HEAP_RIGHT_REDZONE);
        RtlpAsanUnpoison((ULONG_PTR)Ptr, Size);
        return Ptr;
    }
    if (Flags & HEAP_REALLOC_IN_PLACE_ONLY)
        return NULL;

    /* A new block, then the old one is freed */
    NewPtr = RtlpAsanAllocate(HeapPtr, Flags & ~HEAP_REALLOC_IN_PLACE_ONLY, Size);
    if (!NewPtr)
        return NULL;
    RtlCopyMemory(NewPtr, Ptr, Header->UserSize);
    if (Flags & HEAP_ZERO_MEMORY)
        RtlZeroMemory((PUCHAR)NewPtr + Header->UserSize, Size - Header->UserSize);
    RtlpAsanFree(Header, Ptr);
    return NewPtr;
}

SIZE_T
NTAPI
RtlSizeHeap(HANDLE HeapPtr,
            ULONG Flags,
            PVOID Ptr)
{
    PASAN_BLOCK_HEADER Header;

    if (!RtlpAsanEnabled || !(Header = RtlpAsanGetBlock(Ptr)))
        return RtlpSizeHeapRaw(HeapPtr, Flags, Ptr);
    return Header->State == ASAN_BLOCK_ALLOCATED ? Header->UserSize : (SIZE_T)-1;
}
