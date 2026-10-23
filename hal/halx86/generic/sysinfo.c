/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/generic/sysinfo.c
 * PURPOSE:         HAL Information Routines
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Vadim Galyant (vgal@rambler.ru)
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
typedef unsigned int UINT;
#include <x86x64/Cpuid.h>

#define NDEBUG
#include <debug.h>

HAL_AMLI_BAD_IO_ADDRESS_LIST HalAMLIBadIOAddressList[] =
{
    { 0x0000, 0x10, 1, NULL }, // DMA controller
    { 0x0020, 0x02, 0, NULL }, // Programmable Interrupt Controller (8259A)
    { 0x0040, 0x04, 1, NULL }, // System Timer 1
    { 0x0048, 0x04, 1, NULL }, // System Timer 2 failsafe
    { 0x0070, 0x02, 1, NULL }, // Real-time clock
    { 0x0074, 0x03, 1, NULL }, // Extended CMOS
    { 0x0081, 0x03, 1, NULL }, // DMA 1 page registers
    { 0x0087, 0x01, 1, NULL }, // DMA 1 Ch 0 low page
    { 0x0089, 0x01, 1, NULL }, // DMA 2 page registers
    { 0x008A, 0x02, 1, NULL }, // DMA 2 page registers
    { 0x008F, 0x01, 1, NULL }, // DMA 2 low page refresh
    { 0x0090, 0x02, 1, NULL }, // Arbitration control
    { 0x0093, 0x02, 1, NULL }, // Reserved system board setup
    { 0x0096, 0x02, 1, NULL }, // POS channel select
    { 0x00A0, 0x02, 0, NULL }, // Cascaded PIC
    { 0x00C0, 0x20, 1, NULL }, // ISA DMA
    { 0x04D0, 0x02, 0, NULL }, // PIC edge/level registers
    { 0x0CF8, 0x08, 1, &HaliHandlePCIConfigSpaceAccess }, // PCI configuration space
    { 0x0000, 0x00, 0, NULL } // Reserved
};

/* FUNCTIONS ******************************************************************/

/* The address that AML code wrote to the configuration mechanism #1 port */
static ULONG HalpAmliPciConfigAddress;

/*
 * AML code (through the ACPI driver) may not touch ports 0xCF8-0xCFF itself:
 * emulate configuration mechanism #1 with the HAL's PCI access functions.
 * An access outside these ports is not ours: STATUS_UNSUCCESSFUL, as on
 * Windows (kmtest HalSystemInfo).
 */
