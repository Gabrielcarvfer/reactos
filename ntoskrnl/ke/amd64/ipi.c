/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Packet IPIs
 * -----------
 * Every processor has a mailbox for each possible sender in its PRCB
 * (RequestMailbox[SenderNumber]). A sender writes the worker routine and its
 * parameters into the mailbox of each target, sets its own bit in the
 * target's SenderSummary and sends the IPI vector. Its own TargetSet holds
 * the targets that have not finished yet: each target clears its bit after
 * running the worker. A sender never has more than one packet in flight
 * (it waits for completion at IPI_LEVEL), so its mailbox slots are free.
 *
 * While a sender waits, it services its own SenderSummary: two processors
 * sending to each other at the same time (or a processor sending to one that
 * waits in KeIpiGenericCall) must not wait for each other forever.
 */

typedef
VOID
(NTAPI *PKIPI_PACKET_WORKER)(
    _In_ ULONG_PTR Parameter1,
    _In_ ULONG_PTR Parameter2,
    _In_ ULONG_PTR Parameter3);

extern PKPRCB KiFreezeOwner;

/* Debugging/statistics */
volatile LONG64 KiIpiPacketCount;

/* FUNCTIONS *****************************************************************/

/* Raise to IPI_LEVEL unless already there or above */
FORCEINLINE
KIRQL
KiRaiseToIpiLevel(VOID)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    if (OldIrql < IPI_LEVEL)
        KfRaiseIrql(IPI_LEVEL);
    return OldIrql;
}

FORCEINLINE
VOID
KiLowerFromIpiLevel(
    _In_ KIRQL OldIrql)
{
    if (OldIrql < IPI_LEVEL)
        KeLowerIrql(OldIrql);
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    /* Check if we can send the IPI directly */
    if (IpiRequest == IPI_APC)
    {
        HalSendSoftwareInterrupt(TargetSet, APC_LEVEL);
    }
    else if (IpiRequest == IPI_DPC)
    {
        HalSendSoftwareInterrupt(TargetSet, DISPATCH_LEVEL);

        /* An idle processor halts at DISPATCH_LEVEL: the DPC interrupt
           can't wake it up. The IPI vector can (it has nothing to do,
           the idle loop then sees the new work). */
        KAFFINITY Idle = 0, Remaining = TargetSet;
        ULONG Number;
        while (Remaining)
        {
            BitScanForward64(&Number, Remaining);
            Remaining &= Remaining - 1;
            if (KiProcessorBlock[Number]->CurrentThread == KiProcessorBlock[Number]->IdleThread)
                Idle |= AFFINITY_MASK(Number);
        }
        if (Idle)
            HalRequestIpi(Idle);
    }
    else if (IpiRequest == IPI_FREEZE)
    {
        /* On x64 the freeze IPI is an NMI */
        HalSendNMI(TargetSet);
    }
    else
    {
        ASSERT(FALSE);
    }
}

/* Run the packets other processors sent to this one */
static
VOID
KiIpiProcessRequests(
    _In_ PKPRCB Prcb)
{
    ULONG64 Summary;
    ULONG Sender;
    PREQUEST_MAILBOX Mailbox;
    PKIPI_PACKET_WORKER Worker;
    ULONG_PTR P1, P2, P3;

    while ((Summary = (ULONG64)InterlockedExchange64((PLONG64)&Prcb->SenderSummary, 0)) != 0)
    {
        while (Summary != 0)
        {
            BitScanForward64(&Sender, Summary);
            Summary &= Summary - 1;

            /* Read the packet */
            Mailbox = &Prcb->RequestMailbox[Sender];
            Worker = (PKIPI_PACKET_WORKER)Mailbox->RequestPacket.WorkerRoutine;
            P1 = (ULONG_PTR)Mailbox->RequestPacket.CurrentPacket[0];
            P2 = (ULONG_PTR)Mailbox->RequestPacket.CurrentPacket[1];
            P3 = (ULONG_PTR)Mailbox->RequestPacket.CurrentPacket[2];

            /* Run it */
            Worker(P1, P2, P3);

            /* Tell the sender we are done (it may reuse the mailbox now) */
            InterlockedAnd64((PLONG64)&KiProcessorBlock[Sender]->TargetSet,
                             ~(LONG64)Prcb->SetMember);
        }
    }
}

