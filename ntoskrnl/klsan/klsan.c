/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel memory leak sanitizer (KLSAN) for the pool
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * KLSAN finds pool blocks that are still allocated but that nothing points
 * to any more: memory that can never be freed (the same idea as Linux's
 * kmemleak, a tracing garbage collector that only reports).
 *
 * Kernels built with ENABLE_KLSAN and booted with /KLSAN record every pool
 * allocation (ExpAllocatePoolWithTagRaw, used by ExAllocatePoolWithTag and
 * every other pool allocator, lookaside lists included) in a side table: the
 * address, size, tag, pool type, process, time, and the allocation stack
 * (stored once per distinct stack). Frees remove the record.
 *
 * A scan (KlsanControl, or NtSystemDebugControl(SYSDBG_KLSAN_CONTROL) from
 * user mode, see sdk/include/reactos/klsan.h) reads, at PASSIVE_LEVEL:
 *  - the writable sections of every loaded module (global variables),
 *  - the kernel stacks of all threads and the DPC stacks,
 *  - the PFN database,
 *  - the system and session view space (desktop heaps, the GDI handle
 *    table, ...),
 * looking for values that point into a tracked block (anywhere inside it:
 * CONTAINING_RECORD, object headers, low tag bits). A block that is found is
 * read too, and so on (handle tables, object directories, lists all hang off
 * those roots). Blocks that were not reached, in MinScans scans in a row and
 * at least MinAge seconds old, are reported, grouped by allocation stack,
 * with module+offset frames.
 *
 * During a scan, frees are deferred: the block stays allocated (and in the
 * table) until the scan is over, so the scan never reads freed memory.
 * Blocks allocated during the scan count as referenced, and are read too.
 *
 * The scan is conservative: any value that looks like a pointer keeps a
 * block alive (no false leak from that), but a pointer that moved while the
 * scan ran can make a block look unreferenced once; that is why a block must
 * be unreferenced in two scans before it is reported, by default.
 *
 * The stacks are walked with our own copy of the module table (filled when
 * a module is loaded, at PASSIVE_LEVEL): RtlWalkFrameChain looks modules up
 * through their image headers, which is not safe in the pool allocator
 * (see mm/ARM3/kasan.c).
 */

#include <ntoskrnl.h>
#include <reactos/klsan.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#undef ExAllocatePool
#undef ExAllocatePoolWithQuota
#undef ExAllocatePoolWithQuotaTag

/* (expool.c: the pool itself, without tracking) */
PVOID NTAPI ExpAllocatePoolWithTagUntracked(IN POOL_TYPE PoolType, IN SIZE_T NumberOfBytes, IN ULONG Tag);
VOID NTAPI ExpFreePoolWithTagUntracked(IN PVOID P, IN ULONG TagToFree);
PVOID NTAPI ExAllocatePool(POOL_TYPE PoolType, SIZE_T NumberOfBytes);
PVOID NTAPI ExAllocatePoolWithQuota(IN POOL_TYPE PoolType, IN SIZE_T NumberOfBytes);
PVOID NTAPI ExAllocatePoolWithQuotaTag(IN POOL_TYPE PoolType, IN SIZE_T NumberOfBytes, IN ULONG Tag);
PVOID NTAPI ExiAllocateFromPagedLookasideList(IN PPAGED_LOOKASIDE_LIST Lookaside);

/* (the stack walk skips these by their address: no inlined or cloned copies) */
#if defined(__GNUC__) && !defined(__clang__)
#define KLSAN_NOINLINE __attribute__((noinline, noclone))
#else
#define KLSAN_NOINLINE DECLSPEC_NOINLINE
#endif

#define KLSAN_TAG               'nslK'
#define KLSAN_CHUNK_SIZE        (64 * 1024)
#define KLSAN_HASH_BITS         18
#define KLSAN_HASH_SIZE         (1 << KLSAN_HASH_BITS)
#define KLSAN_LOCKS             1024
#define KLSAN_STACK_DEPTH       16
#define KLSAN_STACK_HASH_BITS   14
#define KLSAN_MAX_MODULES       512
#define KLSAN_MODULE_DATA       8
#define KLSAN_MAX_REPORT_STACKS 8192

/* Object flags */
#define KLSAN_PAGE_NODE         0x01    /* indexes another page of a block */
#define KLSAN_FREED             0x02    /* freed during a scan, waits for the end of it */

/* THE RECORDS **************************************************************/

typedef struct _KLSAN_STACK
{
    struct _KLSAN_STACK *Next;          /* hash chain */
    struct _KLSAN_STACK *ReportNext;
    ULONG Hash;
    ULONG Depth;
    /* (report of the current scan) */
    ULONG ReportGeneration;
    ULONG LeakCount;
    SIZE_T LeakBytes;
    ULONG MinAge, MaxAge;
    struct _KLSAN_OBJECT *Example;
    /* (census: the blocks of this stack alive now, and at the mark) */
    ULONG LiveCount, MarkCount;
    SIZE_T LiveBytes, MarkBytes;
    ULONG LiveTag;
    ULONG_PTR Frames[KLSAN_STACK_DEPTH];
} KLSAN_STACK, *PKLSAN_STACK;

typedef struct DECLSPEC_ALIGN(16) _KLSAN_OBJECT
{
    union
    {
        SLIST_ENTRY FreeLink;
        struct _KLSAN_OBJECT *HashNext;
    };
    ULONG_PTR Page;                     /* the page this record indexes */
    struct _KLSAN_OBJECT *Owner;        /* the block's own record (itself for the first page) */
    ULONG_PTR Address;
    SIZE_T Size;
    PKLSAN_STACK Stack;
    struct _KLSAN_OBJECT *GrayNext;     /* scan: still to read */
    struct _KLSAN_OBJECT *DeferNext;    /* scan: freed, waits */
    ULONG Tag;
    ULONG TagToFree;
    ULONG AllocTime;                    /* seconds since boot */
    ULONG AllocGeneration;              /* KlsanGeneration when allocated */
    ULONG Mark;                         /* the generation of the scan that reached it */
    ULONG ProcessId;
    UCHAR Flags;
    UCHAR PoolType;
    UCHAR State;                        /* KLSAN_BLOCK_* of the last scan */
    UCHAR Reserved;
    USHORT UnreferencedScans;
} KLSAN_OBJECT, *PKLSAN_OBJECT;

typedef struct _KLSAN_MODULE
{
    ULONG_PTR Base;
    ULONG_PTR End;
    PRUNTIME_FUNCTION Table;
    ULONG Count;
    BOOLEAN Live;
    BOOLEAN Session;
    UCHAR DataCount;
    ULONG_PTR DataStart[KLSAN_MODULE_DATA];
    ULONG_PTR DataEnd[KLSAN_MODULE_DATA];
    CHAR Name[32];
} KLSAN_MODULE, *PKLSAN_MODULE;

/* GLOBALS ******************************************************************/

BOOLEAN KlsanEnabled = FALSE;

static PKLSAN_OBJECT *KlsanBuckets;
static KSPIN_LOCK KlsanLocks[KLSAN_LOCKS];
static SLIST_HEADER KlsanFreeObjects;
static ULONG_PTR KlsanMinAddress = ~(ULONG_PTR)0, KlsanMaxAddress = 0;

