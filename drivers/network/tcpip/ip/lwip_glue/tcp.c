#include <debug.h>
#include <lwip/tcpip.h>
#include <lwip/priv/tcp_priv.h>

#include "lwip_glue.h"

static const char * const tcp_state_str[] = {
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RCVD",
  "ESTABLISHED",
  "FIN_WAIT_1",
  "FIN_WAIT_2",
  "CLOSE_WAIT",
  "CLOSING",
  "LAST_ACK",
  "TIME_WAIT"
};

/* lwIP's raw API may only be called by one thread at a time: the holder of its
 * core lock (LWIP_TCPIP_CORE_LOCKING). Each LibTCP* function runs its
 * LibTCP*Callback under that lock in the calling thread. (They used to be
 * queued to the "tcpip thread" and waited for: two thread switches per
 * request.) The lock may be taken again by its owner, so lwIP's callbacks can
 * call LibTCP* functions. Requests they finish are completed when the lock is
 * released (CompleteBucket). */

extern KEVENT TerminationEvent;
extern NPAGED_LOOKASIDE_LIST MessageLookasideList;
extern NPAGED_LOOKASIDE_LIST QueueEntryLookasideList;

/* Required for ERR_T to NTSTATUS translation in receive error handling */
NTSTATUS TCPTranslateError(const err_t err);

void
LibTCPDumpPcb(PVOID SocketContext)
{
    struct tcp_pcb *pcb = (struct tcp_pcb*)SocketContext;
    unsigned int addr = lwip_ntohl(pcb->remote_ip.addr);

    DbgPrint("\tState: %s\n", tcp_state_str[pcb->state]);
    DbgPrint("\tRemote: (%d.%d.%d.%d, %d)\n",
    (addr >> 24) & 0xFF,
    (addr >> 16) & 0xFF,
    (addr >> 8) & 0xFF,
    addr & 0xFF,
    pcb->remote_port);
}

/* Set during a batch of received packets (TCPBeginReceiveBatch): tcp_input
 * leaves acknowledgements and sends to the end of the batch, so a batch of
 * segments is acknowledged once, not every second segment: each packet sent
 * costs (a register write per packet in the network card, a VM exit when
 * virtualized) */
static PKTHREAD DeferOutputThread;
static BOOLEAN OutputDeferred;

void
LibTCPBeginDeferOutput(void)
{
    DeferOutputThread = KeGetCurrentThread();
}

int
LibTCPDeferOutput(struct tcp_pcb *pcb)
{
    if (DeferOutputThread != KeGetCurrentThread())
        return 0;

    OutputDeferred = TRUE;
    return 1;
}

/* (under the core lock) */
void
LibTCPEndDeferOutput(void)
{
    struct tcp_pcb *pcb;

    DeferOutputThread = NULL;
    if (!OutputDeferred)
        return;
    OutputDeferred = FALSE;

    for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next)
    {
        /* Acknowledge what the batch brought now */
        if (pcb->flags & TF_ACK_DELAY)
        {
            tcp_clear_flags(pcb, TF_ACK_DELAY);
            tcp_ack_now(pcb);
        }
        tcp_output(pcb);
    }
}

void
LibTCPLockCore(void)
{
    LOCK_TCPIP_CORE();
}

void
LibTCPUnlockCore(void)
{
    UNLOCK_TCPIP_CORE();
}

static
void
LibTCPCall(tcpip_callback_fn Function, struct lwip_callback_msg *msg)
{
    LOCK_TCPIP_CORE();
    Function(msg);
    UNLOCK_TCPIP_CORE();
}

static
void
LibTCPEmptyQueue(PCONNECTION_ENDPOINT Connection)
{
    PLIST_ENTRY Entry;
    PQUEUE_ENTRY qp = NULL;

    ReferenceObject(Connection);

    while (!IsListEmpty(&Connection->PacketQueue))
    {
        Entry = RemoveHeadList(&Connection->PacketQueue);
        qp = CONTAINING_RECORD(Entry, QUEUE_ENTRY, ListEntry);

        pbuf_free(qp->p);

        ExFreeToNPagedLookasideList(&QueueEntryLookasideList, qp);
    }

    DereferenceObject(Connection);
}