/* Called by KiIpiInterrupt (trap.S) at IPI_LEVEL */
VOID
NTAPI
KiIpiInterruptHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    KiIpiProcessRequests(KeGetCurrentPrcb());
}

/* Must be called at IPI_LEVEL */
static
VOID
KiIpiSendRequests(
    _In_ PKPRCB Prcb,
    _In_ KAFFINITY Targets,
    _In_ PKIPI_PACKET_WORKER Worker,
    _In_ ULONG_PTR P1,
    _In_ ULONG_PTR P2,
    _In_ ULONG_PTR P3)
{
    KAFFINITY Remaining = Targets;
    ULONG Number;
    PKPRCB TargetPrcb;
    PREQUEST_MAILBOX Mailbox;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    ASSERT((Targets & Prcb->SetMember) == 0);
    ASSERT(Prcb->TargetSet == 0);

    InterlockedIncrement64(&KiIpiPacketCount);

    Prcb->TargetSet = Targets;

    while (Remaining != 0)
    {
        BitScanForward64(&Number, Remaining);
        Remaining &= Remaining - 1;

        TargetPrcb = KiProcessorBlock[Number];
        Mailbox = &TargetPrcb->RequestMailbox[Prcb->Number];
        Mailbox->RequestPacket.WorkerRoutine = (PVOID)Worker;
        Mailbox->RequestPacket.CurrentPacket[0] = (PVOID)P1;
        Mailbox->RequestPacket.CurrentPacket[1] = (PVOID)P2;
        Mailbox->RequestPacket.CurrentPacket[2] = (PVOID)P3;

        /* Publish it (full barrier) */
        InterlockedOr64((PLONG64)&TargetPrcb->SenderSummary, (LONG64)Prcb->SetMember);
    }

    /* Interrupt the targets */
    HalRequestIpi(Targets);
}

static
VOID
KiIpiWaitForCompletion(
    _In_ PKPRCB Prcb)
{
    while (*(volatile ULONG64 *)&Prcb->TargetSet != 0)
    {
        /* Serve the others while we wait */
        KiIpiProcessRequests(Prcb);
        YieldProcessor();
    }
}

/* The processors to send a request to: none when we are alone or frozen */
static
KAFFINITY
KiIpiGetTargets(
    _In_ PKPRCB Prcb)
{
    /* The debugger froze the other processors: they cannot answer */
    if ((KiFreezeOwner == Prcb) || (Prcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE))
        return 0;

    return KeActiveProcessors & ~Prcb->SetMember;
}

/*
 * Runs Worker(P1, P2, P3) on the other active processors and waits for them.
 * The worker runs at IPI_LEVEL with interrupts disabled; it must be short and
 * must not send IPIs itself. Returns FALSE if there was nobody to send to.
 */
static
BOOLEAN
KiIpiSendPacketAndWaitEx(
    _In_ KAFFINITY Mask,
    _In_ PKIPI_PACKET_WORKER Worker,
    _In_ ULONG_PTR P1,
    _In_ ULONG_PTR P2,
    _In_ ULONG_PTR P3)
{
    KIRQL OldIrql;
    PKPRCB Prcb;
    KAFFINITY Targets;

    if (KeNumberProcessors == 1)
        return FALSE;

    OldIrql = KiRaiseToIpiLevel();

    Prcb = KeGetCurrentPrcb();
    Targets = KiIpiGetTargets(Prcb) & Mask;

    /* A sender interrupted by an NMI/debugger path that flushes again:
       its packet is still in flight, don't overwrite it */
    if (Prcb->TargetSet != 0)
        Targets = 0;

    if (Targets != 0)
    {
        KiIpiSendRequests(Prcb, Targets, Worker, P1, P2, P3);
        KiIpiWaitForCompletion(Prcb);
    }

    KiLowerFromIpiLevel(OldIrql);

    return (Targets != 0);
}

