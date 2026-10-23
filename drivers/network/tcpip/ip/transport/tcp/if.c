
#include "precomp.h"

#include "lwip/pbuf.h"
#include "lwip/netifapi.h"
#include "lwip/ip.h"
#include "lwip/api.h"
#include "lwip/tcpip.h"
#include <ipifcons.h>

/* Large sends: consecutive full-size segments of a connection that lwIP sends
 * while the core lock is held are gathered into one frame the adapter cuts
 * up again (IP_INTERFACE.LargeSendMax): one packet, one copy and one
 * transmit (a register write, a VM exit when virtualized) for up to ~44
 * segments. TCPFlushLargeSend sends the frame when the core lock is released
 * or the next segment doesn't follow. Protected by the core lock */
typedef struct _LARGE_SEND
{
    PNEIGHBOR_CACHE_ENTRY NCE;
    PUCHAR Frame;           /* IP and TCP headers of the first segment, the data of all */
    ULONG Size;             /* Allocated */
    ULONG Length;
    ULONG HeaderLength;
    ULONG Mss;              /* Data of a segment */
    ULONG Segments;
    ULONG NextSeq;
    BOOLEAN LastShort;      /* The last segment was not full: nothing may follow */
} LARGE_SEND;

static LARGE_SEND LargeSend;

/* Room for the link header in front of the data (LANTransmit) */
#define LARGE_SEND_HEADROOM 16

static
err_t
TCPSendFrame(PNEIGHBOR_CACHE_ENTRY NCE, PNDIS_PACKET NdisPacket, PVOID Header,
             ULONG TotalLength, ULONG Mss)
{
    IP_PACKET Packet;
    PIPv4_HEADER IpHeader = Header;

    IPInitializePacket(&Packet, IP_ADDRESS_V4);
    Packet.NdisPacket = NdisPacket;
    Packet.Header = Header;
    Packet.MappedHeader = TRUE;
    Packet.HeaderSize = sizeof(IPv4_HEADER);
    Packet.TotalSize = TotalLength;
    Packet.SrcAddr.Type = IP_ADDRESS_V4;
    Packet.SrcAddr.Address.IPv4Address = IpHeader->SrcAddr;
    Packet.DstAddr.Type = IP_ADDRESS_V4;
    Packet.DstAddr.Address.IPv4Address = IpHeader->DstAddr;

    NDIS_PER_PACKET_INFO_FROM_PACKET(NdisPacket, TcpLargeSendPacketInfo) = UlongToPtr(Mss);

    if (Mss || TotalLength <= NCE->Interface->MTU)
        return NT_SUCCESS(IPSendDatagramDirect(&Packet, NCE)) ? ERR_OK : ERR_RTE;

    return NT_SUCCESS(IPSendDatagram(&Packet, NCE)) ? ERR_OK : ERR_RTE;
}

static
USHORT
GetU16(PUCHAR p)
{
    return (USHORT)((p[0] << 8) | p[1]);
}

