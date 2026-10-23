/*
 * PROJECT:     ReactOS ACPI Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Spread the PCI interrupt links over their possible IRQs
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/*
 * The PCI devices take the IRQ that the firmware wrote in their Interrupt
 * Line register. The firmware often routes several interrupt links (PNP0C0F)
 * to one IRQ although each could have one of its own (QEMU: the network card
 * and the USB controller both on IRQ 11), and then every interrupt of one
 * device runs the interrupt routines of the others too. Like Windows' and
 * Linux' interrupt link handling, give each link the possible IRQ (_PRS)
 * that the fewest devices use, program it (_SRS) and update the Interrupt
 * Line of the devices routed through it, before the PCI driver reads it.
 * Only links whose IRQ is shared with another link's devices are moved.
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

#define MAX_LINKS 32
#define MAX_LINK_DEVICES 32

typedef struct _PCI_LINK
{
    ACPI_HANDLE Handle;
    ULONG CurrentIrq;           /* 0 if disabled or unknown */
    ULONG PossibleCount;
    UCHAR Possible[16];
    ULONG DeviceCount;
    PCI_SLOT_NUMBER Slots[MAX_LINK_DEVICES];
    ULONG Bus[MAX_LINK_DEVICES];
} PCI_LINK, *PPCI_LINK;

static PCI_LINK Links[MAX_LINKS];
static ULONG LinkCount;

static
PPCI_LINK
FindLink(ACPI_HANDLE Handle)
{
    ULONG i;

    for (i = 0; i < LinkCount; i++)
    {
        if (Links[i].Handle == Handle)
            return &Links[i];
    }
    if (LinkCount == MAX_LINKS)
        return NULL;

    RtlZeroMemory(&Links[LinkCount], sizeof(Links[LinkCount]));
    Links[LinkCount].Handle = Handle;
    return &Links[LinkCount++];
}