static PKLSAN_STACK KlsanStackBuckets[1 << KLSAN_STACK_HASH_BITS];
static KSPIN_LOCK KlsanStackLock;
static PUCHAR KlsanStackChunk;
static SIZE_T KlsanStackChunkUsed = KLSAN_CHUNK_SIZE;
static KLSAN_STACK KlsanNoStack;

static KLSAN_MODULE KlsanModules[KLSAN_MAX_MODULES];
static volatile LONG KlsanModuleCount;
static KSPIN_LOCK KlsanModuleLock;
static ULONG_PTR KlsanSkipFunctions[16];
static ULONG KlsanSkipCount;

/* The scan */
static KGUARDED_MUTEX KlsanScanMutex;
static volatile LONG KlsanScanning;
static volatile LONG KlsanFreesInFlight;
static volatile ULONG KlsanGeneration = 1;
static ULONG KlsanLastScanGeneration;
static ULONG KlsanBaselineGeneration;
static PKLSAN_OBJECT KlsanGray;
static PKLSAN_OBJECT KlsanDeferred;
static KSPIN_LOCK KlsanDeferLock;
static ULONGLONG KlsanScannedBytes;

static KLSAN_STATS KlsanStats;

/* HELPERS ******************************************************************/

static __inline ULONG
KlsanNow(VOID)
{
    return (ULONG)(KeQueryInterruptTime() / 10000000);
}

static __inline ULONG
KlsanHashPage(ULONG_PTR Page)
{
    return (ULONG)((Page * 0x9E3779B97F4A7C15ULL) >> (64 - KLSAN_HASH_BITS));
}

static __inline PKSPIN_LOCK
KlsanBucketLock(ULONG Bucket)
{
    return &KlsanLocks[Bucket & (KLSAN_LOCKS - 1)];
}

static __inline BOOLEAN
KlsanIsSessionAddress(ULONG_PTR Address)
{
    return MI_IS_SESSION_ADDRESS((PVOID)Address);
}

/* Untracked memory for the records: 64 KB chunks of nonpaged pool */
static PKLSAN_OBJECT
KlsanAllocateObject(VOID)
{
    PKLSAN_OBJECT Object;
    PUCHAR Chunk;
    ULONG i;

    Object = (PKLSAN_OBJECT)InterlockedPopEntrySList(&KlsanFreeObjects);
    if (Object)
        return Object;

    Chunk = ExpAllocatePoolWithTagUntracked(NonPagedPool, KLSAN_CHUNK_SIZE, KLSAN_TAG);
    if (!Chunk)
        return NULL;
    /* (the first one is ours, the others go to the free list) */
    for (i = 1; i < KLSAN_CHUNK_SIZE / sizeof(KLSAN_OBJECT); i++)
        InterlockedPushEntrySList(&KlsanFreeObjects, &((PKLSAN_OBJECT)Chunk)[i].FreeLink);
    return (PKLSAN_OBJECT)Chunk;
}

static __inline VOID
KlsanFreeObject(PKLSAN_OBJECT Object)
{
    InterlockedPushEntrySList(&KlsanFreeObjects, &Object->FreeLink);
}

static VOID
KlsanUpdateRange(ULONG_PTR Start, ULONG_PTR End)
{
    ULONG_PTR Old;

    while ((Old = KlsanMinAddress) > Start)
    {
        if (InterlockedCompareExchangePointer((PVOID *)&KlsanMinAddress, (PVOID)Start, (PVOID)Old) == (PVOID)Old)
            break;
    }
    while ((Old = KlsanMaxAddress) < End)
    {
        if (InterlockedCompareExchangePointer((PVOID *)&KlsanMaxAddress, (PVOID)End, (PVOID)Old) == (PVOID)Old)
            break;
    }
}

static VOID
KlsanInsert(PKLSAN_OBJECT Node)
{
    ULONG Bucket = KlsanHashPage(Node->Page);
    PKSPIN_LOCK Lock = KlsanBucketLock(Bucket);
    KIRQL OldIrql;

    KeAcquireSpinLock(Lock, &OldIrql);
    Node->HashNext = KlsanBuckets[Bucket];
    /* (a scan reads the chains without the lock) */
    KeMemoryBarrier();
    *(PKLSAN_OBJECT volatile *)&KlsanBuckets[Bucket] = Node;
    KeReleaseSpinLock(Lock, OldIrql);
}

/* Unlink the record of Owner for Page */
static PKLSAN_OBJECT
KlsanUnlink(ULONG_PTR Page, PKLSAN_OBJECT Owner, ULONG_PTR Address)
{
    ULONG Bucket = KlsanHashPage(Page);
    PKSPIN_LOCK Lock = KlsanBucketLock(Bucket);
    PKLSAN_OBJECT *Link, Node;
    KIRQL OldIrql;

    KeAcquireSpinLock(Lock, &OldIrql);
    for (Link = &KlsanBuckets[Bucket]; (Node = *Link) != NULL; Link = &Node->HashNext)
    {
        if (Node->Page != Page)
            continue;
        if (Owner ? (Node->Owner == Owner && Node != Owner) :
                    (Node->Owner == Node && Node->Address == Address))
        {
            *Link = Node->HashNext;
            break;
        }
    }
    KeReleaseSpinLock(Lock, OldIrql);
    return Node;
}

static __inline ULONG
KlsanPageCount(PKLSAN_OBJECT Object)
{
    return (ULONG)ADDRESS_AND_SIZE_TO_SPAN_PAGES(Object->Address, max(Object->Size, 1));
}

/* Remove a block's records (all its pages) and free them */
static VOID
KlsanRemoveObject(PKLSAN_OBJECT Object)
{
    ULONG i, Pages = KlsanPageCount(Object);
    PKLSAN_OBJECT Node;

    for (i = 1; i < Pages; i++)
    {
        Node = KlsanUnlink(Object->Page + i, Object, 0);
        if (Node)
            KlsanFreeObject(Node);
    }
    InterlockedDecrement64((LONG64 *)&KlsanStats.TrackedBlocks);
    InterlockedExchangeAdd64((LONG64 *)&KlsanStats.TrackedBytes, -(LONG64)Object->Size);
    KlsanFreeObject(Object);
}

/* The block that starts at Address (under the bucket lock) */
static PKLSAN_OBJECT
KlsanFindBlock(ULONG_PTR Address, BOOLEAN Inside, PKIRQL OldIrql, PKSPIN_LOCK *Lock)
{
    ULONG_PTR Page = Address >> PAGE_SHIFT;
    ULONG Bucket = KlsanHashPage(Page);
    PKLSAN_OBJECT Node, Object;

    *Lock = KlsanBucketLock(Bucket);
    KeAcquireSpinLock(*Lock, OldIrql);
    for (Node = KlsanBuckets[Bucket]; Node; Node = Node->HashNext)
    {
        if (Node->Page != Page)
            continue;
        Object = Node->Owner;
        if (Inside ? (Address - Object->Address < max(Object->Size, 1)) :
                     (Node == Object && Object->Address == Address))
        {
            return Object;
        }
    }
    return NULL;
}

