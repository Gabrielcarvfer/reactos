/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel Concurrency Sanitizer (KCSAN) prototype: data race detection
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * Files built with -fsanitize=thread (CMake option KCSAN_PROTOTYPE, see
 * ntos.cmake) call __tsan_readN/__tsan_writeN(address) before each plain memory
 * access. This file is NOT instrumented and implements those hooks the way
 * Linux's KCSAN does, with "soft watchpoints":
 *
 *  - Every access first checks the watchpoint slots. If another CPU/thread is
 *    watching an overlapping address and one of the two accesses is a write,
 *    that is a data race: the watcher is told (the slot is "consumed") and
 *    both sides are reported.
 *  - Every KcsanInterval-th access (per CPU, randomized) sets up a watchpoint
 *    on its own address, reads the value, stalls for a few microseconds, reads
 *    the value again and removes the watchpoint. A consumed watchpoint, or a
 *    value that changed while nobody instrumented was supposed to touch it,
 *    means a racing access.
 *
 * Reports go to the debug log (DbgPrint), with a stack at IRQL <= APC_LEVEL.
 * Enabled by the boot option KCSAN; until then every hook returns at once.
 *
 * Atomic accesses (__tsan_atomic*, from the Interlocked intrinsics and
 * __atomic builtins) are performed and never reported. gcc 8 cannot tell
 * volatile accesses apart (no tsan-distinguish-volatile), so a race on a
 * volatile variable is reported like any other: look at each report.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KCSAN_SLOTS             64
#define KCSAN_MAX_CPUS          64
#define KCSAN_STACK             16
#define KCSAN_SEEN              128     /* (PC, PC) pairs reported already */

/* Watchpoint encoding: valid | consumed | write | size (8 bits) | address (48 bits) */
#define WP_VALID                0x8000000000000000ULL
#define WP_CONSUMED             0x4000000000000000ULL
#define WP_WRITE                0x2000000000000000ULL
#define WP_SIZE_SHIFT           48
#define WP_ADDR_MASK            0x0000FFFFFFFFFFFFULL

typedef struct _KCSAN_OTHER
{
    PVOID Pc;
    PVOID Thread;
    ULONG Cpu;
    ULONG Write;
    ULONG Size;
    ULONG_PTR Address;
    ULONG StackCount;
    PVOID Stack[KCSAN_STACK];
} KCSAN_OTHER;

typedef struct _KCSAN_SLOT
{
    volatile LONG64 Watch;
    KCSAN_OTHER Other;          /* Written by the CPU that consumed the slot */
} KCSAN_SLOT;

BOOLEAN KcsanEnabled = FALSE;
ULONG KcsanInterval = 64;       /* Average number of accesses between watchpoints */
ULONG KcsanStallUs = 40;        /* Watch window at PASSIVE/APC_LEVEL */
ULONG KcsanStallUsDispatch = 10;/* ... at DISPATCH_LEVEL */

/* Statistics, readable in the debugger */
volatile LONG KcsanWatchpoints, KcsanRaces, KcsanValueChanges, KcsanNoSlot, KcsanReports;

static KCSAN_SLOT KcsanSlots[KCSAN_SLOTS];
static LONG KcsanSkip[KCSAN_MAX_CPUS];
static ULONG KcsanSeed[KCSAN_MAX_CPUS];
static volatile LONG64 KcsanSeen[KCSAN_SEEN];
static KSPIN_LOCK KcsanReportLock;

VOID
NTAPI
KcsanInitialize(VOID)
{
    ULONG i;

    KeInitializeSpinLock(&KcsanReportLock);
    for (i = 0; i < KCSAN_MAX_CPUS; i++)
    {
        KcsanSeed[i] = 0x9E3779B9u * (i + 1);
        KcsanSkip[i] = KcsanInterval;
    }
    KcsanEnabled = TRUE;
    DbgPrint("KCSAN: enabled, a watchpoint every ~%lu accesses, window %lu us\n",
             KcsanInterval, KcsanStallUs);
}

