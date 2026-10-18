/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Sending packets
 * COPYRIGHT:   Copyright 2018 Mark Jansen <mark.jansen@reactos.org>
 *              Copyright 2019 Victor Perevertkin <victor.perevertkin@reactos.org>
 */

#include "nic.h"

#include <debug.h>

/* Free descriptors in the transmit ring */
static
ULONG
NICFreeTransmitDescriptors(
    _In_ PE1000_ADAPTER Adapter)
{
    if (Adapter->TxFull)
        return 0;

    return (Adapter->LastTxDesc + NUM_TRANSMIT_DESCRIPTORS - Adapter->CurrentTxDesc - 1) % NUM_TRANSMIT_DESCRIPTORS + 1;
}

static
VOID
NICAdvanceTransmitDescriptor(
    _In_ PE1000_ADAPTER Adapter)
{
    Adapter->CurrentTxDesc = (Adapter->CurrentTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;
    if (Adapter->CurrentTxDesc == Adapter->LastTxDesc)
    {
        NDIS_DbgPrint(MID_TRACE, ("All TX descriptors are full now\n"));
        Adapter->TxFull = TRUE;
    }
}

/* A large TCP send (NDIS large send offload, version 1): the card cuts the
 * payload into segments of Mss bytes, giving each a copy of the headers with
 * the IP length and identification, the TCP sequence number and both
 * checksums filled in. The transport sends one IPv4/TCP frame with the TCP
 * checksum field holding the pseudo-header sum without the length */
static
NDIS_STATUS
NICTransmitLargeSend(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_PACKET Packet,
    _In_ PSCATTER_GATHER_LIST SgList,
    _In_ ULONG Mss)
{
    volatile PE1000_CONTEXT_DESCRIPTOR Context;
    volatile PE1000_DATA_DESCRIPTOR Data;
    PNDIS_BUFFER Buffer;
    PUCHAR Frame;
    UINT FrameLength, BufferLength;
    ULONG IpHeaderLength, HeaderLength, i;

    NdisQueryPacket(Packet, NULL, NULL, &Buffer, &FrameLength);
    NdisQueryBufferSafe(Buffer, (PVOID *)&Frame, &BufferLength, HighPagePriority);
    if (!Frame || BufferLength < sizeof(ETH_HEADER) + 20 + 20)
        return NDIS_STATUS_FAILURE;

    /* Ethernet + IPv4 + TCP headers, all in the first buffer */
    IpHeaderLength = (Frame[sizeof(ETH_HEADER)] & 0x0F) * 4;
    HeaderLength = sizeof(ETH_HEADER) + IpHeaderLength +
                   (Frame[sizeof(ETH_HEADER) + IpHeaderLength + 12] >> 4) * 4;
    if (HeaderLength > BufferLength || HeaderLength >= FrameLength)
        return NDIS_STATUS_FAILURE;

    if (NICFreeTransmitDescriptors(Adapter) < SgList->NumberOfElements + 1)
        return NDIS_STATUS_RESOURCES;

    Context = (PE1000_CONTEXT_DESCRIPTOR)(Adapter->TransmitDescriptors + Adapter->CurrentTxDesc);
    Context->IpChecksumStart = sizeof(ETH_HEADER);
    Context->IpChecksumOffset = sizeof(ETH_HEADER) + 10;
    Context->IpChecksumEnd = (USHORT)(sizeof(ETH_HEADER) + IpHeaderLength - 1);
    Context->TcpChecksumStart = (UCHAR)(sizeof(ETH_HEADER) + IpHeaderLength);
    Context->TcpChecksumOffset = (UCHAR)(sizeof(ETH_HEADER) + IpHeaderLength + 16);
    Context->TcpChecksumEnd = 0;
    Context->CommandAndLength = (FrameLength - HeaderLength) | E1000_TXD_CMD_DEXT |
                                E1000_TXD_CMD_CTX_TSE | E1000_TXD_CMD_IP | E1000_TXD_CMD_TCP |
                                E1000_TXD_CMD_RS;
    Context->Status = 0;
    Context->HeaderLength = (UCHAR)HeaderLength;
    Context->Mss = (USHORT)Mss;
    Adapter->TransmitPackets[Adapter->CurrentTxDesc] = NULL;
    NICAdvanceTransmitDescriptor(Adapter);

    for (i = 0; i < SgList->NumberOfElements; i++)
    {
        Data = (PE1000_DATA_DESCRIPTOR)(Adapter->TransmitDescriptors + Adapter->CurrentTxDesc);
        Data->Address = SgList->Elements[i].Address.QuadPart;
        Data->CommandAndLength = SgList->Elements[i].Length | E1000_TXD_DTYP_DATA |
                                 E1000_TXD_CMD_DEXT | E1000_TXD_CMD_TSE | E1000_TXD_CMD_IFCS |
                                 E1000_TXD_CMD_RS | E1000_TXD_CMD_IDE;
        if (i == SgList->NumberOfElements - 1)
            Data->CommandAndLength |= E1000_TXD_CMD_EOP;
        Data->Status = 0;
        Data->Options = E1000_TXD_POPTS_IXSM | E1000_TXD_POPTS_TXSM;
        Data->Special = 0;

        /* (completed with its last descriptor) */
        Adapter->TransmitPackets[Adapter->CurrentTxDesc] =
            (i == SgList->NumberOfElements - 1) ? Packet : NULL;
        NICAdvanceTransmitDescriptor(Adapter);
    }

    /* Report how much payload was sent */
    NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, TcpLargeSendPacketInfo) =
        UlongToPtr(FrameLength - HeaderLength);

    E1000WriteUlong(Adapter, E1000_REG_TDT, Adapter->CurrentTxDesc);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
MiniportSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_PACKET Packet,
    _In_ UINT Flags)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;
    PSCATTER_GATHER_LIST sgList;
    ULONG Mss, i;
    NDIS_STATUS Status;

    sgList = NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, ScatterGatherListPacketInfo);

    ASSERT(sgList != NULL);
    ASSERT(sgList->NumberOfElements >= 1);

    if (Adapter->TxFull)
    {
        NDIS_DbgPrint(MIN_TRACE, ("All TX descriptors are full\n"));
        return NDIS_STATUS_RESOURCES;
    }

    Mss = PtrToUlong(NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, TcpLargeSendPacketInfo));
    if (Mss != 0)
    {
        Status = NICTransmitLargeSend(Adapter, Packet, sgList, Mss);
        return (Status == NDIS_STATUS_SUCCESS) ? NDIS_STATUS_PENDING : Status;
    }

    /* One descriptor per buffer of the packet (a frame may straddle a page) */
    if (NICFreeTransmitDescriptors(Adapter) < sgList->NumberOfElements)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < sgList->NumberOfElements; i++)
    {
        ASSERT(sgList->Elements[i].Length <= MAXIMUM_FRAME_SIZE);

        TransmitDescriptor = Adapter->TransmitDescriptors + Adapter->CurrentTxDesc;
        TransmitDescriptor->Address = sgList->Elements[i].Address.QuadPart;
        TransmitDescriptor->Length = (USHORT)sgList->Elements[i].Length;
        TransmitDescriptor->ChecksumOffset = 0;
        TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS | E1000_TDESC_CMD_IDE;
        if (i == sgList->NumberOfElements - 1)
            TransmitDescriptor->Command |= E1000_TDESC_CMD_EOP;
        TransmitDescriptor->Status = 0;
        TransmitDescriptor->ChecksumStartField = 0;
        TransmitDescriptor->Special = 0;

        Adapter->TransmitPackets[Adapter->CurrentTxDesc] =
            (i == sgList->NumberOfElements - 1) ? Packet : NULL;
        NICAdvanceTransmitDescriptor(Adapter);
    }

    E1000WriteUlong(Adapter, E1000_REG_TDT, Adapter->CurrentTxDesc);

    return NDIS_STATUS_PENDING;
}