BOOLEAN
NTAPI
KiIpiSendPacketAndWait(
    _In_ PKIPI_PACKET_WORKER Worker,
    _In_ ULONG_PTR P1,
    _In_ ULONG_PTR P2,
    _In_ ULONG_PTR P3)
{
    return KiIpiSendPacketAndWaitEx(~(KAFFINITY)0, Worker, P1, P2, P3);
}

/*
 * The processors that may have TLB entries of the current address space's
 * user part: those running a thread of this process (or attached to it).
 * A processor sets its bit in ActiveProcessors before it loads the process'
 * CR3 and clears it after it loaded another one (KiSwapContextResume,
 * KiSwapProcess); one that loads it later starts with a flushed TLB.
 */
static
KAFFINITY
KiCurrentProcessProcessors(VOID)
{
    /* The page tables were changed before: order the read after them */
    KeMemoryBarrier();
    return KeGetCurrentThread()->ApcState.Process->ActiveProcessors;
}

/* TLB SHOOTDOWN **************************************************************/

static
VOID
KiFlushLocalEntireTb(VOID)
{
    ULONG64 Cr4 = __readcr4();

    if (Cr4 & CR4_PGE)
    {
        /* Toggling PGE flushes global entries too */
        __writecr4(Cr4 & ~CR4_PGE);
        __writecr4(Cr4);
    }
    else
    {
        __writecr3(__readcr3());
    }
}

static
VOID
NTAPI
KiFlushTargetSingleTb(
    _In_ ULONG_PTR Address,
    _In_ ULONG_PTR Unused2,
    _In_ ULONG_PTR Unused3)
{
    __invlpg((PVOID)Address);
}

static
VOID
NTAPI
KiFlushTargetMultipleTb(
    _In_ ULONG_PTR Count,
    _In_ ULONG_PTR Addresses,
    _In_ ULONG_PTR Unused3)
{
    PVOID *Array = (PVOID *)Addresses;
    ULONG_PTR i;

    for (i = 0; i < Count; i++)
        __invlpg(Array[i]);
}

static
VOID
NTAPI
KiFlushTargetRangeTb(
    _In_ ULONG_PTR Start,
    _In_ ULONG_PTR PageCount,
    _In_ ULONG_PTR Unused3)
{
    ULONG_PTR i;

    for (i = 0; i < PageCount; i++)
        __invlpg((PVOID)(Start + i * PAGE_SIZE));
}

static
VOID
NTAPI
KiFlushTargetProcessTb(
    _In_ ULONG_PTR Unused1,
    _In_ ULONG_PTR Unused2,
    _In_ ULONG_PTR Unused3)
{
    __writecr3(__readcr3());
}

static
VOID
NTAPI
KiFlushTargetEntireTb(
    _In_ ULONG_PTR Unused1,
    _In_ ULONG_PTR Unused2,
    _In_ ULONG_PTR Unused3)
{
    KiFlushLocalEntireTb();
}