static __inline ULONG
KcsanSlotOf(ULONG_PTR Address)
{
    /* Neighbouring words share a slot, so overlapping accesses meet */
    return (ULONG)((Address >> 4) % KCSAN_SLOTS);
}

static __inline BOOLEAN
KcsanOverlaps(ULONG_PTR A, ULONG SizeA, ULONG_PTR B, ULONG SizeB)
{
    return A < B + SizeB && B < A + SizeA;
}

/* Remember reported (PC, PC) pairs; TRUE if this one is new */
static BOOLEAN
KcsanFirstReport(PVOID Pc1, PVOID Pc2)
{
    LONG64 Key = (LONG64)((ULONG_PTR)Pc1 ^ ((ULONG_PTR)Pc2 << 1) ^ ((ULONG_PTR)Pc2 >> 7));
    ULONG i, Start = (ULONG)(Key % KCSAN_SEEN);

    if (Key == 0) Key = 1;
    for (i = 0; i < KCSAN_SEEN; i++)
    {
        volatile LONG64 *Entry = &KcsanSeen[(Start + i) % KCSAN_SEEN];
        LONG64 Old = *Entry;

        if (Old == Key)
            return FALSE;
        if (Old == 0 && InterlockedCompareExchange64(Entry, Key, 0) == 0)
            return TRUE;
        if (*Entry == Key)
            return FALSE;
    }
    return TRUE; /* Table full: report anyway */
}

static ULONG
KcsanStack(PVOID *Stack)
{
    /* Walking the stack can touch pageable unwind data (see kasan.c) */
    if (KeGetCurrentIrql() > APC_LEVEL)
        return 0;
    return RtlWalkFrameChain(Stack, KCSAN_STACK, 0);
}

/* Prints Pc as module+offset, so a driver's reports can be symbolized whatever its load address */
static VOID
KcsanPrintPc(PVOID Pc)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    BOOLEAN InKernel;
    PVOID Base;

    Base = KiPcToFileHeader(Pc, &LdrEntry, FALSE, &InKernel);
    if (Base)
    {
        /* (Reports run at raised IRQL, under a spin lock: %wZ would convert the
         * name with pageable NLS code, so copy it to ASCII by hand) */
        CHAR Name[32];
        ULONG i, Length = LdrEntry->BaseDllName.Length / sizeof(WCHAR);

        if (Length >= sizeof(Name))
            Length = sizeof(Name) - 1;
        for (i = 0; i < Length; i++)
        {
            WCHAR Char = LdrEntry->BaseDllName.Buffer[i];
            Name[i] = (Char < 0x80) ? (CHAR)Char : '?';
        }
        Name[Length] = ANSI_NULL;
        DbgPrint(" %s+0x%Ix", Name, (ULONG_PTR)Pc - (ULONG_PTR)Base);
    }
    else
    {
        DbgPrint(" %p", Pc);
    }
}

static VOID
KcsanPrintStack(PCSTR Who, PVOID *Stack, ULONG Count)
{
    ULONG i;

    DbgPrint("KCSAN:   %s stack:", Who);
    if (Count == 0)
        DbgPrint(" (not taken at IRQL > APC_LEVEL)");
    for (i = 0; i < Count; i++)
        KcsanPrintPc(Stack[i]);
    DbgPrint("\n");
}