void LibTCPEnqueuePacket(PCONNECTION_ENDPOINT Connection, struct pbuf *p)
{
    PQUEUE_ENTRY qp;

    qp = (PQUEUE_ENTRY)ExAllocateFromNPagedLookasideList(&QueueEntryLookasideList);
    qp->p = p;
    qp->Offset = 0;

    LockObject(Connection);
    InsertTailList(&Connection->PacketQueue, &qp->ListEntry);
    UnlockObject(Connection);
}

PQUEUE_ENTRY LibTCPDequeuePacket(PCONNECTION_ENDPOINT Connection)
{
    PLIST_ENTRY Entry;
    PQUEUE_ENTRY qp = NULL;

    if (IsListEmpty(&Connection->PacketQueue)) return NULL;

    Entry = RemoveHeadList(&Connection->PacketQueue);

    qp = CONTAINING_RECORD(Entry, QUEUE_ENTRY, ListEntry);

    return qp;
}

NTSTATUS LibTCPGetDataFromConnectionQueue(PCONNECTION_ENDPOINT Connection, PUCHAR RecvBuffer, UINT RecvLen, UINT *Received)
{
    PQUEUE_ENTRY qp;
    struct pbuf* p;
    NTSTATUS Status;
    UINT ReadLength, PayloadLength, Offset, Copied;

    (*Received) = 0;

    LockObject(Connection);

    if (!IsListEmpty(&Connection->PacketQueue))
    {
        while ((qp = LibTCPDequeuePacket(Connection)) != NULL)
        {
            p = qp->p;

            /* Calculate the payload length first */
            PayloadLength = p->tot_len;
            PayloadLength -= qp->Offset;
            Offset = qp->Offset;

            /* Check if we're reading the whole buffer */
            ReadLength = MIN(PayloadLength, RecvLen);
            ASSERT(ReadLength != 0);
            if (ReadLength != PayloadLength)
            {
                /* Save this one for later */
                qp->Offset += ReadLength;
                InsertHeadList(&Connection->PacketQueue, &qp->ListEntry);
                qp = NULL;
            }

            Copied = pbuf_copy_partial(p, RecvBuffer, ReadLength, Offset);
            ASSERT(Copied == ReadLength);

            /* Update trackers */
            RecvLen -= ReadLength;
            RecvBuffer += ReadLength;
            (*Received) += ReadLength;

            if (qp != NULL)
            {
                /* (pbufs are malloc'ed, and their reference counts are
                 * protected by SYS_ARCH_PROTECT: any thread may free them) */
                pbuf_free(qp->p);

                ExFreeToNPagedLookasideList(&QueueEntryLookasideList, qp);
            }
            else
            {
                /* If we get here, it means we've filled the buffer */
                ASSERT(RecvLen == 0);
            }

            ASSERT((*Received) != 0);
            Status = STATUS_SUCCESS;

            if (!RecvLen)
                break;
        }
    }
    else
    {
        if (Connection->ReceiveShutdown)
            Status = Connection->ReceiveShutdownStatus;
        else
            Status = STATUS_PENDING;
    }

    UnlockObject(Connection);

    return Status;
}

/* The reader took Length bytes out of the connection's queue: open the
 * receive window by as much. Call without the connection's lock (lwIP's
 * callbacks take it under the core lock) */
void
LibTCPRecved(PCONNECTION_ENDPOINT Connection, ULONG Length)
{
    PTCP_PCB pcb;
    u16_t Chunk;

    if (!Length)
        return;

    LOCK_TCPIP_CORE();
    pcb = Connection->SocketContext;
    if (pcb && pcb->state != LISTEN)
    {
        while (Length)
        {
            Chunk = (u16_t)MIN(Length, 0xFFFF);
            tcp_recved(pcb, Chunk);
            Length -= Chunk;
        }
    }
    UNLOCK_TCPIP_CORE();
}

static
BOOLEAN
WaitForEventSafely(PRKEVENT Event)
{
    PVOID WaitObjects[] = {Event, &TerminationEvent};

    if (KeWaitForMultipleObjects(2,
                                 WaitObjects,
                                 WaitAny,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 NULL,
                                 NULL) == STATUS_WAIT_0)
    {
        /* Signalled by the caller's event */
        return TRUE;
    }
    else /* if KeWaitForMultipleObjects() == STATUS_WAIT_1 */
    {
        /* Signalled by our termination event */
        return FALSE;
    }
}

static
err_t
InternalSendEventHandler(void *arg, PTCP_PCB pcb, const u16_t space)
{
    /* Make sure the socket didn't get closed */
    if (!arg) return ERR_OK;

    TCPSendEventHandler(arg, space);

    return ERR_OK;
}