NTSTATUS
NTAPI
HaliHandlePCIConfigSpaceAccess(_In_ BOOLEAN IsRead,
                               _In_ ULONG Port,
                               _In_ ULONG Length,
                               _Inout_ PULONG Buffer)
{
    PCI_SLOT_NUMBER Slot;
    ULONG Address, BusNumber, Offset;

    if ((Port < 0xCF8) || (Port > 0xCFF) || (Length == 0) || (Length > 4) ||
        (Port + Length > 0xD00))
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* The address register */
    if (Port < 0xCFC)
    {
        if ((Port != 0xCF8) || (Length != 4))
            return STATUS_UNSUCCESSFUL;
        if (IsRead)
            *Buffer = HalpAmliPciConfigAddress;
        else
            HalpAmliPciConfigAddress = *Buffer;
        return STATUS_SUCCESS;
    }

    /* The data register: read/write the selected configuration space */
    Address = HalpAmliPciConfigAddress;
    if (!(Address & 0x80000000))
    {
        if (IsRead) *Buffer = MAXULONG;
        return STATUS_SUCCESS;
    }
    BusNumber = (Address >> 16) & 0xFF;
    Slot.u.AsULONG = 0;
    Slot.u.bits.DeviceNumber = (Address >> 11) & 0x1F;
    Slot.u.bits.FunctionNumber = (Address >> 8) & 0x7;
    Offset = (Address & 0xFC) + (Port - 0xCFC);
    if (IsRead)
    {
        *Buffer = 0;
        HalGetBusDataByOffset(PCIConfiguration, BusNumber, Slot.u.AsULONG,
                              Buffer, Offset, Length);
    }
    else
    {
        HalSetBusDataByOffset(PCIConfiguration, BusNumber, Slot.u.AsULONG,
                              Buffer, Offset, Length);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
HaliQueryProcessorBrandString(
    _Out_writes_(BrandStringLength) PCHAR BrandString,
    _In_ ULONG BrandStringLength,
    _Out_ PULONG ReturnedLength)
{
    CPUID_EXTENDED_FUNCTION_REGS ExtendedFunction;
    union
    {
        INT AsInt[3 * 4];
        CHAR Chars[49];
    } BrandStringUnion;

    *ReturnedLength = sizeof(BrandStringUnion.Chars);
    if (BrandStringLength < *ReturnedLength)
    {
        DPRINT1("HaliQueryProcessorBrandString: Buffer too small (%u < %u)\n", BrandStringLength, *ReturnedLength);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    /* Check if we can query the brand string */
    __cpuid(ExtendedFunction.AsInt32, CPUID_EXTENDED_FUNCTION);
    if (ExtendedFunction.MaxLeaf < CPUID_BRAND_STRING3)
    {
        return STATUS_NOT_SUPPORTED;
    }

    __cpuid(&BrandStringUnion.AsInt[0], CPUID_BRAND_STRING1);
    __cpuid(&BrandStringUnion.AsInt[4], CPUID_BRAND_STRING2);
    __cpuid(&BrandStringUnion.AsInt[8], CPUID_BRAND_STRING3);
    BrandStringUnion.Chars[48] = '\0'; // Ensure null-termination

    /* Copy the brand string to the output buffer */
    RtlCopyMemory(BrandString,
                  BrandStringUnion.Chars,
                  sizeof(BrandStringUnion.Chars));

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HaliQuerySystemInformation(IN HAL_QUERY_INFORMATION_CLASS InformationClass,
                           IN ULONG BufferSize,
                           IN OUT PVOID Buffer,
                           OUT PULONG ReturnedLength)
{
#define REPORT_THIS_CASE(X) case X: DPRINT1("Unhandled case: %s\n", #X); break
    switch (InformationClass)
    {
        REPORT_THIS_CASE(HalInstalledBusInformation);
        REPORT_THIS_CASE(HalProfileSourceInformation);
        REPORT_THIS_CASE(HalInformationClassUnused1);
        REPORT_THIS_CASE(HalPowerInformation);
        REPORT_THIS_CASE(HalProcessorSpeedInformation);
        REPORT_THIS_CASE(HalCallbackInformation);
        REPORT_THIS_CASE(HalMapRegisterInformation);
        REPORT_THIS_CASE(HalMcaLogInformation);
        case HalFrameBufferCachingInformation:
        {
            /* FIXME: TODO */
            return STATUS_NOT_IMPLEMENTED;
        }
        REPORT_THIS_CASE(HalDisplayBiosInformation);
        REPORT_THIS_CASE(HalProcessorFeatureInformation);
        REPORT_THIS_CASE(HalNumaTopologyInterface);
        REPORT_THIS_CASE(HalErrorInformation);
        REPORT_THIS_CASE(HalCmcLogInformation);
        REPORT_THIS_CASE(HalCpeLogInformation);
        REPORT_THIS_CASE(HalQueryMcaInterface);
        case HalQueryAMLIIllegalIOPortAddresses:
        {
            ULONG Size = sizeof(HalAMLIBadIOAddressList);
            NTSTATUS Status = STATUS_INFO_LENGTH_MISMATCH;

            if (BufferSize >= Size)
            {
                RtlCopyMemory(Buffer, HalAMLIBadIOAddressList, Size);
                Status = STATUS_SUCCESS;
            }

            *ReturnedLength = Size;
            KeFlushWriteBuffer();
            return Status;
        }

        REPORT_THIS_CASE(HalQueryMaxHotPlugMemoryAddress);
        REPORT_THIS_CASE(HalPartitionIpiInterface);
        REPORT_THIS_CASE(HalPlatformInformation);
        REPORT_THIS_CASE(HalQueryProfileSourceList);
        REPORT_THIS_CASE(HalInitLogInformation);
        REPORT_THIS_CASE(HalFrequencyInformation);
        case HalProcessorBrandString:
        {
            return HaliQueryProcessorBrandString((PCHAR)Buffer, BufferSize, ReturnedLength);
        }
        REPORT_THIS_CASE(HalHypervisorInformation);
        REPORT_THIS_CASE(HalPlatformTimerInformation);
        REPORT_THIS_CASE(HalAcpiAuditInformation);
    }
#undef REPORT_THIS_CASE

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HaliSetSystemInformation(IN HAL_SET_INFORMATION_CLASS InformationClass,
                         IN ULONG BufferSize,
                         IN OUT PVOID Buffer)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}