/* The watcher's side: the slot was consumed by Other, or the value changed */
static VOID
KcsanReport(PVOID Pc, ULONG_PTR Address, ULONG Size, BOOLEAN Write,
            KCSAN_OTHER *Other, ULONG64 OldValue, ULONG64 NewValue)
{
    PVOID Stack[KCSAN_STACK];
    ULONG Count;
    KIRQL OldIrql;

    if (!KcsanFirstReport(Pc, Other ? Other->Pc : NULL))
        return;

    Count = KcsanStack(Stack);
    InterlockedIncrement(&KcsanReports);
    KeAcquireSpinLock(&KcsanReportLock, &OldIrql);
    DbgPrint("==================================================================\n");
    if (Other)
    {
        DbgPrint("KCSAN: data race: %s of %lu bytes at %p (pc %p, thread %p, cpu %lu)\n",
                 Write ? "write" : "read", Size, (PVOID)Address, Pc,
                 KeGetCurrentThread(), KeGetCurrentProcessorNumber());
        DbgPrint("KCSAN:   races with %s of %lu bytes at %p (pc %p, thread %p, cpu %lu)\n",
                 Other->Write ? "write" : "read", Other->Size, (PVOID)Other->Address,
                 Other->Pc, Other->Thread, Other->Cpu);
        DbgPrint("KCSAN:   pcs:");
        KcsanPrintPc(Pc);
        KcsanPrintPc(Other->Pc);
        DbgPrint("\n");
        KcsanPrintStack("watcher", Stack, Count);
        KcsanPrintStack("other", Other->Stack, Other->StackCount);
    }
    else
    {
        DbgPrint("KCSAN: data race: value changed during %s of %lu bytes at %p (pc %p, thread %p, cpu %lu): %I64x -> %I64x by an uninstrumented access\n",
                 Write ? "write" : "read", Size, (PVOID)Address, Pc,
                 KeGetCurrentThread(), KeGetCurrentProcessorNumber(), OldValue, NewValue);
        DbgPrint("KCSAN:   pc:");
        KcsanPrintPc(Pc);
        DbgPrint("\n");
        KcsanPrintStack("watcher", Stack, Count);
    }
    DbgPrint("KCSAN: %ld races so far (%ld watchpoints)\n", KcsanRaces + KcsanValueChanges, KcsanWatchpoints);
    DbgPrint("==================================================================\n");
    KeReleaseSpinLock(&KcsanReportLock, OldIrql);
}

static __inline ULONG64
KcsanReadValue(ULONG_PTR Address, ULONG Size)
{
    switch (Size)
    {
        case 1: return *(volatile UCHAR *)Address;
        case 2: return *(volatile USHORT *)Address;
        case 4: return *(volatile ULONG *)Address;
        case 8: return *(volatile ULONG64 *)Address;
        default: return 0; /* 16 and ranges: only watched, not compared */
    }
}

/* Set up a watchpoint on our own access and wait for someone to hit it */
static VOID
KcsanWatch(PVOID Pc, ULONG_PTR Address, ULONG Size, BOOLEAN Write)
{
    ULONG SlotIndex = KcsanSlotOf(Address);
    KCSAN_SLOT *Slot = &KcsanSlots[SlotIndex];
    LONG64 Encoded, Now;
    ULONG64 Before, After;
    KIRQL Irql = KeGetCurrentIrql();

    Encoded = (LONG64)(WP_VALID | (Write ? WP_WRITE : 0) |
                       ((ULONG64)(Size > 255 ? 255 : Size) << WP_SIZE_SHIFT) |
                       (Address & WP_ADDR_MASK));
    if (InterlockedCompareExchange64(&Slot->Watch, Encoded, 0) != 0)
    {
        InterlockedIncrement(&KcsanNoSlot);
        return;
    }
    InterlockedIncrement(&KcsanWatchpoints);

    /* The access itself comes right after us at this IRQL, so this read is
     * as safe as the access (no IRQL raise: the memory may be pageable). */
    Before = KcsanReadValue(Address, Size);
    KeStallExecutionProcessor(Irql >= DISPATCH_LEVEL ? KcsanStallUsDispatch : KcsanStallUs);
    After = KcsanReadValue(Address, Size);

    /* Remove the watchpoint; if it was consumed meanwhile, Other is filled */
    Now = InterlockedExchange64(&Slot->Watch, 0);
    if (Now & WP_CONSUMED)
    {
        InterlockedIncrement(&KcsanRaces);
        KcsanReport(Pc, Address, Size, Write, &Slot->Other, 0, 0);
    }
    else if (Before != After)
    {
        InterlockedIncrement(&KcsanValueChanges);
        KcsanReport(Pc, Address, Size, Write, NULL, Before, After);
    }
}