static
err_t
InternalRecvEventHandler(void *arg, PTCP_PCB pcb, struct pbuf *p, const err_t err)
{
    PCONNECTION_ENDPOINT Connection = arg;

    /* Make sure the socket didn't get closed */
    if (!arg)
    {
        if (p)
            pbuf_free(p);

        return ERR_OK;
    }

    if (p)
    {
        /* The window opens again when the data is read (LibTCPRecved), so a
         * reader that falls behind stops the sender instead of piling the
         * data up here */
        LibTCPEnqueuePacket(Connection, p);

        TCPQueueRecvNotify(Connection);
    }
    else if (err == ERR_OK)
    {
        /* Complete pending reads with 0 bytes to indicate a graceful closure,
         * but note that send is still possible in this state so we don't close the
         * whole socket here (by calling tcp_close()) as that would violate TCP specs
         */
        Connection->ReceiveShutdown = TRUE;
        Connection->ReceiveShutdownStatus = STATUS_SUCCESS;

        /* If we already did a send shutdown, we're in TIME_WAIT so we can't use this PCB anymore */
        if (Connection->SendShutdown)
        {
            Connection->SocketContext = NULL;
            tcp_arg(pcb, NULL);
        }

        /* Indicate the graceful close event */
        TCPRecvEventHandler(arg);

        /* If the PCB is gone, clean up the connection */
        if (Connection->SendShutdown)
        {
            TCPFinEventHandler(Connection, ERR_CLSD);
        }
    }

    return ERR_OK;
}

/* This function MUST return an error value that is not ERR_ABRT or ERR_OK if the connection
 * is not accepted to avoid leaking the new PCB */
static
err_t
InternalAcceptEventHandler(void *arg, PTCP_PCB newpcb, const err_t err)
{
    /* Make sure the socket didn't get closed */
    if (!arg)
        return ERR_CLSD;

    TCPAcceptEventHandler(arg, newpcb);

    /* Set in LibTCPAccept (called from TCPAcceptEventHandler) */
    if (newpcb->callback_arg)
        return ERR_OK;
    else
        return ERR_CLSD;
}

static
err_t
InternalConnectEventHandler(void *arg, PTCP_PCB pcb, const err_t err)
{
    /* Make sure the socket didn't get closed */
    if (!arg)
        return ERR_OK;

    TCPConnectEventHandler(arg, err);

    return ERR_OK;
}

static
void
InternalErrorEventHandler(void *arg, const err_t err)
{
    PCONNECTION_ENDPOINT Connection = arg;

    /* Make sure the socket didn't get closed */
    if (!arg || Connection->SocketContext == NULL) return;

    /* The PCB is dead now */
    Connection->SocketContext = NULL;

    /* Give them one shot to receive the remaining data */
    Connection->ReceiveShutdown = TRUE;
    Connection->ReceiveShutdownStatus = TCPTranslateError(err);
    TCPRecvEventHandler(Connection);

    /* Terminate the connection */
    TCPFinEventHandler(Connection, err);
}

static
void
LibTCPSocketCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;

    ASSERT(msg);

    msg->Output.Socket.NewPcb = tcp_new();

    if (msg->Output.Socket.NewPcb)
    {
        tcp_arg(msg->Output.Socket.NewPcb, msg->Input.Socket.Arg);
        tcp_err(msg->Output.Socket.NewPcb, InternalErrorEventHandler);
    }

    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

struct tcp_pcb *
LibTCPSocket(void *arg)
{
    struct lwip_callback_msg *msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    struct tcp_pcb *ret;

    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);
        msg->Input.Socket.Arg = arg;

        LibTCPCall(LibTCPSocketCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Socket.NewPcb;
        else
            ret = NULL;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return NULL;
}

static
void
LibTCPFreeSocketCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;

    ASSERT(msg);

    /* Calling tcp_close will free it */
    tcp_close(msg->Input.FreeSocket.pcb);

    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

void LibTCPFreeSocket(PTCP_PCB pcb)
{
    struct lwip_callback_msg msg;

    KeInitializeEvent(&msg.Event, NotificationEvent, FALSE);
    msg.Input.FreeSocket.pcb = pcb;

    LibTCPCall(LibTCPFreeSocketCallback, &msg);

    WaitForEventSafely(&msg.Event);
}