/* MODULES AND STACKS *******************************************************/

static VOID
KlsanAddModule(PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PKLSAN_MODULE Module;
    PIMAGE_NT_HEADERS NtHeader;
    PIMAGE_SECTION_HEADER Section;
    ULONG_PTR Base = (ULONG_PTR)LdrEntry->DllBase;
    ULONG i, n, Size;
    KIRQL OldIrql;

    KeAcquireSpinLock(&KlsanModuleLock, &OldIrql);
    if (KlsanModuleCount >= KLSAN_MAX_MODULES)
    {
        KeReleaseSpinLock(&KlsanModuleLock, OldIrql);
        DPRINT1("KLSAN: too many modules, %wZ is not known\n", &LdrEntry->BaseDllName);
        return;
    }
    Module = &KlsanModules[KlsanModuleCount];
    RtlZeroMemory(Module, sizeof(*Module));
    Module->Base = Base;
    Module->End = Base + LdrEntry->SizeOfImage;
    Module->Session = KlsanIsSessionAddress(Base);
    for (i = 0, n = 0; i < LdrEntry->BaseDllName.Length / sizeof(WCHAR) && n < sizeof(Module->Name) - 1; i++)
    {
        WCHAR c = LdrEntry->BaseDllName.Buffer[i];
        Module->Name[n++] = (c < 0x80 && c >= 0x20) ? (CHAR)c : '?';
    }

    /* The exception directory (for the stacks) and the writable sections
     * (scanned for pointers) */
    if (MmIsAddressValid((PVOID)Base) && (NtHeader = RtlImageNtHeader((PVOID)Base)) != NULL &&
        MmIsAddressValid(NtHeader) && MmIsAddressValid((PUCHAR)(NtHeader + 1) + PAGE_SIZE / 2))
    {
        PIMAGE_DATA_DIRECTORY Dir = &NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

        if (NtHeader->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION &&
            Dir->VirtualAddress && Dir->Size && Dir->VirtualAddress + Dir->Size <= LdrEntry->SizeOfImage)
        {
            Module->Table = (PRUNTIME_FUNCTION)(Base + Dir->VirtualAddress);
            Module->Count = Dir->Size / sizeof(RUNTIME_FUNCTION);
        }

        Section = IMAGE_FIRST_SECTION(NtHeader);
        for (i = 0; i < NtHeader->FileHeader.NumberOfSections && Module->DataCount < KLSAN_MODULE_DATA; i++, Section++)
        {
            if (!MmIsAddressValid(Section) || !MmIsAddressValid(Section + 1))
                break;
            if (!(Section->Characteristics & IMAGE_SCN_MEM_WRITE) ||
                (Section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE))
            {
                continue;
            }
            Size = Section->Misc.VirtualSize ? Section->Misc.VirtualSize : Section->SizeOfRawData;
            if (!Size || Section->VirtualAddress >= LdrEntry->SizeOfImage)
                continue;
            Size = min(Size, LdrEntry->SizeOfImage - Section->VirtualAddress);
            Module->DataStart[Module->DataCount] = Base + Section->VirtualAddress;
            Module->DataEnd[Module->DataCount] = Base + Section->VirtualAddress + Size;
            Module->DataCount++;
        }
    }
    Module->Live = TRUE;
    /* (published last: readers go up to KlsanModuleCount without the lock) */
    KeMemoryBarrier();
    InterlockedIncrement(&KlsanModuleCount);
    KeReleaseSpinLock(&KlsanModuleLock, OldIrql);
}

VOID
NTAPI
KlsanModuleLoaded(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    if (KlsanEnabled)
        KlsanAddModule(LdrEntry);
}

VOID
NTAPI
KlsanModuleUnloaded(IN PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    LONG i;

    if (!KlsanEnabled)
        return;
    /* (the entry stays, for the names in the reports) */
    for (i = 0; i < KlsanModuleCount; i++)
    {
        if (KlsanModules[i].Live && KlsanModules[i].Base == (ULONG_PTR)LdrEntry->DllBase)
            KlsanModules[i].Live = FALSE;
    }
}

static PKLSAN_MODULE
KlsanFindModule(ULONG_PTR Pc, BOOLEAN LiveOnly)
{
    LONG i, Count = KlsanModuleCount;
    PKLSAN_MODULE Dead = NULL;

    /* (the newest one first: an unloaded module's range can be reused) */
    for (i = Count - 1; i >= 0; i--)
    {
        PKLSAN_MODULE Module = &KlsanModules[i];

        if (Pc >= Module->Base && Pc < Module->End)
        {
            if (Module->Live)
                return Module;
            if (!Dead)
                Dead = Module;
        }
    }
    return LiveOnly ? NULL : Dead;
}

static PRUNTIME_FUNCTION
KlsanLookupFunction(PKLSAN_MODULE Module, ULONG_PTR Pc)
{
    ULONG Lo = 0, Hi = Module->Count, Mid;
    ULONG Rva = (ULONG)(Pc - Module->Base);
    PRUNTIME_FUNCTION Entry;

    while (Hi > Lo)
    {
        Mid = (Lo + Hi) / 2;
        Entry = &Module->Table[Mid];
        if (Module->Session && !MmIsAddressValid(Entry))
            return NULL;
        if (Rva < Entry->BeginAddress)
            Hi = Mid;
        else if (Rva >= Entry->EndAddress)
            Lo = Mid + 1;
        else
            return Entry;
    }
    return NULL;
}

static BOOLEAN
KlsanIsSkipped(ULONG_PTR Function)
{
    ULONG i;

    for (i = 0; i < KlsanSkipCount; i++)
    {
        if (KlsanSkipFunctions[i] == Function)
            return TRUE;
    }
    return FALSE;
}

/* The allocation stack, without the pool functions themselves */
static KLSAN_NOINLINE ULONG
KlsanCaptureStack(PULONG_PTR Frames, PVOID Caller)
{
    CONTEXT Context;
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR Low, High, Here = (ULONG_PTR)&Context;
    ULONG Steps, Count = 0;
    BOOLEAN Skipping = TRUE;

    /* On the thread's stack, or on the DPC stack */
    if (Here >= Thread->StackLimit && Here < (ULONG_PTR)Thread->StackBase)
    {
        Low = Thread->StackLimit;
        High = (ULONG_PTR)Thread->StackBase;
    }
    else
    {
        High = (ULONG_PTR)KeGetCurrentPrcb()->DpcStack;
        Low = High - KERNEL_STACK_SIZE;
        if (Here < Low || Here >= High)
            goto CallerOnly;
    }
    /* (deep in the stack: no room for the walk) */
    if (Here - Low < 6 * 1024)
        goto CallerOnly;

    RtlCaptureContext(&Context);
    for (Steps = 0; Steps < KLSAN_STACK_DEPTH + 8 && Count < KLSAN_STACK_DEPTH; Steps++)
    {
        ULONG_PTR Pc = Context.Rip;
        PKLSAN_MODULE Module = KlsanFindModule(Pc, TRUE);
        PRUNTIME_FUNCTION Entry;
        PVOID HandlerData;
        ULONG64 EstablisherFrame;

        if (!Module || !Module->Table)
            break;
        Entry = KlsanLookupFunction(Module, Pc);
        if (Skipping && !(Entry && KlsanIsSkipped(Module->Base + Entry->BeginAddress)))
            Skipping = FALSE;
        if (!Skipping)
            Frames[Count++] = Pc;

        if (Entry)
        {
            /* RtlVirtualUnwind reads the code at Pc (is it in an epilog?):
             * pageable code can be paged out, INIT code is gone */
            if (!MmIsAddressValid((PVOID)Pc) ||
                (BYTE_OFFSET(Pc) > PAGE_SIZE - 128 && !MmIsAddressValid((PVOID)(Pc + 128))))
            {
                break;
            }
            if (Module->Session && !MmIsAddressValid((PVOID)(Module->Base + Entry->UnwindData)))
                break;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, Module->Base, Pc, Entry, &Context,
                             &HandlerData, &EstablisherFrame, NULL);
        }
        else
        {
            /* A leaf function: the return address is on top */
            if (Context.Rsp < Low || Context.Rsp + sizeof(ULONG_PTR) > High)
                break;
            Context.Rip = *(PULONG_PTR)Context.Rsp;
            Context.Rsp += sizeof(ULONG_PTR);
        }
        if (Context.Rsp < Low || Context.Rsp >= High || Context.Rip < (ULONG_PTR)MmSystemRangeStart)
            break;
    }
    if (Count)
        return Count;

