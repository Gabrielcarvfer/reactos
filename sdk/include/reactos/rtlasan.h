/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AddressSanitizer for ReactOS user mode: shadow memory and the
 *              heap in ASan mode (sdk/lib/rtl/asan.c), runtime in ntdll
 *              (dll/ntdll/rtl/asan.c)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#pragma once

/*
 * One shadow byte for each 8 bytes of memory:
 *   0           all 8 bytes may be used
 *   1 to 7      only the first N bytes
 *   negative    none: the value says why (red zone of a heap block, freed...)
 */
#define ASAN_SHADOW_SCALE           3
#define ASAN_GRANULE                (1 << ASAN_SHADOW_SCALE)
#define ASAN_HEAP_LEFT_REDZONE      ((CHAR)0xFA)
#define ASAN_HEAP_RIGHT_REDZONE     ((CHAR)0xFB)
#define ASAN_HEAP_FREED             ((CHAR)0xFD)

/* AsanFlags (Image File Execution Options of the program) */
#define ASAN_FLAG_ENABLE            0x00000001  /* checks and the heap in ASan mode */
#define ASAN_FLAG_BREAK             0x00000002  /* break into the debugger on each report */
#define ASAN_FLAG_NO_QUARANTINE     0x00000004  /* reuse freed blocks right away */

/* The heap blocks: a header at the end of the left red zone */
#define ASAN_STACK_DEPTH            6
#define ASAN_BLOCK_MAGIC            0x4E534121  /* "!ASN" */
#define ASAN_BLOCK_ALLOCATED        1
#define ASAN_BLOCK_FREED            2

typedef struct _ASAN_BLOCK_HEADER
{
    ULONG Magic;
    ULONG State;
    SIZE_T UserSize;
    PVOID RawBlock;                     /* what the heap gave */
    PVOID Heap;
    PVOID AllocStack[ASAN_STACK_DEPTH];
    PVOID FreeStack[ASAN_STACK_DEPTH];
} ASAN_BLOCK_HEADER, *PASAN_BLOCK_HEADER;

/* (a multiple of 16, at least the header plus 16 bytes of red zone) */
#define ASAN_LEFT_REDZONE           ((sizeof(ASAN_BLOCK_HEADER) + 16 + 15) & ~15)
#define ASAN_RIGHT_REDZONE          16

extern BOOLEAN RtlpAsanEnabled;
extern ULONG RtlpAsanFlags;
extern ULONG_PTR RtlpAsanShadowBase;
extern ULONG_PTR RtlpAsanShadowLimit;    /* the memory below it has a shadow */

/* How ntdll reports a bad access (or a bad free) found by the heap */
typedef VOID (NTAPI *PRTLP_ASAN_REPORT)(ULONG_PTR Address, SIZE_T Size, BOOLEAN IsWrite,
                                        PVOID Pc, PCSTR What);
extern PRTLP_ASAN_REPORT RtlpAsanReport;
extern BOOLEAN RtlpAsanCaptureStacks;   /* set once the process has started */

static __inline CHAR *RtlpAsanShadow(ULONG_PTR Address)
{
    return (CHAR *)(RtlpAsanShadowBase + (Address >> ASAN_SHADOW_SCALE));
}

VOID NTAPI RtlpAsanPoison(ULONG_PTR Address, SIZE_T Size, CHAR Value);
VOID NTAPI RtlpAsanUnpoison(ULONG_PTR Address, SIZE_T Size);
PASAN_BLOCK_HEADER NTAPI RtlpAsanGetBlock(PVOID Ptr);
PVOID NTAPI RtlpAsanToRawPointer(PVOID Ptr);

/* The heap below the ASan layer (heap.c) */
PVOID NTAPI RtlpAllocateHeapRaw(PVOID HeapPtr, ULONG Flags, SIZE_T Size);
BOOLEAN NTAPI RtlpFreeHeapRaw(HANDLE HeapPtr, ULONG Flags, PVOID Ptr);
PVOID NTAPI RtlpReAllocateHeapRaw(HANDLE HeapPtr, ULONG Flags, PVOID Ptr, SIZE_T Size);
SIZE_T NTAPI RtlpSizeHeapRaw(HANDLE HeapPtr, ULONG Flags, PVOID Ptr);
