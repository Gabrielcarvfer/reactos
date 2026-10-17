/*
 * PROJECT:     ReactOS Kernel-Mode Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for scatter/gather DMA of 32-bit bus masters
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira
 */

/*
 * A 32-bit bus master (e.g. a PCI IDE controller) must never get a
 * scatter/gather element above 4 GB. When the buffer lies above 4 GB the
 * HAL has to double-buffer it through map registers below 4 GB:
 * - for a write to the device, the data must already be in the map buffer
 *   when the S/G list is handed out;
 * - for a read from the device, the data the device put in the map buffer
 *   must be copied back into the caller's buffer by PutScatterGatherList.
 * The test mixes pages below and above 4 GB in one MDL and starts the
 * transfer in the middle of a page.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'tDmH'
#define HALF_PAGES 8
#define TOTAL_PAGES (2 * HALF_PAGES)
#define START_OFFSET 0x200
#define TRANSFER_LENGTH (TOTAL_PAGES * PAGE_SIZE - 2 * START_OFFSET)
#define MAX_ELEMENTS 64

typedef struct _DMA_TEST_CONTEXT
{
    KEVENT Event;
    PSCATTER_GATHER_LIST SgList;
    ULONG NumberOfElements;
    SCATTER_GATHER_ELEMENT Elements[MAX_ELEMENTS];
} DMA_TEST_CONTEXT, *PDMA_TEST_CONTEXT;

static
VOID
NTAPI
DmaTestListControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PVOID Context)
{
    PDMA_TEST_CONTEXT TestContext = Context;
    ULONG i;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    TestContext->SgList = ScatterGather;
    TestContext->NumberOfElements = min(ScatterGather->NumberOfElements, MAX_ELEMENTS);
    for (i = 0; i < TestContext->NumberOfElements; i++)
        TestContext->Elements[i] = ScatterGather->Elements[i];

    KeSetEvent(&TestContext->Event, IO_NO_INCREMENT, FALSE);
}

static
UCHAR
Pattern(
    _In_ ULONG Offset,
    _In_ UCHAR Seed)
{
    return (UCHAR)((Offset * 7) ^ (Offset >> 8) ^ Seed);
}

/* Returns TRUE if the element lies inside one of the test's own pages */
static
BOOLEAN
IsOwnPage(
    _In_ PMDL Mdl,
    _In_ ULONGLONG Address)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);
    ULONG i;

    for (i = 0; i < TOTAL_PAGES; i++)
    {
        if (Pages[i] == (PFN_NUMBER)(Address >> PAGE_SHIFT))
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
GetSgList(
    _In_ PDMA_ADAPTER Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ BOOLEAN WriteToDevice,
    _Inout_ PDMA_TEST_CONTEXT TestContext)
{
    NTSTATUS Status;
    KIRQL OldIrql;
    LARGE_INTEGER Timeout;

    KeInitializeEvent(&TestContext->Event, NotificationEvent, FALSE);
    TestContext->SgList = NULL;
    TestContext->NumberOfElements = 0;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Status = Adapter->DmaOperations->GetScatterGatherList(Adapter,
                                                          DeviceObject,
                                                          Mdl,
                                                          MmGetMdlVirtualAddress(Mdl),
                                                          TRANSFER_LENGTH,
                                                          DmaTestListControl,
                                                          TestContext,
                                                          WriteToDevice);
    KeLowerIrql(OldIrql);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* The list may be built later, when map registers become available */
    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&TestContext->Event, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return (Status == STATUS_SUCCESS) && (TestContext->SgList != NULL);
}

static
VOID
PutSgList(
    _In_ PDMA_ADAPTER Adapter,
    _In_ PDMA_TEST_CONTEXT TestContext,
    _In_ BOOLEAN WriteToDevice)
{
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Adapter->DmaOperations->PutScatterGatherList(Adapter, TestContext->SgList, WriteToDevice);
    KeLowerIrql(OldIrql);
}

/* Checks the S/G list of the buffer; returns the number of bytes double-buffered */
static
ULONG
CheckSgList(
    _In_ PMDL Mdl,
    _In_ PDMA_TEST_CONTEXT TestContext)
{
    ULONG i, Total = 0, Bounced = 0;

    ok(TestContext->SgList->NumberOfElements <= MAX_ELEMENTS,
       "Too many elements: %lu\n", TestContext->SgList->NumberOfElements);

    for (i = 0; i < TestContext->NumberOfElements; i++)
    {
        PSCATTER_GATHER_ELEMENT Element = &TestContext->Elements[i];

        ok(Element->Address.HighPart == 0,
           "Element %lu: address 0x%I64x is above 4 GB (length 0x%lx)\n",
           i, Element->Address.QuadPart, Element->Length);
        ok(Element->Length != 0, "Element %lu: zero length\n", i);
        ok((Element->Address.LowPart & 1) == 0,
           "Element %lu: address 0x%I64x not word aligned\n", i, Element->Address.QuadPart);

        if (!IsOwnPage(Mdl, Element->Address.QuadPart))
            Bounced += Element->Length;
        Total += Element->Length;
    }
    ok_eq_ulong(Total, (ULONG)TRANSFER_LENGTH);
    return Bounced;
}

static
VOID
TestTransfer(
    _In_ PDMA_ADAPTER Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PUCHAR Buffer)
{
    DMA_TEST_CONTEXT *TestContext;
    ULONG i, j, Offset, Mismatches, Bounced;

    TestContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*TestContext), TAG_TEST);
    if (skip(TestContext != NULL, "No memory\n"))
        return;

    /* Write to the device: the device must see the caller's data */
    for (i = 0; i < TRANSFER_LENGTH; i++)
        Buffer[i] = Pattern(i, 0x5A);

    if (GetSgList(Adapter, DeviceObject, Mdl, TRUE, TestContext))
    {
        Bounced = CheckSgList(Mdl, TestContext);
        ok(Bounced != 0, "No page was double-buffered\n");

        Mismatches = 0;
        Offset = 0;
        for (i = 0; i < TestContext->NumberOfElements; i++)
        {
            PSCATTER_GATHER_ELEMENT Element = &TestContext->Elements[i];
            PUCHAR DeviceView;

            if (Element->Address.HighPart != 0 || IsOwnPage(Mdl, Element->Address.QuadPart))
            {
                /* Not a map buffer: the device reads the caller's page directly */
                Offset += Element->Length;
                continue;
            }

            DeviceView = MmMapIoSpace(Element->Address, Element->Length, MmNonCached);
            ok(DeviceView != NULL, "MmMapIoSpace failed\n");
            if (DeviceView)
            {
                for (j = 0; j < Element->Length; j++)
                {
                    if (DeviceView[j] != Pattern(Offset + j, 0x5A))
                        Mismatches++;
                }
                MmUnmapIoSpace(DeviceView, Element->Length);
            }
            Offset += Element->Length;
        }
        ok_eq_ulong(Mismatches, 0UL);

        PutSgList(Adapter, TestContext, TRUE);
    }

    /* Read from the device: what the device wrote must reach the caller's buffer */
    for (i = 0; i < TRANSFER_LENGTH; i++)
        Buffer[i] = 0xCC;

    if (GetSgList(Adapter, DeviceObject, Mdl, FALSE, TestContext))
    {
        CheckSgList(Mdl, TestContext);

        /* Simulate the device writing into each element */
        Offset = 0;
        for (i = 0; i < TestContext->NumberOfElements; i++)
        {
            PSCATTER_GATHER_ELEMENT Element = &TestContext->Elements[i];
            PUCHAR DeviceView;

            if (IsOwnPage(Mdl, Element->Address.QuadPart))
            {
                /* Direct DMA into the caller's buffer */
                for (j = 0; j < Element->Length; j++)
                    Buffer[Offset + j] = Pattern(Offset + j, 0xA5);
            }
            else if (Element->Address.HighPart == 0)
            {
                DeviceView = MmMapIoSpace(Element->Address, Element->Length, MmNonCached);
                ok(DeviceView != NULL, "MmMapIoSpace failed\n");
                if (DeviceView)
                {
                    for (j = 0; j < Element->Length; j++)
                        DeviceView[j] = Pattern(Offset + j, 0xA5);
                    MmUnmapIoSpace(DeviceView, Element->Length);
                }
            }
            Offset += Element->Length;
        }

        PutSgList(Adapter, TestContext, FALSE);

        Mismatches = 0;
        for (i = 0; i < TRANSFER_LENGTH; i++)
        {
            if (Buffer[i] != Pattern(i, 0xA5))
            {
                if (Mismatches == 0)
                    trace("First mismatch at offset 0x%lx: 0x%x\n", i, Buffer[i]);
                Mismatches++;
            }
        }
        ok_eq_ulong(Mismatches, 0UL);
    }

    ExFreePoolWithTag(TestContext, TAG_TEST);
}