static
ULONG
GetU32(PUCHAR p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

VOID
TCPFlushLargeSend(VOID)
{
    PNDIS_PACKET NdisPacket;
    PNDIS_BUFFER Buffer;
    NDIS_STATUS NdisStatus;
    PUCHAR Ip, Tcp;
    ULONG Sum, Mss = 0;

    if (!LargeSend.Frame)
        return;

    Ip = LargeSend.Frame;
    Tcp = Ip + (Ip[0] & 0x0F) * 4;

    if (LargeSend.Segments > 1)
    {
        /* The adapter fills in the length, identification and checksum of
         * each segment's IP header, and its TCP checksum from the
         * pseudo-header sum without the length */
        Ip[2] = (UCHAR)(LargeSend.Length >> 8);
        Ip[3] = (UCHAR)LargeSend.Length;
        Ip[10] = Ip[11] = 0;
        Sum = GetU16(Ip + 12) + GetU16(Ip + 14) + GetU16(Ip + 16) + GetU16(Ip + 18) + IPPROTO_TCP;
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
        Tcp[16] = (UCHAR)(Sum >> 8);
        Tcp[17] = (UCHAR)Sum;
        Mss = LargeSend.Mss;
    }

    NdisAllocatePacket(&NdisStatus, &NdisPacket, GlobalPacketPool);
    if (NdisStatus == NDIS_STATUS_SUCCESS)
    {
        NdisAllocateBuffer(&NdisStatus, &Buffer, GlobalBufferPool, LargeSend.Frame, LargeSend.Length);
        if (NdisStatus == NDIS_STATUS_SUCCESS)
        {
            NdisChainBufferAtFront(NdisPacket, Buffer);
            PC(NdisPacket)->Headroom = LARGE_SEND_HEADROOM;
            LargeSend.Frame = NULL;
            /* (FreeNdisPacket frees the frame) */
            TCPSendFrame(LargeSend.NCE, NdisPacket, Ip, LargeSend.Length, Mss);
            return;
        }
        NdisFreePacket(NdisPacket);
    }

    /* (lost: TCP sends it again) */
    ExFreePoolWithTag(LargeSend.Frame - LARGE_SEND_HEADROOM, PACKET_BUFFER_TAG);
    LargeSend.Frame = NULL;
}

/* Gather a segment into the large send, if it can be: TRUE if it was */
static
BOOLEAN
TCPGatherSegment(PNEIGHBOR_CACHE_ENTRY NCE, struct pbuf *p)
{
    UCHAR Headers[60 + 60];
    PUCHAR Ip = Headers, Tcp, PendingTcp;
    ULONG IpHeaderLength, HeaderLength, DataLength, Seq;
    UCHAR Flags;

    if (!NCE->Interface->LargeSendMax || p->tot_len < 40)
        return FALSE;

    if (pbuf_copy_partial(p, Headers, min(p->tot_len, sizeof(Headers)), 0) < 40)
        return FALSE;

    /* IPv4 without options, not a fragment; TCP with ACK (and PSH) only */
    IpHeaderLength = (Ip[0] & 0x0F) * 4;
    if (Ip[0] != 0x45 || Ip[9] != IPPROTO_TCP || (GetU16(Ip + 6) & 0x3FFF))
        return FALSE;
    Tcp = Ip + IpHeaderLength;
    HeaderLength = IpHeaderLength + (Tcp[12] >> 4) * 4;
    Flags = Tcp[13];
    if ((Flags & ~0x08) != 0x10 || HeaderLength > p->tot_len || HeaderLength > sizeof(Headers))
        return FALSE;
    DataLength = p->tot_len - HeaderLength;
    if (!DataLength)
        return FALSE;
    Seq = GetU32(Tcp + 4);

    if (LargeSend.Frame)
    {
        PendingTcp = LargeSend.Frame + IpHeaderLength;

        /* The next data of the same connection, same headers otherwise
         * (acknowledgement, window, options) */
        if (LargeSend.NCE == NCE && !LargeSend.LastShort &&
            LargeSend.HeaderLength == HeaderLength &&
            Seq == LargeSend.NextSeq && DataLength <= LargeSend.Mss &&
            LargeSend.Length + DataLength <= NCE->Interface->LargeSendMax &&
            RtlCompareMemory(LargeSend.Frame + 12, Ip + 12, 8) == 8 &&
            RtlCompareMemory(PendingTcp, Tcp, 4) == 4 &&
            RtlCompareMemory(PendingTcp + 8, Tcp + 8, 5) == 5 &&
            RtlCompareMemory(PendingTcp + 14, Tcp + 14, 2) == 2)
        {
            pbuf_copy_partial(p, LargeSend.Frame + LargeSend.Length, DataLength, HeaderLength);
            LargeSend.Length += DataLength;
            LargeSend.NextSeq += DataLength;
            LargeSend.Segments++;
            LargeSend.LastShort = (DataLength < LargeSend.Mss);
            PendingTcp[13] |= Flags;    /* (PSH) */
            return TRUE;
        }

        TCPFlushLargeSend();
    }

    /* Start a new one with this segment */
    LargeSend.Size = NCE->Interface->LargeSendMax;
    LargeSend.Frame = ExAllocatePoolWithTag(NonPagedPool, LARGE_SEND_HEADROOM + LargeSend.Size, PACKET_BUFFER_TAG);
    if (!LargeSend.Frame)
        return FALSE;
    LargeSend.Frame += LARGE_SEND_HEADROOM;
    pbuf_copy_partial(p, LargeSend.Frame, p->tot_len, 0);
    LargeSend.NCE = NCE;
    LargeSend.Length = p->tot_len;
    LargeSend.HeaderLength = HeaderLength;
    LargeSend.Mss = DataLength;
    LargeSend.Segments = 1;
    LargeSend.NextSeq = Seq + DataLength;
    LargeSend.LastShort = FALSE;
    return TRUE;
}

err_t
TCPSendDataCallback(struct netif *netif, struct pbuf *p, const ip4_addr_t *dest)
{
    NDIS_STATUS NdisStatus;
    PNEIGHBOR_CACHE_ENTRY NCE;
    IP_ADDRESS RemoteAddress;
    PIPv4_HEADER Header;
    PNDIS_PACKET NdisPacket;
    PCHAR Data;

    /* The caller frees the pbuf struct */

    if (((*(u8_t*)p->payload) & 0xF0) != 0x40)
        return ERR_IF;

    Header = p->payload;
    RemoteAddress.Type = IP_ADDRESS_V4;
    RemoteAddress.Address.IPv4Address = Header->DstAddr;

    if (!(NCE = RouteGetRouteToDestination(&RemoteAddress)))
    {
        return ERR_RTE;
    }

    if (TCPGatherSegment(NCE, p))
        return ERR_OK;

    /* (keep the order) */
    TCPFlushLargeSend();

    NdisStatus = AllocatePacketWithHeadroom(&NdisPacket, p->tot_len, LARGE_SEND_HEADROOM, &Data);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        return ERR_MEM;
    }

    pbuf_copy_partial(p, Data, p->tot_len, 0);

    return TCPSendFrame(NCE, NdisPacket, Data, p->tot_len, 0);
}

