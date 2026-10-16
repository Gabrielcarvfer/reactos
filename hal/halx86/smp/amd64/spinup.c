/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern BOOLEAN HalpOnlyBootProcessor;

extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;
extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* The real mode code and its data (apentry.S) */
extern UCHAR HalpAPEntry16[];
extern UCHAR HalpAPEntry16End[];
extern UCHAR HalpAPEntry64[];
extern UCHAR HalpApEntry32[];
extern UCHAR HalpApEntry64Low[];
extern UCHAR HalpApTempGdt[];
extern UCHAR HalpApTempGdtr[];
extern UCHAR HalpApJump32[];
extern UCHAR HalpApJump64[];
extern UCHAR HalpApPml4[];
extern UCHAR HalpApKernelEntry[];
extern UCHAR HalpApGdtr[];
extern UCHAR HalpApIdtr[];
extern UCHAR HalpApCr0[];
extern UCHAR HalpApCr3[];
extern UCHAR HalpApCr4[];
extern UCHAR HalpApRsp[];
extern UCHAR HalpApRip[];
extern UCHAR HalpApRcx[];
extern UCHAR HalpApStage[];

ULONG HalpStartedProcessorCount = 1;

/* Offset of a label of apentry.S inside the stub */
#define STUB_OFFSET(Label) ((ULONG)((ULONG_PTR)(Label) - (ULONG_PTR)HalpAPEntry16))

/* Pointer to the copy of a label inside the low stub */
#define STUB_PTR(Label) ((PVOID)((PUCHAR)HalpLowStub + STUB_OFFSET(Label)))

/* Physical address of a label inside the low stub */
#define STUB_PHYS(Label) ((ULONG)(HalpLowStubPhysicalAddress.LowPart + STUB_OFFSET(Label)))

/* FUNCTIONS *****************************************************************/

/*
 * The low stub has HALP_LOW_STUB_SIZE_IN_PAGES (5) pages:
 *   0: real mode code and data
 *   1: temporary PML4 (copy of the current one, entry 0 identity maps)
 *   2: PDPT for the identity mapping
 *   3: page directory for the identity mapping (one 2 MB large page at 0)
 */
static
ULONG
HalpSetupTemporaryMappings(VOID)
{
    PULONG64 Pml4 = (PULONG64)((PUCHAR)HalpLowStub + 1 * PAGE_SIZE);
    PULONG64 Pdpt = (PULONG64)((PUCHAR)HalpLowStub + 2 * PAGE_SIZE);
    PULONG64 Pd = (PULONG64)((PUCHAR)HalpLowStub + 3 * PAGE_SIZE);
    ULONG64 Base = HalpLowStubPhysicalAddress.QuadPart;

    /* The stub must be identity mapped by the first large page */
    ASSERT(Base + HALP_LOW_STUB_SIZE_IN_PAGES * PAGE_SIZE <= 0x200000);

    /* Copy the current top level table: the kernel half is the same everywhere */
    RtlCopyMemory(Pml4, (PVOID)PXE_BASE, PAGE_SIZE);

    /* Identity map the first 2 MB: present, writable, large page */
    RtlZeroMemory(Pdpt, PAGE_SIZE);
    RtlZeroMemory(Pd, PAGE_SIZE);
    Pd[0] = 0x0 | 0x83;
    Pdpt[0] = (Base + 3 * PAGE_SIZE) | 0x3;
    Pml4[0] = (Base + 2 * PAGE_SIZE) | 0x3;

    return (ULONG)(Base + 1 * PAGE_SIZE);
}