/* The IRQ of a link's _CRS, or the IRQ list of its _PRS */
static
VOID
GetLinkIrqs(ACPI_HANDLE Handle, BOOLEAN Possible, PUCHAR Irqs, PULONG Count)
{
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_RESOURCE *Resource;
    ACPI_STATUS Status;
    ULONG i;

    *Count = 0;

    if (Possible)
        Status = AcpiGetPossibleResources(Handle, &Buffer);
    else
        Status = AcpiGetCurrentResources(Handle, &Buffer);
    if (ACPI_FAILURE(Status))
        return;

    for (Resource = Buffer.Pointer;
         Resource->Type != ACPI_RESOURCE_TYPE_END_TAG && Resource->Length;
         Resource = ACPI_NEXT_RESOURCE(Resource))
    {
        if (Resource->Type == ACPI_RESOURCE_TYPE_IRQ)
        {
            for (i = 0; i < Resource->Data.Irq.InterruptCount && *Count < 16; i++)
            {
                if (Resource->Data.Irq.Interrupts[i])
                    Irqs[(*Count)++] = Resource->Data.Irq.Interrupts[i];
            }
        }
        else if (Resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
        {
            for (i = 0; i < Resource->Data.ExtendedIrq.InterruptCount && *Count < 16; i++)
            {
                /* (only legacy IRQs: the devices' Interrupt Line is a byte) */
                if (Resource->Data.ExtendedIrq.Interrupts[i] &&
                    Resource->Data.ExtendedIrq.Interrupts[i] < 16)
                {
                    Irqs[(*Count)++] = (UCHAR)Resource->Data.ExtendedIrq.Interrupts[i];
                }
            }
        }
    }

    AcpiOsFree(Buffer.Pointer);
}

/* Program a link (_SRS) with one IRQ, from its _PRS template */
static
BOOLEAN
SetLinkIrq(ACPI_HANDLE Handle, ULONG Irq)
{
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_RESOURCE *Resource;
    ACPI_STATUS Status;
    BOOLEAN Found = FALSE;

    Status = AcpiGetPossibleResources(Handle, &Buffer);
    if (ACPI_FAILURE(Status))
        return FALSE;

    /* Keep only the first interrupt descriptor, with the chosen IRQ */
    Resource = Buffer.Pointer;
    if (Resource->Type == ACPI_RESOURCE_TYPE_IRQ)
    {
        Resource->Data.Irq.InterruptCount = 1;
        Resource->Data.Irq.Interrupts[0] = (UINT8)Irq;
        Found = TRUE;
    }
    else if (Resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
    {
        Resource->Data.ExtendedIrq.InterruptCount = 1;
        Resource->Data.ExtendedIrq.Interrupts[0] = Irq;
        Found = TRUE;
    }

    if (Found)
    {
        /* End the list after it */
        ACPI_RESOURCE *End = ACPI_NEXT_RESOURCE(Resource);
        End->Type = ACPI_RESOURCE_TYPE_END_TAG;
        End->Length = sizeof(ACPI_RESOURCE);
        Buffer.Length = (ACPI_SIZE)((PUCHAR)End + sizeof(ACPI_RESOURCE) - (PUCHAR)Buffer.Pointer);

        Status = AcpiSetCurrentResources(Handle, &Buffer);
        Found = ACPI_SUCCESS(Status);
    }

    AcpiOsFree(Buffer.Pointer);
    return Found;
}

/* Note the devices of the root bus that a routing table entry routes through a link */
static
VOID
AddRoutedDevices(ACPI_HANDLE Root, ULONG Bus, ACPI_PCI_ROUTING_TABLE *Entry)
{
    ACPI_HANDLE LinkHandle;
    PPCI_LINK Link;
    PCI_SLOT_NUMBER Slot;
    UCHAR HeaderType, InterruptPin;
    USHORT VendorId;
    ULONG Device = (ULONG)(Entry->Address >> 16) & 0x1F;
    ULONG Function, Functions;

    if (!Entry->Source[0] || ACPI_FAILURE(AcpiGetHandle(Root, Entry->Source, &LinkHandle)))
        return;

    Link = FindLink(LinkHandle);
    if (!Link)
        return;

    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = Device;
    Slot.u.bits.FunctionNumber = 0;
    if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &VendorId,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), sizeof(VendorId)) != sizeof(VendorId) ||
        VendorId == 0xFFFF || VendorId == 0)
    {
        return;
    }
    HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &HeaderType,
                          FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType), sizeof(HeaderType));
    Functions = (HeaderType & PCI_MULTIFUNCTION) ? PCI_MAX_FUNCTION : 1;

    for (Function = 0; Function < Functions; Function++)
    {
        Slot.u.bits.FunctionNumber = Function;
        if (HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &VendorId,
                                  FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), sizeof(VendorId)) != sizeof(VendorId) ||
            VendorId == 0xFFFF)
        {
            continue;
        }
        HalGetBusDataByOffset(PCIConfiguration, Bus, Slot.u.AsULONG, &InterruptPin,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.InterruptPin), sizeof(InterruptPin));

        /* (_PRT pins count from 0 = INTA, the register from 1) */
        if (InterruptPin != Entry->Pin + 1 || Link->DeviceCount == MAX_LINK_DEVICES)
            continue;

        Link->Bus[Link->DeviceCount] = Bus;
        Link->Slots[Link->DeviceCount++] = Slot;
    }
}

static
ACPI_STATUS
ScanRootBridge(ACPI_HANDLE Handle, UINT32 Level, void *Context, void **ReturnValue)
{
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_PCI_ROUTING_TABLE *Entry;
    unsigned long long BusNumber = 0;

    /* (the devices behind bridges have routing tables of their own: only the root bus) */
    if (ACPI_FAILURE(AcpiGetIrqRoutingTable(Handle, &Buffer)))
        return AE_OK;

    if (ACPI_FAILURE(acpi_evaluate_integer(Handle, (ACPI_STRING)"_BBN", NULL, &BusNumber)))
        BusNumber = 0;

    for (Entry = Buffer.Pointer; Entry->Length; Entry = ACPI_ADD_PTR(ACPI_PCI_ROUTING_TABLE, Entry, Entry->Length))
        AddRoutedDevices(Handle, (ULONG)BusNumber, Entry);

    AcpiOsFree(Buffer.Pointer);
    return AE_OK;
}