CallerOnly:
    Frames[0] = (ULONG_PTR)Caller;
    return 1;
}

static PKLSAN_STACK
KlsanSaveStack(PULONG_PTR Frames, ULONG Depth)
{
    ULONG Hash = 2166136261u, i, Bucket;
    PKLSAN_STACK Stack;
    KIRQL OldIrql;

    for (i = 0; i < Depth; i++)
        Hash = (Hash ^ (ULONG)Frames[i] ^ (ULONG)(Frames[i] >> 32)) * 16777619u;
    Bucket = Hash & ((1 << KLSAN_STACK_HASH_BITS) - 1);

    /* (entries are never removed: look without the lock first) */
    for (Stack = *(PKLSAN_STACK volatile *)&KlsanStackBuckets[Bucket]; Stack; Stack = Stack->Next)
    {
        if (Stack->Hash == Hash && Stack->Depth == Depth &&
            RtlEqualMemory(Stack->Frames, Frames, Depth * sizeof(ULONG_PTR)))
        {
            return Stack;
        }
    }

    KeAcquireSpinLock(&KlsanStackLock, &OldIrql);
    for (Stack = KlsanStackBuckets[Bucket]; Stack; Stack = Stack->Next)
    {
        if (Stack->Hash == Hash && Stack->Depth == Depth &&
            RtlEqualMemory(Stack->Frames, Frames, Depth * sizeof(ULONG_PTR)))
        {
            KeReleaseSpinLock(&KlsanStackLock, OldIrql);
            return Stack;
        }
    }
    if (KlsanStackChunkUsed + sizeof(KLSAN_STACK) > KLSAN_CHUNK_SIZE)
    {
        PUCHAR Chunk = ExpAllocatePoolWithTagUntracked(NonPagedPool, KLSAN_CHUNK_SIZE, KLSAN_TAG);
        if (!Chunk)
        {
            KeReleaseSpinLock(&KlsanStackLock, OldIrql);
            return &KlsanNoStack;
        }
        KlsanStackChunk = Chunk;
        KlsanStackChunkUsed = 0;
    }
    Stack = (PKLSAN_STACK)(KlsanStackChunk + KlsanStackChunkUsed);
    KlsanStackChunkUsed += sizeof(KLSAN_STACK);
    RtlZeroMemory(Stack, sizeof(*Stack));
    Stack->Hash = Hash;
    Stack->Depth = Depth;
    RtlCopyMemory(Stack->Frames, Frames, Depth * sizeof(ULONG_PTR));
    Stack->Next = KlsanStackBuckets[Bucket];
    KeMemoryBarrier();
    *(PKLSAN_STACK volatile *)&KlsanStackBuckets[Bucket] = Stack;
    KlsanStats.Stacks++;
    KeReleaseSpinLock(&KlsanStackLock, OldIrql);
    return Stack;
}

/* THE POOL HOOKS ***********************************************************/

static KLSAN_NOINLINE VOID
KlsanTrackAllocation(PVOID P, SIZE_T Size, ULONG Tag, POOL_TYPE PoolType, PVOID Caller)
{
    ULONG_PTR Frames[KLSAN_STACK_DEPTH];
    PKLSAN_OBJECT Object, Node, Nodes = NULL;
    ULONG Depth, Pages, i;

    Depth = KlsanCaptureStack(Frames, Caller);

    Object = KlsanAllocateObject();
    if (!Object)
    {
        InterlockedIncrement64((LONG64 *)&KlsanStats.UntrackedAllocations);
        return;
    }
    Object->Address = (ULONG_PTR)P;
    Object->Size = Size;
    Object->Page = (ULONG_PTR)P >> PAGE_SHIFT;
    Object->Owner = Object;
    Object->Stack = KlsanSaveStack(Frames, Depth);
    Object->GrayNext = NULL;
    Object->DeferNext = NULL;
    Object->Tag = Tag;
    Object->TagToFree = 0;
    Object->AllocTime = KlsanNow();
    Object->AllocGeneration = KlsanGeneration;
    Object->Mark = 0;
    Object->ProcessId = HandleToUlong(PsGetCurrentProcessId());
    Object->Flags = 0;
    Object->PoolType = (UCHAR)PoolType;
    Object->State = KLSAN_BLOCK_NEW;
    Object->UnreferencedScans = 0;

    /* A record for each other page of a big block (pointers into the middle
     * of it are found through the page) */
    Pages = KlsanPageCount(Object);
    for (i = 1; i < Pages; i++)
    {
        Node = KlsanAllocateObject();
        if (!Node)
        {
            while (Nodes)
            {
                Node = Nodes->GrayNext;
                KlsanFreeObject(Nodes);
                Nodes = Node;
            }
            KlsanFreeObject(Object);
            InterlockedIncrement64((LONG64 *)&KlsanStats.UntrackedAllocations);
            return;
        }
        Node->Page = Object->Page + i;
        Node->Owner = Object;
        Node->Flags = KLSAN_PAGE_NODE;
        Node->GrayNext = Nodes;
        Nodes = Node;
    }

    KlsanUpdateRange(Object->Address, Object->Address + max(Size, 1));
    KlsanInsert(Object);
    while (Nodes)
    {
        Node = Nodes->GrayNext;
        Nodes->GrayNext = NULL;
        KlsanInsert(Nodes);
        Nodes = Node;
    }
    InterlockedIncrement64((LONG64 *)&KlsanStats.Allocations);
    InterlockedIncrement64((LONG64 *)&KlsanStats.TrackedBlocks);
    InterlockedExchangeAdd64((LONG64 *)&KlsanStats.TrackedBytes, (LONG64)Size);
}