START_TEST(HalDma)
{
    DEVICE_DESCRIPTION DeviceDescription;
    PDMA_ADAPTER Adapter;
    ULONG NumberOfMapRegisters = 0;
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL HighMdl = NULL, LowMdl = NULL, Mdl = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PPFN_NUMBER Pages;
    PUCHAR Buffer;
    NTSTATUS Status;
    ULONG i;

    /* Pages above 4 GB, if the machine has any */
    Low.QuadPart = 0x100000000LL;
    High.QuadPart = -1LL;
    Skip.QuadPart = 0;
    HighMdl = MmAllocatePagesForMdlEx(Low, High, Skip, HALF_PAGES * PAGE_SIZE, MmCached, 0);
    if (skip(HighMdl && MmGetMdlByteCount(HighMdl) == HALF_PAGES * PAGE_SIZE,
             "No physical memory above 4 GB, nothing to test\n"))
    {
        goto Cleanup;
    }

    Low.QuadPart = 0;
    High.QuadPart = 0xFFFFFFFFLL;
    LowMdl = MmAllocatePagesForMdlEx(Low, High, Skip, HALF_PAGES * PAGE_SIZE, MmCached, 0);
    if (skip(LowMdl && MmGetMdlByteCount(LowMdl) == HALF_PAGES * PAGE_SIZE, "No low memory\n"))
        goto Cleanup;

    /* Interleave low and high pages: L H L H ... */
    Mdl = IoAllocateMdl((PVOID)(ULONG_PTR)(0x10000 + START_OFFSET), TRANSFER_LENGTH, FALSE, FALSE, NULL);
    if (skip(Mdl != NULL, "IoAllocateMdl failed\n"))
        goto Cleanup;
    ok_eq_ulong(ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), TRANSFER_LENGTH), (ULONG)TOTAL_PAGES);

    Pages = MmGetMdlPfnArray(Mdl);
    for (i = 0; i < HALF_PAGES; i++)
    {
        Pages[2 * i] = MmGetMdlPfnArray(LowMdl)[i];
        Pages[2 * i + 1] = MmGetMdlPfnArray(HighMdl)[i];
    }
    Mdl->MdlFlags |= MDL_PAGES_LOCKED;

    Buffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (skip(Buffer != NULL, "Unable to map the buffer\n"))
        goto Cleanup;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No device object\n"))
        goto Cleanup;

    /* What pciidex asks for: a 32-bit PCI bus master with hardware S/G */
    RtlZeroMemory(&DeviceDescription, sizeof(DeviceDescription));
    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription.Master = TRUE;
    DeviceDescription.ScatterGather = TRUE;
    DeviceDescription.Dma32BitAddresses = TRUE;
    DeviceDescription.InterfaceType = PCIBus;
    DeviceDescription.MaximumLength = 0x20000;

    Adapter = IoGetDmaAdapter(NULL, &DeviceDescription, &NumberOfMapRegisters);
    ok(Adapter != NULL, "IoGetDmaAdapter failed\n");
    if (skip(Adapter != NULL, "No DMA adapter\n"))
        goto Cleanup;
    ok(NumberOfMapRegisters >= TOTAL_PAGES, "Only %lu map registers\n", NumberOfMapRegisters);

    /* Repeat to catch stale map register state between transfers */
    for (i = 0; i < 3; i++)
        TestTransfer(Adapter, DeviceObject, Mdl, Buffer);

    Adapter->DmaOperations->PutDmaAdapter(Adapter);

Cleanup:
    if (DeviceObject)
        IoDeleteDevice(DeviceObject);
    if (Mdl)
    {
        if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
            MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
        IoFreeMdl(Mdl);
    }
    if (LowMdl)
    {
        MmFreePagesFromMdl(LowMdl);
        ExFreePool(LowMdl);
    }
    if (HighMdl)
    {
        MmFreePagesFromMdl(HighMdl);
        ExFreePool(HighMdl);
    }
}