static
void
LibTCPBindCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Bind.Connection->SocketContext;

    ASSERT(msg);

    if (!msg->Input.Bind.Connection->SocketContext)
    {
        msg->Output.Bind.Error = ERR_CLSD;
        goto done;
    }

    /* We're guaranteed that the local address is valid to bind at this point */
    pcb->so_options |= SOF_REUSEADDR;

    msg->Output.Bind.Error = tcp_bind(pcb,
                                      msg->Input.Bind.IpAddress,
                                      lwip_ntohs(msg->Input.Bind.Port));

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPBind(PCONNECTION_ENDPOINT Connection, ip4_addr_t *const ipaddr, const u16_t port)
{
    struct lwip_callback_msg *msg;
    err_t ret;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);
        msg->Input.Bind.Connection = Connection;
        msg->Input.Bind.IpAddress = ipaddr;
        msg->Input.Bind.Port = port;

        LibTCPCall(LibTCPBindCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Bind.Error;
        else
            ret = ERR_CLSD;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return ERR_MEM;
}

static
void
LibTCPListenCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;

    ASSERT(msg);

    if (!msg->Input.Listen.Connection->SocketContext)
    {
        msg->Output.Listen.NewPcb = NULL;
        goto done;
    }

    msg->Output.Listen.NewPcb = tcp_listen_with_backlog((PTCP_PCB)msg->Input.Listen.Connection->SocketContext, msg->Input.Listen.Backlog);

    if (msg->Output.Listen.NewPcb)
    {
        tcp_accept(msg->Output.Listen.NewPcb, InternalAcceptEventHandler);
    }

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

PTCP_PCB
LibTCPListen(PCONNECTION_ENDPOINT Connection, const u8_t backlog)
{
    struct lwip_callback_msg *msg;
    PTCP_PCB ret;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);
        msg->Input.Listen.Connection = Connection;
        msg->Input.Listen.Backlog = backlog;

        LibTCPCall(LibTCPListenCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Listen.NewPcb;
        else
            ret = NULL;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return NULL;
}

static
void
LibTCPSendCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Send.Connection->SocketContext;
    ULONG SendLength;
    UCHAR SendFlags;

    ASSERT(msg);

    if (!msg->Input.Send.Connection->SocketContext)
    {
        msg->Output.Send.Error = ERR_CLSD;
        goto done;
    }

    if (msg->Input.Send.Connection->SendShutdown)
    {
        msg->Output.Send.Error = ERR_CLSD;
        goto done;
    }

    SendFlags = TCP_WRITE_FLAG_COPY;
    SendLength = msg->Input.Send.DataLength;
    if (tcp_sndbuf(pcb) == 0)
    {
        /* No buffer space so return pending */
        msg->Output.Send.Error = ERR_INPROGRESS;
        goto done;
    }
    else if (tcp_sndbuf(pcb) < SendLength)
    {
        /* We've got some room so let's send what we can */
        SendLength = tcp_sndbuf(pcb);

        /* Don't set the push flag */
        SendFlags |= TCP_WRITE_FLAG_MORE;
    }

    /* (tcp_write takes at most 64K - 1 at a time) */
    if (SendLength > 0xFFFF)
    {
        SendLength = 0xFFFF;
        SendFlags |= TCP_WRITE_FLAG_MORE;
    }

    msg->Output.Send.Error = tcp_write(pcb,
                                       msg->Input.Send.Data,
                                       SendLength,
                                       SendFlags);
    if (msg->Output.Send.Error == ERR_OK)
    {
        /* Queued successfully so try to send it */
        tcp_output((PTCP_PCB)msg->Input.Send.Connection->SocketContext);
        msg->Output.Send.Information = SendLength;
    }
    else if (msg->Output.Send.Error == ERR_MEM)
    {
        /* The queue is too long */
        msg->Output.Send.Error = ERR_INPROGRESS;
    }

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPSend(PCONNECTION_ENDPOINT Connection, void *const dataptr, const ULONG len, ULONG *sent, const int safe)
{
    err_t ret;
    struct lwip_callback_msg *msg;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);
        msg->Input.Send.Connection = Connection;
        msg->Input.Send.Data = dataptr;
        msg->Input.Send.DataLength = len;

        LibTCPCall(LibTCPSendCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Send.Error;
        else
            ret = ERR_CLSD;

        if (ret == ERR_OK)
            *sent = msg->Output.Send.Information;
        else
            *sent = 0;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return ERR_MEM;
}