/* TRUE: freed during a scan, the scan frees it when it is done */
static BOOLEAN
KlsanTrackFree(PVOID P, ULONG TagToFree)
{
    PKLSAN_OBJECT Object;
    PKSPIN_LOCK Lock;
    KIRQL OldIrql;
    BOOLEAN Deferred = FALSE;

    InterlockedIncrement(&KlsanFreesInFlight);
    if (KlsanScanning)
    {
        Object = KlsanFindBlock((ULONG_PTR)P, FALSE, &OldIrql, &Lock);
        if (Object && !(Object->Flags & KLSAN_FREED))
        {
            Object->Flags |= KLSAN_FREED;
            Object->TagToFree = TagToFree;
            Deferred = TRUE;
        }
        KeReleaseSpinLock(Lock, OldIrql);
        if (Deferred)
        {
            KeAcquireSpinLock(&KlsanDeferLock, &OldIrql);
            Object->DeferNext = KlsanDeferred;
            KlsanDeferred = Object;
            KeReleaseSpinLock(&KlsanDeferLock, OldIrql);
            InterlockedIncrement64((LONG64 *)&KlsanStats.DeferredFrees);
        }
    }
    else
    {
        Object = KlsanUnlink((ULONG_PTR)P >> PAGE_SHIFT, NULL, (ULONG_PTR)P);
        if (Object)
        {
            KlsanRemoveObject(Object);
            InterlockedIncrement64((LONG64 *)&KlsanStats.Frees);
        }
    }
    InterlockedDecrement(&KlsanFreesInFlight);
    return Deferred;
}

/* The pool functions everybody else uses (expool.c has the untracked ones) */
KLSAN_NOINLINE
PVOID
NTAPI
ExpAllocatePoolWithTagRaw(IN POOL_TYPE PoolType,
                          IN SIZE_T NumberOfBytes,
                          IN ULONG Tag)
{
    PVOID P = ExpAllocatePoolWithTagUntracked(PoolType, NumberOfBytes, Tag);

    if (P && KlsanEnabled)
        KlsanTrackAllocation(P, NumberOfBytes, Tag, PoolType, _ReturnAddress());
    return P;
}

KLSAN_NOINLINE
VOID
NTAPI
ExpFreePoolWithTagRaw(IN PVOID P,
                      IN ULONG TagToFree)
{
    if (KlsanEnabled && KlsanTrackFree(P, TagToFree))
        return;
    ExpFreePoolWithTagUntracked(P, TagToFree);
}

/* THE SCAN *****************************************************************/

static VOID
KlsanScanValue(ULONG_PTR Value)
{
    ULONG_PTR Page;
    PKLSAN_OBJECT Node, Object;

    if (Value < KlsanMinAddress || Value >= KlsanMaxAddress)
        return;
    Page = Value >> PAGE_SHIFT;
    /* (no lock: nothing is removed while a scan runs, and new records are
     * linked in fully set up) */
    for (Node = *(PKLSAN_OBJECT volatile *)&KlsanBuckets[KlsanHashPage(Page)]; Node; Node = Node->HashNext)
    {
        if (Node->Page != Page)
            continue;
        Object = Node->Owner;
        if (Value - Object->Address < max(Object->Size, 1))
        {
            if (Object->Mark != KlsanGeneration)
            {
                Object->Mark = KlsanGeneration;
                Object->GrayNext = KlsanGray;
                KlsanGray = Object;
            }
            return;
        }
    }
}

/* Validate: read only the pages that are mapped right now */
static VOID
KlsanScanRange(ULONG_PTR Start, ULONG_PTR End, BOOLEAN Validate)
{
    ULONG_PTR PageEnd, p;

    Start = (Start + sizeof(ULONG_PTR) - 1) & ~(ULONG_PTR)(sizeof(ULONG_PTR) - 1);
    while (Start + sizeof(ULONG_PTR) <= End)
    {
        PageEnd = min((ULONG_PTR)PAGE_ALIGN(Start) + PAGE_SIZE, End);
        if (!Validate || MmIsAddressValid((PVOID)Start))
        {
            for (p = Start; p + sizeof(ULONG_PTR) <= PageEnd; p += sizeof(ULONG_PTR))
                KlsanScanValue(*(volatile ULONG_PTR *)p);
            KlsanScannedBytes += PageEnd - Start;
        }
        Start = PageEnd;
    }
}

static VOID
KlsanScanGray(VOID)
{
    PKLSAN_OBJECT Object;

    while ((Object = KlsanGray) != NULL)
    {
        KlsanGray = Object->GrayNext;
        Object->GrayNext = NULL;
        if (Object->Flags & KLSAN_FREED)
            continue;
        /* (paged pool is read at PASSIVE_LEVEL, paged in if needed; session
         * pool only where it is mapped) */
        KlsanScanRange(Object->Address, Object->Address + Object->Size,
                       KlsanIsSessionAddress(Object->Address));
    }
}

static VOID
KlsanScanThreadStack(PKTHREAD Thread)
{
    ULONG_PTR Low, High, Limit;

    if (Thread == KeGetCurrentThread())
    {
        Low = (ULONG_PTR)&Low;
    }
    else
    {
        /* (the stack of a terminated thread goes away without waiting for
         * the last reference) */
        if (Thread->State == Terminated || !Thread->InitialStack)
            return;
        Low = (Thread->State == Running) ? Thread->StackLimit : (ULONG_PTR)Thread->KernelStack;
    }
    High = (ULONG_PTR)Thread->StackBase;
    Limit = Thread->StackLimit;
    if (!High || High <= Limit || High - Limit > 4 * 1024 * 1024)
        return;
    if (Low < Limit || Low >= High)
        Low = Limit;
    KlsanScanRange(Low, High, TRUE);
}

static VOID
KlsanScanRoots(VOID)
{
    PEPROCESS Process;
    PETHREAD Thread;
    LONG i;
    ULONG j;

    /* Global variables */
    for (i = 0; i < KlsanModuleCount; i++)
    {
        PKLSAN_MODULE Module = &KlsanModules[i];

        if (!Module->Live)
            continue;
        for (j = 0; j < Module->DataCount; j++)
            KlsanScanRange(Module->DataStart[j], Module->DataEnd[j], TRUE);
    }
    KlsanScanGray();

    /* Kernel stacks */
    for (Process = PsGetNextProcess(NULL); Process; Process = PsGetNextProcess(Process))
    {
        for (Thread = PsGetNextProcessThread(Process, NULL); Thread; Thread = PsGetNextProcessThread(Process, Thread))
            KlsanScanThreadStack(&Thread->Tcb);
    }
    for (i = 0; i < (LONG)KeNumberProcessors; i++)
    {
        ULONG_PTR DpcStack = (ULONG_PTR)KiProcessorBlock[i]->DpcStack;

        if (DpcStack)
            KlsanScanRange(DpcStack - KERNEL_STACK_SIZE, DpcStack, TRUE);
    }
    KlsanScanGray();

    /* This session's own structure (in session space) */
    if (MmSessionSpace && MmIsAddressValid(MmSessionSpace))
        KlsanScanRange((ULONG_PTR)MmSessionSpace, (ULONG_PTR)(MmSessionSpace + 1), TRUE);

    /* The PFN database (the reverse map lists of the pages, ...) */
    if (MmPfnDatabase)
        KlsanScanRange((ULONG_PTR)MmPfnDatabase, (ULONG_PTR)(MmPfnDatabase + MmHighestPhysicalPage + 1), TRUE);
    KlsanScanGray();

    /* Views in system space (desktop heaps, the GDI handle table...) */
    if (MiSystemViewStart && MmSystemViewSize)
        KlsanScanRange((ULONG_PTR)MiSystemViewStart, (ULONG_PTR)MiSystemViewStart + MmSystemViewSize, TRUE);
    if (MiSessionViewStart && MmSessionViewSize)
        KlsanScanRange((ULONG_PTR)MiSessionViewStart, (ULONG_PTR)MiSessionViewStart + MmSessionViewSize, TRUE);
    KlsanScanGray();
}