/* Invalidate one page on every processor */
VOID
NTAPI
KiFlushSingleTbAll(
    _In_ PVOID Address)
{
    KIRQL OldIrql;

    if (KeNumberProcessors == 1)
    {
        __invlpg(Address);
        return;
    }

    /* Flush here and on the others without being moved in between */
    OldIrql = KiRaiseToIpiLevel();
    __invlpg(Address);
    KiIpiSendPacketAndWaitEx((Address <= MmHighestUserAddress) ?
                                 KiCurrentProcessProcessors() : ~(KAFFINITY)0,
                             KiFlushTargetSingleTb, (ULONG_PTR)Address, 0, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* Flush the non-global entries here and where the current process runs:
   for changes of user addresses and of this process' page tables */
VOID
NTAPI
KiFlushCurrentProcessTb(VOID)
{
    KIRQL OldIrql;

    OldIrql = KiRaiseToIpiLevel();
    __writecr3(__readcr3());
    if (KeNumberProcessors > 1)
        KiIpiSendPacketAndWaitEx(KiCurrentProcessProcessors(), KiFlushTargetProcessTb, 0, 0, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* Invalidate a range of pages on every processor */
VOID
NTAPI
KiFlushRangeTbAll(
    _In_ PVOID Start,
    _In_ ULONG_PTR PageCount)
{
    KIRQL OldIrql;
    ULONG_PTR i;

    /* Large ranges: flush everything */
    if (PageCount > 32)
    {
        KeFlushEntireTb(TRUE, TRUE);
        return;
    }

    OldIrql = KiRaiseToIpiLevel();
    for (i = 0; i < PageCount; i++)
        __invlpg((PUCHAR)Start + i * PAGE_SIZE);
    KiIpiSendPacketAndWait(KiFlushTargetRangeTb, (ULONG_PTR)Start, PageCount, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* Invalidate a list of pages on every processor */
VOID
NTAPI
KeFlushMultipleTbAll(
    _In_ ULONG Count,
    _In_ PVOID *Addresses)
{
    KIRQL OldIrql;
    ULONG i;

    OldIrql = KiRaiseToIpiLevel();
    for (i = 0; i < Count; i++)
        __invlpg(Addresses[i]);
    KiIpiSendPacketAndWait(KiFlushTargetMultipleTb, Count, (ULONG_PTR)Addresses, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* Flush the non-global entries (CR3 reload) on every processor */
VOID
NTAPI
KiFlushProcessTbAll(VOID)
{
    KIRQL OldIrql;

    if (KeNumberProcessors == 1)
    {
        __writecr3(__readcr3());
        return;
    }

    OldIrql = KiRaiseToIpiLevel();
    __writecr3(__readcr3());
    KiIpiSendPacketAndWait(KiFlushTargetProcessTb, 0, 0, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* Flush everything, global entries included, here and (optionally) elsewhere */
VOID
NTAPI
KiFlushEntireTbAll(
    _In_ BOOLEAN AllProcessors)
{
    KIRQL OldIrql;

    OldIrql = KiRaiseToIpiLevel();
    KiFlushLocalEntireTb();
    if (AllProcessors)
        KiIpiSendPacketAndWait(KiFlushTargetEntireTb, 0, 0, 0);
    KiLowerFromIpiLevel(OldIrql);
}

/* GENERIC CALL ***************************************************************/

typedef struct _KI_GENERIC_CALL
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    volatile LONG Arrived;
    volatile LONG Go;
} KI_GENERIC_CALL, *PKI_GENERIC_CALL;

static KSPIN_LOCK KiGenericCallLock;

static
VOID
NTAPI
KiIpiGenericCallTarget(
    _In_ ULONG_PTR Context,
    _In_ ULONG_PTR Unused2,
    _In_ ULONG_PTR Unused3)
{
    PKI_GENERIC_CALL Call = (PKI_GENERIC_CALL)Context;

    /* Wait until everybody is here, then run it together */
    InterlockedIncrement(&Call->Arrived);
    while (Call->Go == 0)
        YieldProcessor();

    Call->Function(Call->Argument);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    KI_GENERIC_CALL Call;
    KIRQL OldIrql, DummyIrql;
    PKPRCB Prcb;
    KAFFINITY Targets;
    ULONG_PTR Status;
    LONG Count;

    Call.Function = Function;
    Call.Argument = Argument;
    Call.Arrived = 0;
    Call.Go = 0;

    /* One generic call at a time. Waiting at DISPATCH_LEVEL with interrupts
       on lets us take part in the current one. */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KiGenericCallLock);
    KeRaiseIrql(IPI_LEVEL, &DummyIrql);

    Prcb = KeGetCurrentPrcb();
    Targets = KiIpiGetTargets(Prcb);
    Count = 0;
    for (KAFFINITY t = Targets; t; t &= t - 1)
        Count++;

    if (Targets != 0)
    {
        KiIpiSendRequests(Prcb, Targets, KiIpiGenericCallTarget, (ULONG_PTR)&Call, 0, 0);

        /* Wait for the others to arrive */
        while (Call.Arrived != Count)
        {
            KiIpiProcessRequests(Prcb);
            YieldProcessor();
        }
    }

    /* Go! */
    InterlockedExchange(&Call.Go, 1);
    Status = Function(Argument);

    if (Targets != 0)
        KiIpiWaitForCompletion(Prcb);

    KeLowerIrql(DISPATCH_LEVEL);
    KeReleaseSpinLockFromDpcLevel(&KiGenericCallLock);
    KeLowerIrql(OldIrql);

    return Status;
}