static
void
LibTCPConnectCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    err_t Error;

    ASSERT(arg);

    if (!msg->Input.Connect.Connection->SocketContext)
    {
        msg->Output.Connect.Error = ERR_CLSD;
        goto done;
    }

    tcp_recv((PTCP_PCB)msg->Input.Connect.Connection->SocketContext, InternalRecvEventHandler);
    tcp_sent((PTCP_PCB)msg->Input.Connect.Connection->SocketContext, InternalSendEventHandler);

    Error = tcp_connect((PTCP_PCB)msg->Input.Connect.Connection->SocketContext,
                        msg->Input.Connect.IpAddress, lwip_ntohs(msg->Input.Connect.Port),
                        InternalConnectEventHandler);

    msg->Output.Connect.Error = Error == ERR_OK ? ERR_INPROGRESS : Error;

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPConnect(PCONNECTION_ENDPOINT Connection, ip_addr_t *const ipaddr, const u16_t port)
{
    struct lwip_callback_msg *msg;
    err_t ret;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);
        msg->Input.Connect.Connection = Connection;
        msg->Input.Connect.IpAddress = ipaddr;
        msg->Input.Connect.Port = port;

        LibTCPCall(LibTCPConnectCallback, msg);

        if (WaitForEventSafely(&msg->Event))
        {
            ret = msg->Output.Connect.Error;
        }
        else
            ret = ERR_CLSD;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return ERR_MEM;
}

static
void
LibTCPShutdownCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Shutdown.Connection->SocketContext;

    if (!msg->Input.Shutdown.Connection->SocketContext)
    {
        msg->Output.Shutdown.Error = ERR_CLSD;
        goto done;
    }

    /* LwIP makes the (questionable) assumption that SHUTDOWN_RDWR is equivalent to tcp_close().
     * This assumption holds even if the shutdown calls are done separately (even through multiple
     * WinSock shutdown() calls). This assumption means that lwIP has the right to deallocate our
     * PCB without telling us if we shutdown TX and RX. To avoid these problems, we'll clear the
     * socket context if we have called shutdown for TX and RX.
     */
    if (msg->Input.Shutdown.shut_rx != msg->Input.Shutdown.shut_tx) {
        if (msg->Input.Shutdown.shut_rx) {
            msg->Output.Shutdown.Error = tcp_shutdown(pcb, TRUE, FALSE);
        }
        if (msg->Input.Shutdown.shut_tx) {
            msg->Output.Shutdown.Error = tcp_shutdown(pcb, FALSE, TRUE);
        }
    }
    else if (msg->Input.Shutdown.shut_rx) {
        /* We received both RX and TX requests, which seems to mean closing connection from TDI.
         * So call tcp_close, otherwise we risk to be put in TCP_WAIT_* states, which makes further
         * attempts to close the socket to fail in this state.
         */
        msg->Output.Shutdown.Error = tcp_close(pcb);
    }
    else {
        /* This case shouldn't happen */
        DbgPrint("Requested socket shutdown(0, 0) !\n");
    }

    if (!msg->Output.Shutdown.Error)
    {
        if (msg->Input.Shutdown.shut_rx)
        {
            msg->Input.Shutdown.Connection->ReceiveShutdown = TRUE;
            msg->Input.Shutdown.Connection->ReceiveShutdownStatus = STATUS_FILE_CLOSED;
        }

        if (msg->Input.Shutdown.shut_tx)
            msg->Input.Shutdown.Connection->SendShutdown = TRUE;

        if (msg->Input.Shutdown.Connection->ReceiveShutdown &&
            msg->Input.Shutdown.Connection->SendShutdown)
        {
            /* The PCB is not ours anymore */
            msg->Input.Shutdown.Connection->SocketContext = NULL;
            tcp_arg(pcb, NULL);
            TCPFinEventHandler(msg->Input.Shutdown.Connection, ERR_CLSD);
        }
    }

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPShutdown(PCONNECTION_ENDPOINT Connection, const int shut_rx, const int shut_tx)
{
    struct lwip_callback_msg *msg;
    err_t ret;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);

        msg->Input.Shutdown.Connection = Connection;
        msg->Input.Shutdown.shut_rx = shut_rx;
        msg->Input.Shutdown.shut_tx = shut_tx;

        LibTCPCall(LibTCPShutdownCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Shutdown.Error;
        else
            ret = ERR_CLSD;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return ERR_MEM;
}