/* Visit every block (their first-page record) */
#define FOR_EACH_BLOCK(Object) \
    for (ULONG _b = 0; _b < KLSAN_HASH_SIZE; _b++) \
        for (PKLSAN_OBJECT Object = *(PKLSAN_OBJECT volatile *)&KlsanBuckets[_b]; Object; Object = Object->HashNext) \
            if (Object->Owner == Object)

/* THE REPORT ***************************************************************/

typedef struct _KLSAN_OUTPUT
{
    PCHAR Buffer;
    SIZE_T Size;
    SIZE_T Used;
    BOOLEAN Print;
} KLSAN_OUTPUT, *PKLSAN_OUTPUT;

static VOID
__cdecl
KlsanPrint(PKLSAN_OUTPUT Out, PCSTR Format, ...)
{
    CHAR Line[256];
    va_list Args;
    int Length;

    va_start(Args, Format);
    Length = _vsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    if (Length < 0)
        Length = sizeof(Line) - 1;
    Line[Length] = ANSI_NULL;
    if (Out->Print)
        DbgPrint("KLSAN: %s", Line);
    if (Out->Buffer && Out->Used + Length < Out->Size)
    {
        RtlCopyMemory(Out->Buffer + Out->Used, Line, Length);
        Out->Used += Length;
        Out->Buffer[Out->Used] = ANSI_NULL;
    }
}

static VOID
KlsanPrintFrame(PKLSAN_OUTPUT Out, ULONG Index, ULONG_PTR Pc)
{
    PKLSAN_MODULE Module = KlsanFindModule(Pc, FALSE);

    if (Module)
        KlsanPrint(Out, "    #%-2lu %s+0x%Ix\n", Index, Module->Name, Pc - Module->Base);
    else
        KlsanPrint(Out, "    #%-2lu %p\n", Index, (PVOID)Pc);
}

static VOID
KlsanPrintExample(PKLSAN_OUTPUT Out, PKLSAN_OBJECT Object)
{
    CHAR Hex[3 * 32 + 1], Text[32 + 1];
    PUCHAR Data = (PUCHAR)Object->Address;
    ULONG i, n = (ULONG)min(Object->Size, 32);

    if (KlsanIsSessionAddress(Object->Address) && !MmIsAddressValid(Data))
        return;
    for (i = 0; i < n; i++)
    {
        static const CHAR Digits[] = "0123456789abcdef";

        Hex[3 * i] = Digits[Data[i] >> 4];
        Hex[3 * i + 1] = Digits[Data[i] & 15];
        Hex[3 * i + 2] = ' ';
        Text[i] = (Data[i] >= 0x20 && Data[i] < 0x7F) ? Data[i] : '.';
    }
    Hex[3 * n] = ANSI_NULL;
    Text[n] = ANSI_NULL;
    KlsanPrint(Out, "    data: %s |%s|\n", Hex, Text);
}

static VOID
KlsanReport(PKLSAN_CONTROL Control, PKLSAN_OUTPUT Out, ULONG ScanMs)
{
    ULONG Now = KlsanNow(), MinAge, MinScans, MaxStacks, Age;
    ULONG Stacks = 0, Blocks = 0, i, j, Best;
    SIZE_T Bytes = 0;
    PKLSAN_STACK *Sorted, Stack;

    MinAge = Control->MinAgeSeconds ? Control->MinAgeSeconds : 30;
    MinScans = Control->MinScans ? Control->MinScans : 2;
    MaxStacks = Control->MaxStacks ? Control->MaxStacks : 40;
    if (Control->MinAgeSeconds == MAXULONG)
        MinAge = 0;

    Sorted = ExpAllocatePoolWithTagUntracked(NonPagedPool, KLSAN_MAX_REPORT_STACKS * sizeof(PVOID), KLSAN_TAG);
    if (!Sorted)
        return;

    FOR_EACH_BLOCK(Object)
    {
        if (Object->Flags & KLSAN_FREED)
            continue;
        if (Object->Mark == KlsanGeneration || Object->AllocGeneration == KlsanGeneration)
        {
            Object->State = (Object->Mark == KlsanGeneration) ? KLSAN_BLOCK_REFERENCED : KLSAN_BLOCK_NEW;
            Object->UnreferencedScans = 0;
            continue;
        }
        Object->State = KLSAN_BLOCK_UNREFERENCED;
        if (Object->UnreferencedScans < MAXUSHORT)
            Object->UnreferencedScans++;
        Age = Now - Object->AllocTime;
        if (Object->UnreferencedScans < MinScans || Age < MinAge ||
            Object->AllocGeneration < KlsanBaselineGeneration)
        {
            continue;
        }

        Stack = Object->Stack ? Object->Stack : &KlsanNoStack;
        if (Stack->ReportGeneration != KlsanGeneration)
        {
            if (Stacks >= KLSAN_MAX_REPORT_STACKS)
                Stack = &KlsanNoStack;
            if (Stack->ReportGeneration != KlsanGeneration)
            {
                Stack->ReportGeneration = KlsanGeneration;
                Stack->LeakCount = 0;
                Stack->LeakBytes = 0;
                Stack->MinAge = MAXULONG;
                Stack->MaxAge = 0;
                Stack->Example = NULL;
                Sorted[Stacks++] = Stack;
            }
        }
        Stack->LeakCount++;
        Stack->LeakBytes += Object->Size;
        Stack->MinAge = min(Stack->MinAge, Age);
        Stack->MaxAge = max(Stack->MaxAge, Age);
        if (!Stack->Example)
            Stack->Example = Object;
        Blocks++;
        Bytes += Object->Size;
    }

    KlsanStats.LastLeakBlocks = Blocks;
    KlsanStats.LastLeakBytes = Bytes;
    KlsanPrint(Out, "scan %lu: %I64u blocks (%I64u bytes) tracked, %I64u KB read in %lu ms\n",
               KlsanGeneration, KlsanStats.TrackedBlocks, KlsanStats.TrackedBytes,
               KlsanScannedBytes / 1024, ScanMs);
    KlsanPrint(Out, "%lu unreferenced blocks (%Iu bytes) from %lu allocation stacks "
               "(unreferenced in %lu scans, at least %lu s old)\n",
               Blocks, Bytes, Stacks, MinScans, MinAge);

    /* The biggest first */
    for (i = 0; i < Stacks && i < MaxStacks; i++)
    {
        PKLSAN_OBJECT Example;
        ULONG Tag;

        Best = i;
        for (j = i + 1; j < Stacks; j++)
        {
            if (Sorted[j]->LeakBytes > Sorted[Best]->LeakBytes)
                Best = j;
        }
        Stack = Sorted[Best];
        Sorted[Best] = Sorted[i];
        Sorted[i] = Stack;

        Example = Stack->Example;
        Tag = Example->Tag;
        KlsanPrint(Out, "[%lu] %lu blocks, %Iu bytes, tag '%.4s', pool type %lu, age %lu-%lu s, e.g. %p (%Iu bytes, pid %lu)\n",
                   i + 1, Stack->LeakCount, Stack->LeakBytes, (PCHAR)&Tag, (ULONG)Example->PoolType,
                   Stack->MinAge, Stack->MaxAge, (PVOID)Example->Address, Example->Size, Example->ProcessId);
        for (j = 0; j < Stack->Depth; j++)
            KlsanPrintFrame(Out, j, Stack->Frames[j]);
        KlsanPrintExample(Out, Example);
    }
    if (Stacks > MaxStacks)
        KlsanPrint(Out, "(%lu more stacks)\n", Stacks - MaxStacks);
    ExpFreePoolWithTagUntracked(Sorted, KLSAN_TAG);
}