VOID
TCPUpdateInterfaceLinkStatus(PIP_INTERFACE IF)
{
    ULONG OperationalStatus;

    GetInterfaceConnectionStatus(IF, &OperationalStatus);

    LOCK_TCPIP_CORE();
    if (OperationalStatus == MIB_IF_OPER_STATUS_OPERATIONAL)
        netif_set_link_up(IF->TCPContext);
    else
        netif_set_link_down(IF->TCPContext);
    UNLOCK_TCPIP_CORE();
}

err_t
TCPInterfaceInit(struct netif *netif)
{
    PIP_INTERFACE IF = netif->state;

    netif->hwaddr_len = IF->AddressLength;
    RtlCopyMemory(netif->hwaddr, IF->Address, netif->hwaddr_len);

    netif->output = TCPSendDataCallback;
    netif->mtu = IF->MTU;

    netif->name[0] = 'e';
    netif->name[1] = 'n';

    netif->flags |= NETIF_FLAG_BROADCAST;

    TCPUpdateInterfaceLinkStatus(IF);

    TCPUpdateInterfaceIPInformation(IF);

    return 0;
}

VOID
TCPRegisterInterface(PIP_INTERFACE IF)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    gw.addr = 0;
    ipaddr.addr = 0;
    netmask.addr = 0;

    /* (lwIP's interface list belongs to the holder of its core lock) */
    LOCK_TCPIP_CORE();
    IF->TCPContext = netif_add(IF->TCPContext,
                               &ipaddr,
                               &netmask,
                               &gw,
                               IF,
                               TCPInterfaceInit,
                               tcpip_input);
    UNLOCK_TCPIP_CORE();
}

VOID
TCPUnregisterInterface(PIP_INTERFACE IF)
{
    LOCK_TCPIP_CORE();
    netif_remove(IF->TCPContext);
    UNLOCK_TCPIP_CORE();
}

VOID
TCPUpdateInterfaceIPInformation(PIP_INTERFACE IF)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    gw.addr = 0;

    GetInterfaceIPv4Address(IF,
                            ADE_UNICAST,
                            (PULONG)&ipaddr.addr);

    GetInterfaceIPv4Address(IF,
                            ADE_ADDRMASK,
                            (PULONG)&netmask.addr);

    LOCK_TCPIP_CORE();
    netif_set_addr(IF->TCPContext, &ipaddr, &netmask, &gw);

    if (ipaddr.addr != 0)
    {
        netif_set_up(IF->TCPContext);
        netif_set_default(IF->TCPContext);
    }
    else
    {
        netif_set_down(IF->TCPContext);
    }
    UNLOCK_TCPIP_CORE();
}