static
void
LibTCPCloseCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Close.Connection->SocketContext;

    /* Empty the queue even if we're already "closed" */
    LibTCPEmptyQueue(msg->Input.Close.Connection);

    /* Check if we've already been closed */
    if (msg->Input.Close.Connection->Closing)
    {
        msg->Output.Close.Error = ERR_OK;
        goto done;
    }

    /* Enter "closing" mode if we're doing a normal close */
    if (msg->Input.Close.Callback)
        msg->Input.Close.Connection->Closing = TRUE;

    /* Check if the PCB was already "closed" but the client doesn't know it yet */
    if (!msg->Input.Close.Connection->SocketContext)
    {
        msg->Output.Close.Error = ERR_OK;
        goto done;
    }

    /* Clear the PCB pointer and stop callbacks */
    msg->Input.Close.Connection->SocketContext = NULL;
    tcp_arg(pcb, NULL);

    /* This may generate additional callbacks but we don't care,
     * because they're too inconsistent to rely on */
    msg->Output.Close.Error = tcp_close(pcb);

    if (msg->Output.Close.Error)
    {
        /* Restore the PCB pointer */
        msg->Input.Close.Connection->SocketContext = pcb;
        msg->Input.Close.Connection->Closing = FALSE;
    }
    else if (msg->Input.Close.Callback)
    {
        TCPFinEventHandler(msg->Input.Close.Connection, ERR_CLSD);
    }

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPClose(PCONNECTION_ENDPOINT Connection, const int safe, const int callback)
{
    err_t ret;
    struct lwip_callback_msg *msg;

    msg = ExAllocateFromNPagedLookasideList(&MessageLookasideList);
    if (msg)
    {
        KeInitializeEvent(&msg->Event, NotificationEvent, FALSE);

        msg->Input.Close.Connection = Connection;
        msg->Input.Close.Callback = callback;

        LibTCPCall(LibTCPCloseCallback, msg);

        if (WaitForEventSafely(&msg->Event))
            ret = msg->Output.Close.Error;
        else
            ret = ERR_CLSD;

        ExFreeToNPagedLookasideList(&MessageLookasideList, msg);

        return ret;
    }

    return ERR_MEM;
}

void
LibTCPAccept(PTCP_PCB pcb, struct tcp_pcb *listen_pcb, void *arg)
{
    ASSERT(arg);

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, InternalRecvEventHandler);
    tcp_sent(pcb, InternalSendEventHandler);
    tcp_err(pcb, InternalErrorEventHandler);
    tcp_arg(pcb, arg);

    tcp_accepted(listen_pcb);
}

err_t
LibTCPGetHostName(PTCP_PCB pcb, ip_addr_t *const ipaddr, u16_t *const port)
{
    if (!pcb)
        return ERR_CLSD;

    *ipaddr = pcb->local_ip;
    /* TDI addresses hold the port in network byte order, lwIP in host order */
    *port = lwip_htons(pcb->local_port);

    return ERR_OK;
}

err_t
LibTCPGetPeerName(PTCP_PCB pcb, ip_addr_t * const ipaddr, u16_t * const port)
{
    if (!pcb)
        return ERR_CLSD;

    *ipaddr = pcb->remote_ip;
    /* TDI addresses hold the port in network byte order, lwIP in host order */
    *port = lwip_htons(pcb->remote_port);

    return ERR_OK;
}

void
LibTCPSetNoDelay(
    PTCP_PCB pcb,
    BOOLEAN Set)
{
    if (Set)
        pcb->flags |= TF_NODELAY;
    else
        pcb->flags &= ~TF_NODELAY;
}

void
LibTCPSetKeepAlive(
    PTCP_PCB pcb,
    BOOLEAN Set)
{
    if (Set)
        pcb->so_options |= SOF_KEEPALIVE;
    else
        pcb->so_options &= ~SOF_KEEPALIVE;
}

void
LibTcpSetKeepAliveValues(
    PTCP_PCB pcb,
    u32_t KeepAliveTime,
    u32_t KeepAliveInterval
)
{
    pcb->keep_idle = KeepAliveTime;
    pcb->keep_intvl = KeepAliveInterval;
    pcb->keep_cnt = 10;
}

void
LibTCPGetSocketStatus(
    PTCP_PCB pcb,
    PULONG State)
{
    /* Translate state from enum tcp_state -> MIB_TCP_STATE */
    *State = pcb->state + 1;
}