static VOID
KlsanWaitForFrees(VOID)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10 * 1000; /* 1 ms */
    while (KlsanFreesInFlight)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

/* Frees wait (the blocks stay in the table) until KlsanThaw */
static VOID
KlsanFreeze(VOID)
{
    InterlockedExchange(&KlsanScanning, 1);
    KlsanWaitForFrees();
}

static VOID
KlsanThaw(VOID)
{
    PKLSAN_OBJECT Object, Next;
    KIRQL OldIrql;

    /* Now the deferred frees */
    InterlockedExchange(&KlsanScanning, 0);
    KlsanWaitForFrees();
    KeAcquireSpinLock(&KlsanDeferLock, &OldIrql);
    Object = KlsanDeferred;
    KlsanDeferred = NULL;
    KeReleaseSpinLock(&KlsanDeferLock, OldIrql);
    for (; Object; Object = Next)
    {
        PVOID P = (PVOID)Object->Address;
        ULONG TagToFree = Object->TagToFree;

        Next = Object->DeferNext;
        if (KlsanUnlink(Object->Page, NULL, Object->Address) == Object)
            KlsanRemoveObject(Object);
        InterlockedIncrement64((LONG64 *)&KlsanStats.Frees);
        ExpFreePoolWithTagUntracked(P, TagToFree);
    }
}

/* CENSUS: how many blocks of each allocation stack are alive *************/

/* (with the table frozen) */
static VOID
KlsanCensus(VOID)
{
    ULONG i;
    PKLSAN_STACK Stack;

    for (i = 0; i < RTL_NUMBER_OF(KlsanStackBuckets); i++)
    {
        for (Stack = KlsanStackBuckets[i]; Stack; Stack = Stack->Next)
        {
            Stack->LiveCount = 0;
            Stack->LiveBytes = 0;
        }
    }
    KlsanNoStack.LiveCount = 0;
    KlsanNoStack.LiveBytes = 0;
    FOR_EACH_BLOCK(Object)
    {
        if (Object->Flags & KLSAN_FREED)
            continue;
        Stack = Object->Stack ? Object->Stack : &KlsanNoStack;
        Stack->LiveCount++;
        Stack->LiveBytes += Object->Size;
        Stack->LiveTag = Object->Tag;
    }
}

static VOID
KlsanMark(VOID)
{
    ULONG i;
    PKLSAN_STACK Stack;

    KlsanFreeze();
    KlsanCensus();
    for (i = 0; i < RTL_NUMBER_OF(KlsanStackBuckets); i++)
    {
        for (Stack = KlsanStackBuckets[i]; Stack; Stack = Stack->Next)
        {
            Stack->MarkCount = Stack->LiveCount;
            Stack->MarkBytes = Stack->LiveBytes;
        }
    }
    KlsanNoStack.MarkCount = KlsanNoStack.LiveCount;
    KlsanNoStack.MarkBytes = KlsanNoStack.LiveBytes;
    KlsanThaw();
    DbgPrint("KLSAN: mark: the growth report counts from now on\n");
}

/* The allocation stacks with the most more bytes alive than at the mark */
static VOID
KlsanGrowth(PKLSAN_CONTROL Control, PKLSAN_OUTPUT Out)
{
    ULONG MaxStacks = Control->MaxStacks ? Control->MaxStacks : 40;
    ULONG i, j, Best, Count = 0;
    LONGLONG TotalBytes = 0, TotalCount = 0;
    PKLSAN_STACK *Sorted, Stack;

    Sorted = ExpAllocatePoolWithTagUntracked(NonPagedPool, KLSAN_MAX_REPORT_STACKS * sizeof(PVOID), KLSAN_TAG);
    if (!Sorted)
        return;
    KlsanFreeze();
    KlsanCensus();
    for (i = 0; i < RTL_NUMBER_OF(KlsanStackBuckets); i++)
    {
        for (Stack = KlsanStackBuckets[i]; Stack; Stack = Stack->Next)
        {
            TotalBytes += (LONGLONG)Stack->LiveBytes - (LONGLONG)Stack->MarkBytes;
            TotalCount += (LONGLONG)Stack->LiveCount - (LONGLONG)Stack->MarkCount;
            if (Stack->LiveBytes > Stack->MarkBytes && Count < KLSAN_MAX_REPORT_STACKS)
                Sorted[Count++] = Stack;
        }
    }
    KlsanPrint(Out, "growth since the mark: %I64d blocks, %I64d bytes in all; %lu allocation stacks grew\n",
               TotalCount, TotalBytes, Count);
    for (i = 0; i < Count && i < MaxStacks; i++)
    {
        ULONG Tag;

        Best = i;
        for (j = i + 1; j < Count; j++)
        {
            if (Sorted[j]->LiveBytes - Sorted[j]->MarkBytes > Sorted[Best]->LiveBytes - Sorted[Best]->MarkBytes)
                Best = j;
        }
        Stack = Sorted[Best];
        Sorted[Best] = Sorted[i];
        Sorted[i] = Stack;
        Tag = Stack->LiveTag;
        KlsanPrint(Out, "[%lu] +%ld blocks, +%Iu bytes (now %lu blocks, %Iu bytes), tag '%.4s'\n",
                   i + 1, (LONG)(Stack->LiveCount - Stack->MarkCount), Stack->LiveBytes - Stack->MarkBytes,
                   Stack->LiveCount, Stack->LiveBytes, (PCHAR)&Tag);
        for (j = 0; j < Stack->Depth; j++)
            KlsanPrintFrame(Out, j, Stack->Frames[j]);
    }
    KlsanThaw();
    ExpFreePoolWithTagUntracked(Sorted, KLSAN_TAG);
}