/* IRQs of the other devices (ISA ones: keyboard, serial ports...): taken */
static
ACPI_STATUS
CountDeviceIrqs(ACPI_HANDLE Handle, UINT32 Level, void *Context, void **ReturnValue)
{
    PULONG Load = Context;
    UCHAR Irqs[16];
    ULONG Count, i;

    for (i = 0; i < LinkCount; i++)
    {
        if (Links[i].Handle == Handle)
            return AE_OK;
    }

    GetLinkIrqs(Handle, FALSE, Irqs, &Count);
    for (i = 0; i < Count; i++)
        Load[Irqs[i]] += 2;

    return AE_OK;
}

VOID
acpi_pci_balance_links(VOID)
{
    ULONG Load[16] = { 0 };
    UCHAR Irqs[16];
    ULONG Count, i, j, k, Best, BestLoad;
    PPCI_LINK Link;
    BOOLEAN Shared;

    LinkCount = 0;
    AcpiGetDevices("PNP0A03", ScanRootBridge, NULL, NULL);
    AcpiGetDevices("PNP0A08", ScanRootBridge, NULL, NULL);

    /* What each link has now */
    for (i = 0; i < LinkCount; i++)
    {
        Link = &Links[i];
        GetLinkIrqs(Link->Handle, FALSE, Irqs, &Count);
        Link->CurrentIrq = Count ? Irqs[0] : 0;
        GetLinkIrqs(Link->Handle, TRUE, Link->Possible, &Link->PossibleCount);
        if (Link->CurrentIrq)
            Load[Link->CurrentIrq] += Link->DeviceCount;
        DPRINT("Link %p: IRQ %lu, %lu devices, %lu possible IRQs\n",
               Link->Handle, Link->CurrentIrq, Link->DeviceCount, Link->PossibleCount);
    }

    AcpiGetDevices(NULL, CountDeviceIrqs, Load, NULL);

    /* The ACPI interrupt (SCI) is busy too */
    if (AcpiGbl_FADT.SciInterrupt < 16)
        Load[AcpiGbl_FADT.SciInterrupt]++;

    for (i = 0; i < LinkCount; i++)
    {
        Link = &Links[i];
        if (!Link->DeviceCount || !Link->CurrentIrq || !Link->PossibleCount)
            continue;

        /* Does another link with devices share this IRQ? */
        Shared = FALSE;
        for (j = 0; j < LinkCount; j++)
        {
            if (j != i && Links[j].DeviceCount && Links[j].CurrentIrq == Link->CurrentIrq)
                Shared = TRUE;
        }
        if (!Shared)
            continue;

        /* The possible IRQ with the fewest devices (without this link's own) */
        Load[Link->CurrentIrq] -= Link->DeviceCount;
        Best = Link->CurrentIrq;
        BestLoad = Load[Best];
        for (k = 0; k < Link->PossibleCount; k++)
        {
            if (Load[Link->Possible[k]] < BestLoad)
            {
                Best = Link->Possible[k];
                BestLoad = Load[Best];
            }
        }

        if (Best != Link->CurrentIrq && SetLinkIrq(Link->Handle, Best))
        {
            DPRINT1("PCI interrupt link %p: IRQ %lu -> %lu (%lu devices)\n",
                    Link->Handle, Link->CurrentIrq, Best, Link->DeviceCount);
            Link->CurrentIrq = Best;

            /* The PCI driver reports the Interrupt Line */
            for (j = 0; j < Link->DeviceCount; j++)
            {
                UCHAR Line = (UCHAR)Best;
                HalSetBusDataByOffset(PCIConfiguration, Link->Bus[j], Link->Slots[j].u.AsULONG, &Line,
                                      FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.InterruptLine), sizeof(Line));
            }
        }
        Load[Link->CurrentIrq] += Link->DeviceCount;
    }
}