static
VOID
HalpWriteStubDescriptor(
    _In_ PUCHAR Label,
    _In_ PKDESCRIPTOR Descriptor)
{
    PUCHAR Ptr = STUB_PTR(Label);

    /* Limit (2 bytes), followed by the 64-bit base */
    *(PUSHORT)Ptr = Descriptor->Limit;
    *(PULONG64)(Ptr + 2) = (ULONG64)Descriptor->Base;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONG Pml4;
    PUCHAR Ptr;
    ULONG Wait;
    UCHAR Stage;

    /* Bail out if we only use the boot CPU */
    if (HalpOnlyBootProcessor)
        return FALSE;

    /* Bail out if we have started all available CPUs */
    if (HalpStartedProcessorCount >= HalpApicInfoTable.ProcessorCount)
        return FALSE;

    if (!HalpLowStub)
    {
        DPRINT1("HalStartNextProcessor: no low memory stub\n");
        return FALSE;
    }

    /* The code must fit in the first page of the stub */
    ASSERT((ULONG_PTR)HalpAPEntry16End - (ULONG_PTR)HalpAPEntry16 <= PAGE_SIZE);

    /* Put the bootstrap code into low memory */
    RtlCopyMemory(HalpLowStub,
                  HalpAPEntry16,
                  (ULONG_PTR)HalpAPEntry16End - (ULONG_PTR)HalpAPEntry16);

    /* Initialize the temporary page tables */
    Pml4 = HalpSetupTemporaryMappings();

    /* Temporary GDT (physical base) */
    Ptr = STUB_PTR(HalpApTempGdtr);
    *(PUSHORT)Ptr = 4 * sizeof(ULONG64) - 1;
    *(PULONG)(Ptr + 2) = STUB_PHYS(HalpApTempGdt);

    /* Far jump targets (physical) */
    *(PULONG)STUB_PTR(HalpApJump32) = STUB_PHYS(HalpApEntry32);
    *(PULONG)STUB_PTR(HalpApJump64) = STUB_PHYS(HalpApEntry64Low);

    *(PULONG64)STUB_PTR(HalpApPml4) = Pml4;
    *(PULONG64)STUB_PTR(HalpApKernelEntry) = (ULONG64)HalpAPEntry64;

    /* The final state, prepared by the kernel */
    HalpWriteStubDescriptor(HalpApGdtr, &ProcessorState->SpecialRegisters.Gdtr);
    HalpWriteStubDescriptor(HalpApIdtr, &ProcessorState->SpecialRegisters.Idtr);
    *(PULONG64)STUB_PTR(HalpApCr0) = ProcessorState->SpecialRegisters.Cr0;
    *(PULONG64)STUB_PTR(HalpApCr3) = ProcessorState->SpecialRegisters.Cr3;
    *(PULONG64)STUB_PTR(HalpApCr4) = ProcessorState->SpecialRegisters.Cr4;
    *(PULONG64)STUB_PTR(HalpApRsp) = ProcessorState->ContextFrame.Rsp;
    *(PULONG64)STUB_PTR(HalpApRip) = ProcessorState->ContextFrame.Rip;
    *(PULONG64)STUB_PTR(HalpApRcx) = ProcessorState->ContextFrame.Rcx;
    *(PULONG64)STUB_PTR(HalpApStage) = 0;

    /* Make sure everything is in memory before the AP looks at it */
    KeMemoryBarrier();

    DPRINT1("HalStartNextProcessor: starting CPU %lu (APIC id %lu), stub at 0x%lx\n",
            HalpStartedProcessorCount,
            HalpProcessorIdentity[HalpStartedProcessorCount].LapicId,
            HalpLowStubPhysicalAddress.LowPart);

    ApicStartApplicationProcessor(HalpStartedProcessorCount, HalpLowStubPhysicalAddress);

    /* Wait until the AP has left the low stub, so that it can be reused */
    for (Wait = 0; Wait < 100000; Wait++)
    {
        Stage = *(volatile UCHAR *)STUB_PTR(HalpApStage);
        if (Stage >= 4)
            break;
        KeStallExecutionProcessor(10);
    }

    if (Stage < 4)
    {
        DPRINT1("HalStartNextProcessor: CPU %lu did not reach long mode (stage %u)\n",
                HalpStartedProcessorCount, Stage);
        return FALSE;
    }

    HalpProcessorIdentity[HalpStartedProcessorCount].ProcessorStarted = TRUE;
    HalpStartedProcessorCount++;

    return TRUE;
}