static VOID
KlsanScan(PKLSAN_CONTROL Control, PKLSAN_OUTPUT Out)
{
    ULONGLONG Start = KeQueryInterruptTime();
    ULONG ScanMs;

    KlsanFreeze();
    InterlockedIncrement((PLONG)&KlsanGeneration);
    KlsanScannedBytes = 0;
    KlsanGray = NULL;

    KlsanScanRoots();

    /* Blocks allocated meanwhile count as referenced: what they point to too */
    FOR_EACH_BLOCK(Block)
    {
        if (Block->AllocGeneration == KlsanGeneration && Block->Mark != KlsanGeneration)
        {
            Block->Mark = KlsanGeneration;
            Block->GrayNext = KlsanGray;
            KlsanGray = Block;
        }
    }
    KlsanScanGray();

    ScanMs = (ULONG)((KeQueryInterruptTime() - Start) / 10000);
    KlsanStats.Scans++;
    KlsanStats.LastScanMs = ScanMs;
    KlsanStats.LastScanBytes = KlsanScannedBytes;
    KlsanLastScanGeneration = KlsanGeneration;
    KlsanReport(Control, Out, ScanMs);

    KlsanThaw();
}

NTSTATUS
NTAPI
KlsanControl(IN PKLSAN_CONTROL Control,
             OUT PVOID Output OPTIONAL,
             IN ULONG OutputLength,
             OUT PULONG ReturnLength OPTIONAL)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KLSAN_OUTPUT Out = { NULL, 0, 0, FALSE };
    KLSAN_STATS Stats;
    ULONG State = KLSAN_BLOCK_UNTRACKED;
    PVOID Result = NULL;
    ULONG Length = 0;

    if (!KlsanEnabled)
        return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireGuardedMutex(&KlsanScanMutex);
    switch (Control->Command)
    {
        case KLSAN_CMD_SCAN:
            Out.Size = min(OutputLength, 4 * 1024 * 1024);
            Out.Buffer = Out.Size ? ExpAllocatePoolWithTagUntracked(PagedPool, Out.Size, KLSAN_TAG) : NULL;
            Out.Print = !(Control->Flags & KLSAN_SCAN_QUIET);
            if (Out.Buffer)
                Out.Buffer[0] = ANSI_NULL;
            KlsanScan(Control, &Out);
            if (Out.Buffer)
            {
                Result = Out.Buffer;
                Length = (ULONG)Out.Used + 1;
            }
            break;

        case KLSAN_CMD_GROWTH:
            Out.Size = min(OutputLength, 4 * 1024 * 1024);
            Out.Buffer = Out.Size ? ExpAllocatePoolWithTagUntracked(PagedPool, Out.Size, KLSAN_TAG) : NULL;
            Out.Print = !(Control->Flags & KLSAN_SCAN_QUIET);
            if (Out.Buffer)
                Out.Buffer[0] = ANSI_NULL;
            KlsanGrowth(Control, &Out);
            if (Out.Buffer)
            {
                Result = Out.Buffer;
                Length = (ULONG)Out.Used + 1;
            }
            break;

        case KLSAN_CMD_MARK:
            KlsanMark();
            break;

        case KLSAN_CMD_BASELINE:
            KlsanBaselineGeneration = InterlockedIncrement((PLONG)&KlsanGeneration);
            DbgPrint("KLSAN: baseline: blocks allocated until now are not reported\n");
            break;

        case KLSAN_CMD_QUERY:
        {
            PKLSAN_OBJECT Object;
            PKSPIN_LOCK Lock;
            KIRQL OldIrql;

            Object = KlsanFindBlock((ULONG_PTR)Control->Address, TRUE, &OldIrql, &Lock);
            if (Object && !(Object->Flags & KLSAN_FREED))
                State = (Object->AllocGeneration >= KlsanLastScanGeneration) ? KLSAN_BLOCK_NEW : Object->State;
            KeReleaseSpinLock(Lock, OldIrql);
            Result = &State;
            Length = sizeof(State);
            break;
        }

        case KLSAN_CMD_STATS:
            Stats = KlsanStats;
            Result = &Stats;
            Length = sizeof(Stats);
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }
    KeReleaseGuardedMutex(&KlsanScanMutex);

    /* (the caller's buffer, maybe user memory: nothing is held now) */
    if (Result)
    {
        if (OutputLength < Length)
        {
            BOOLEAN Text = (Control->Command == KLSAN_CMD_SCAN || Control->Command == KLSAN_CMD_GROWTH);

            /* (a report is cut, still terminated) */
            Status = Text ? STATUS_SUCCESS : STATUS_INFO_LENGTH_MISMATCH;
            Length = Text ? OutputLength : 0;
            if (Text && Length)
                Out.Buffer[Length - 1] = ANSI_NULL;
        }
        _SEH2_TRY
        {
            if (Length)
                RtlCopyMemory(Output, Result, Length);
            if (ReturnLength)
                *ReturnLength = Length;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else if (ReturnLength)
    {
        *ReturnLength = 0;
    }
    if (Out.Buffer)
        ExpFreePoolWithTagUntracked(Out.Buffer, KLSAN_TAG);
    return Status;
}

/* INITIALIZATION ***********************************************************/

static BOOLEAN
KlsanHasOption(PCSTR Options, PCSTR Option)
{
    SIZE_T Length = strlen(Option), i;

    for (; Options && *Options; Options++)
    {
        for (i = 0; i < Length; i++)
        {
            if (RtlUpperChar(Options[i]) != Option[i])
                break;
        }
        if (i == Length && (Options[i] == ANSI_NULL || Options[i] == ' ' || Options[i] == '='))
            return TRUE;
    }
    return FALSE;
}

/* (after the pools and the loaded module list are set up, before the drivers load) */
CODE_SEG("INIT")
VOID
NTAPI
KlsanInitialize(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY Entry;

    if (!LoaderBlock->LoadOptions || !KlsanHasOption(LoaderBlock->LoadOptions, "KLSAN"))
        return;

    KlsanBuckets = ExpAllocatePoolWithTagUntracked(NonPagedPool, KLSAN_HASH_SIZE * sizeof(PVOID), KLSAN_TAG);
    if (!KlsanBuckets)
    {
        DPRINT1("KLSAN: no memory for the table\n");
        return;
    }
    RtlZeroMemory(KlsanBuckets, KLSAN_HASH_SIZE * sizeof(PVOID));
    InitializeSListHead(&KlsanFreeObjects);
    KeInitializeSpinLock(&KlsanStackLock);
    KeInitializeSpinLock(&KlsanModuleLock);
    KeInitializeSpinLock(&KlsanDeferLock);
    KeInitializeGuardedMutex(&KlsanScanMutex);

    for (Entry = PsLoadedModuleList.Flink; Entry != &PsLoadedModuleList; Entry = Entry->Flink)
        KlsanAddModule(CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks));

    /* The pool functions: not part of the allocation stacks */
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)KlsanCaptureStack;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)KlsanTrackAllocation;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExpAllocatePoolWithTagRaw;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExAllocatePoolWithTag;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExAllocatePool;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExAllocatePoolWithQuota;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExAllocatePoolWithQuotaTag;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExAllocatePoolWithTagPriority;
    KlsanSkipFunctions[KlsanSkipCount++] = (ULONG_PTR)ExiAllocateFromPagedLookasideList;

    KlsanEnabled = TRUE;
    DbgPrint("KLSAN: memory leak sanitizer enabled, %ld modules\n", KlsanModuleCount);
}

/* EOF */