/* Check the watchpoints of other accesses against ours */
static __inline BOOLEAN
KcsanCheckSlot(ULONG SlotIndex, PVOID Pc, ULONG_PTR Address, ULONG Size, BOOLEAN Write)
{
    KCSAN_SLOT *Slot = &KcsanSlots[SlotIndex];
    LONG64 Watch = Slot->Watch;
    ULONG_PTR WatchAddress;
    ULONG WatchSize;

    if (!(Watch & WP_VALID) || (Watch & WP_CONSUMED))
        return FALSE;
    if (!Write && !(Watch & WP_WRITE))
        return FALSE; /* Two reads don't race */
    WatchAddress = (ULONG_PTR)(Watch & WP_ADDR_MASK) | 0xFFFF000000000000ULL;
    WatchSize = (ULONG)((Watch >> WP_SIZE_SHIFT) & 0xFF);
    if (!KcsanOverlaps(Address, Size, WatchAddress, WatchSize))
        return FALSE;

    /* Claim it: only one CPU fills in Other */
    if (InterlockedCompareExchange64(&Slot->Watch, Watch | WP_CONSUMED, Watch) != Watch)
        return FALSE;
    Slot->Other.Pc = Pc;
    Slot->Other.Thread = KeGetCurrentThread();
    Slot->Other.Cpu = KeGetCurrentProcessorNumber();
    Slot->Other.Write = Write;
    Slot->Other.Size = Size;
    Slot->Other.Address = Address;
    Slot->Other.StackCount = KcsanStack(Slot->Other.Stack);
    return TRUE;
}

static VOID
KcsanAccess(PVOID Pc, PVOID Pointer, ULONG Size, BOOLEAN Write)
{
    ULONG_PTR Address = (ULONG_PTR)Pointer;
    ULONG Cpu, First, Last;

    if (!KcsanEnabled || Size == 0)
        return;
    /* Kernel addresses only: user buffers are probed under SEH */
    if (Address < 0xFFFF800000000000ULL)
        return;

    First = KcsanSlotOf(Address);
    Last = KcsanSlotOf(Address + Size - 1);
    if (KcsanCheckSlot(First, Pc, Address, Size, Write))
        return;
    if (Last != First && KcsanCheckSlot(Last, Pc, Address, Size, Write))
        return;

    /* Not above DISPATCH_LEVEL: no stalls in interrupts, nor with the
     * per-CPU data of the clock or IPIs in flux */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return;
    Cpu = KeGetCurrentProcessorNumber() % KCSAN_MAX_CPUS;
    if (--KcsanSkip[Cpu] > 0)
        return;
    KcsanSeed[Cpu] = KcsanSeed[Cpu] * 1103515245 + 12345;
    KcsanSkip[Cpu] = 1 + (LONG)((KcsanSeed[Cpu] >> 8) % (2 * KcsanInterval));
    KcsanWatch(Pc, Address, Size, Write);
}

/* THE HOOKS (called by instrumented code) **********************************/

#define KCSAN_HOOK(Name, Size, Write) \
    void __cdecl Name(void *p) { KcsanAccess(__builtin_return_address(0), p, Size, Write); }

