/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture specific source file to hold multiprocessor functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* Everything an application processor needs, in one non-paged block */
typedef struct _KAP_BOOT_BLOCK
{
    KIPCR Pcr;
    DECLSPEC_ALIGN(16) KIDTENTRY64 Idt[256];
    DECLSPEC_ALIGN(16) KGDTENTRY64 Gdt[128];
    DECLSPEC_ALIGN(16) KTSS64 Tss;
    DECLSPEC_ALIGN(16) ETHREAD IdleThread;
} KAP_BOOT_BLOCK, *PKAP_BOOT_BLOCK;

extern KIPCR KiInitialPcr;
extern PVOID KiP0BootStack;

VOID
KiInitializeProcessorBootStructures(
    _In_ ULONG ProcessorNumber,
    _Out_ PKIPCR Pcr,
    _In_ PKGDTENTRY64 GdtBase,
    _In_ PKIDTENTRY64 IdtBase,
    _In_ PKTSS64 TssBase,
    _In_ PKTHREAD IdleThread,
    _In_ PVOID KernelStack,
    _In_ PVOID DpcStack,
    _In_ PVOID DoubleFaultStack,
    _In_ PVOID NmiStack);

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
static
BOOLEAN
KiStartProcessor(
    _In_ ULONG Number)
{
    PKAP_BOOT_BLOCK Block;
    PVOID KernelStack = NULL, DpcStack = NULL, DoubleFaultStack = NULL;
    PKPROCESSOR_STATE ProcessorState;
    KDESCRIPTOR GdtDesc = {{0}, 0, 0}, IdtDesc = {{0}, 0, 0};
    LARGE_INTEGER Start, Now, Frequency;

    /* Allocate the structures for the new processor */
    Block = ExAllocatePoolZero(NonPagedPool, sizeof(*Block), TAG_KERNEL);
    KernelStack = MmCreateKernelStack(FALSE, 0);
    DpcStack = MmCreateKernelStack(FALSE, 0);
    DoubleFaultStack = MmCreateKernelStack(FALSE, 0);
    if (!Block || !KernelStack || !DpcStack || !DoubleFaultStack)
    {
        DPRINT1("KiStartProcessor(%lu): out of memory\n", Number);
        goto Fail;
    }

    /* Copy the GDT and IDT of the boot processor */
    __sgdt(&GdtDesc.Limit);
    __sidt(&IdtDesc.Limit);
    ASSERT(GdtDesc.Limit < sizeof(Block->Gdt));
    ASSERT(IdtDesc.Limit < sizeof(Block->Idt));
    RtlCopyMemory(Block->Gdt, GdtDesc.Base, GdtDesc.Limit + 1);
    RtlCopyMemory(Block->Idt, IdtDesc.Base, IdtDesc.Limit + 1);

    /* Initialize the PCR, the TSS and its GDT descriptor */
    KiInitializeProcessorBootStructures(Number,
                                        &Block->Pcr,
                                        Block->Gdt,
                                        Block->Idt,
                                        &Block->Tss,
                                        &Block->IdleThread.Tcb,
                                        KernelStack,
                                        DpcStack,
                                        DoubleFaultStack,
                                        DoubleFaultStack);
    Block->Pcr.Prcb.RspBase = (ULONG64)KernelStack;

    /* The state the AP starts with (the HAL's trampoline loads it) */
    ProcessorState = &Block->Pcr.Prcb.ProcessorState;
    RtlZeroMemory(ProcessorState, sizeof(*ProcessorState));
    ProcessorState->SpecialRegisters.Cr0 = __readcr0();
    ProcessorState->SpecialRegisters.Cr3 = __readcr3();
    ProcessorState->SpecialRegisters.Cr4 = __readcr4();
    ProcessorState->SpecialRegisters.Gdtr.Base = Block->Gdt;
    ProcessorState->SpecialRegisters.Gdtr.Limit = GdtDesc.Limit;
    ProcessorState->SpecialRegisters.Idtr.Base = Block->Idt;
    ProcessorState->SpecialRegisters.Idtr.Limit = IdtDesc.Limit;
    ProcessorState->SpecialRegisters.Tr = KGDT64_SYS_TSS;

    /* KiSystemStartup(KeLoaderBlock) runs on the DPC stack until it switches
       to the idle thread's stack (as after a call: RSP + 8 is 16-byte aligned) */
    ProcessorState->ContextFrame.Rsp = (ULONG64)DpcStack - 0x38;
    ProcessorState->ContextFrame.Rip = (ULONG64)KiSystemStartup;
    ProcessorState->ContextFrame.Rcx = (ULONG64)KeLoaderBlock;

    /* Tell KiSystemStartup which processor it is starting */
    KeLoaderBlock->KernelStack = (ULONG_PTR)KernelStack;
    KeLoaderBlock->Thread = (ULONG_PTR)&Block->IdleThread.Tcb;
    KeLoaderBlock->Prcb = (ULONG_PTR)&Block->Pcr.Prcb;
    KeMemoryBarrier();

    DPRINT1("Starting CPU %lu: Pcr %p, idle thread %p, stack %p\n",
            Number, &Block->Pcr, &Block->IdleThread, KernelStack);

    if (!HalStartNextProcessor(KeLoaderBlock, ProcessorState))
    {
        DPRINT1("HalStartNextProcessor failed for CPU %lu\n", Number);
        KeLoaderBlock->Prcb = 0;
        goto Fail;
    }

    /* Wait (at most 5 seconds) until the processor has initialized */
    Start = KeQueryPerformanceCounter(&Frequency);
    while (*(volatile ULONG_PTR *)&KeLoaderBlock->Prcb != 0)
    {
        YieldProcessor();
        Now = KeQueryPerformanceCounter(NULL);
        if ((Now.QuadPart - Start.QuadPart) > 5 * Frequency.QuadPart)
        {
            /* It is running our code somewhere: don't free anything */
            DPRINT1("CPU %lu did not finish its initialization!\n", Number);
            KeLoaderBlock->Prcb = 0;
            return FALSE;
        }
    }

    DPRINT1("CPU %lu is running\n", Number);
    return TRUE;

Fail:
    if (DoubleFaultStack) MmDeleteKernelStack(DoubleFaultStack, FALSE);
    if (DpcStack) MmDeleteKernelStack(DpcStack, FALSE);
    if (KernelStack) MmDeleteKernelStack(KernelStack, FALSE);
    if (Block) ExFreePoolWithTag(Block, TAG_KERNEL);
    return FALSE;
}

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(VOID)
{
    ULONG Number;
    ULONG MaximumProcessors;

    /* NOTE: NT6+ HAL exports HalEnumerateProcessors() and
     * HalQueryMaximumProcessorCount() that help determining
     * the number of detected processors on the system. */
    MaximumProcessors = KeMaximumProcessors;

    /* Limit the number of processors we can start at run-time */
    if (KeNumprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeNumprocSpecified);

    /* Limit also the number of processors we can start during boot-time */
    if (KeBootprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeBootprocSpecified);

    MaximumProcessors = min(MaximumProcessors, MAXIMUM_PROCESSORS);

    /* Start the application processors, one after the other */
    for (Number = 1; Number < MaximumProcessors; Number++)
    {
        if (!KiStartProcessor(Number))
            break;
    }

    /* Restore the boot processor's values in the loader block */
    KeLoaderBlock->KernelStack = (ULONG_PTR)KiP0BootStack;
    KeLoaderBlock->Thread = (ULONG_PTR)&KiInitialThread;
    KeLoaderBlock->Prcb = (ULONG_PTR)&KiInitialPcr.Prcb;

    /* New processes (all descend from the system process) may run anywhere */
    PsInitialSystemProcess->Pcb.Affinity = KeActiveProcessors;

    DPRINT1("KeStartAllProcessors: %u processor(s) running\n", KeNumberProcessors);
}