KCSAN_HOOK(__tsan_read1, 1, FALSE)
KCSAN_HOOK(__tsan_read2, 2, FALSE)
KCSAN_HOOK(__tsan_read4, 4, FALSE)
KCSAN_HOOK(__tsan_read8, 8, FALSE)
KCSAN_HOOK(__tsan_read16, 16, FALSE)
KCSAN_HOOK(__tsan_write1, 1, TRUE)
KCSAN_HOOK(__tsan_write2, 2, TRUE)
KCSAN_HOOK(__tsan_write4, 4, TRUE)
KCSAN_HOOK(__tsan_write8, 8, TRUE)
KCSAN_HOOK(__tsan_write16, 16, TRUE)
KCSAN_HOOK(__tsan_unaligned_read2, 2, FALSE)
KCSAN_HOOK(__tsan_unaligned_read4, 4, FALSE)
KCSAN_HOOK(__tsan_unaligned_read8, 8, FALSE)
KCSAN_HOOK(__tsan_unaligned_read16, 16, FALSE)
KCSAN_HOOK(__tsan_unaligned_write2, 2, TRUE)
KCSAN_HOOK(__tsan_unaligned_write4, 4, TRUE)
KCSAN_HOOK(__tsan_unaligned_write8, 8, TRUE)
KCSAN_HOOK(__tsan_unaligned_write16, 16, TRUE)
/* gcc >= 11 with --param tsan-distinguish-volatile=1: marked races, ignored */
void __cdecl __tsan_volatile_read1(void *p) { }
void __cdecl __tsan_volatile_read2(void *p) { }
void __cdecl __tsan_volatile_read4(void *p) { }
void __cdecl __tsan_volatile_read8(void *p) { }
void __cdecl __tsan_volatile_read16(void *p) { }
void __cdecl __tsan_volatile_write1(void *p) { }
void __cdecl __tsan_volatile_write2(void *p) { }
void __cdecl __tsan_volatile_write4(void *p) { }
void __cdecl __tsan_volatile_write8(void *p) { }
void __cdecl __tsan_volatile_write16(void *p) { }

void __cdecl __tsan_read_range(void *p, SIZE_T n)
{ KcsanAccess(__builtin_return_address(0), p, (ULONG)min(n, 255), FALSE); }
void __cdecl __tsan_write_range(void *p, SIZE_T n)
{ KcsanAccess(__builtin_return_address(0), p, (ULONG)min(n, 255), TRUE); }

/* gcc 8 always emits these (no tsan-instrument-func-entry-exit=0 param) */
void __cdecl __tsan_func_entry(void *pc) { }
void __cdecl __tsan_func_exit(void) { }
void __cdecl __tsan_init(void) { }
void __cdecl __tsan_vptr_update(void **p, void *v) { }
void __cdecl __tsan_vptr_read(void **p) { }

/* ATOMICS: performed, never reported (marked concurrent accesses).
 * The memory order argument is ignored: everything is sequentially consistent. */

#define KCSAN_ATOMIC(Bits, Type) \
    Type __cdecl __tsan_atomic##Bits##_load(const volatile Type *a, int mo) \
    { return __atomic_load_n(a, __ATOMIC_SEQ_CST); } \
    void __cdecl __tsan_atomic##Bits##_store(volatile Type *a, Type v, int mo) \
    { __atomic_store_n(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_exchange(volatile Type *a, Type v, int mo) \
    { return __atomic_exchange_n(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_add(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_add(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_sub(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_sub(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_and(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_and(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_or(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_or(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_xor(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_xor(a, v, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_fetch_nand(volatile Type *a, Type v, int mo) \
    { return __atomic_fetch_nand(a, v, __ATOMIC_SEQ_CST); } \
    int __cdecl __tsan_atomic##Bits##_compare_exchange_strong(volatile Type *a, Type *c, Type v, int mo, int fmo) \
    { return __atomic_compare_exchange_n(a, c, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); } \
    int __cdecl __tsan_atomic##Bits##_compare_exchange_weak(volatile Type *a, Type *c, Type v, int mo, int fmo) \
    { return __atomic_compare_exchange_n(a, c, v, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); } \
    Type __cdecl __tsan_atomic##Bits##_compare_exchange_val(volatile Type *a, Type c, Type v, int mo, int fmo) \
    { __atomic_compare_exchange_n(a, &c, v, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return c; }

KCSAN_ATOMIC(8, UCHAR)
KCSAN_ATOMIC(16, USHORT)
KCSAN_ATOMIC(32, ULONG)
KCSAN_ATOMIC(64, ULONG64)

void __cdecl __tsan_atomic_thread_fence(int mo) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void __cdecl __tsan_atomic_signal_fence(int mo) { __atomic_signal_fence(__ATOMIC_SEQ_CST); }
