/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/net/afd/afd/write.c
 * PURPOSE:          Ancillary functions driver
 * PROGRAMMER:       Art Yerkes (ayerkes@speakeasy.net)
 * UPDATE HISTORY:
 * 20040708 Created
 */

#include "afd.h"

/* A stream send completes as soon as all of its data is copied to the
 * socket's send window, as on Windows (it used to wait until tcpip had taken
 * the window: a "non-blocking" send blocked for as long as the peer's receive
 * window stayed closed). A send IRP waiting in
 * FCB->PendingIrpList[FUNCTION_SEND] keeps
 *   IoStatus.Information: how many of its bytes were copied to the window
 *   DriverContext[2]: how many are still to be copied (blocking sends).
 * A blocking send bigger than the send window is copied piece by piece as the
 * window drains and completes once all of it is copied (it used to complete
 * with a short count: a 1 MB send() returned 8192) */
#define SEND_TO_COPY(Irp) ((Irp)->Tail.Overlay.DriverContext[2])

static BOOLEAN IsBlockingSend(PAFD_FCB FCB, PAFD_SEND_INFO SendReq)
{
    return !((SendReq->AfdFlags & AFD_IMMEDIATE) || FCB->NonBlocking);
}

static UINT SendRequestLength(PAFD_SEND_INFO SendReq)
{
    UINT i, Length = 0;

    for (i = 0; i < SendReq->BufferCount; i++)
        Length += SendReq->BufferArray[i].len;

    return Length;
}

/* Append up to SpaceAvail bytes of a send request, from Offset bytes into it,
 * to the send window. UserVa: in the caller's context, read its buffers
 * directly; otherwise through their locked MDLs */
static UINT CopySendData(PAFD_FCB FCB, PAFD_SEND_INFO SendReq, UINT Offset,
                         UINT SpaceAvail, BOOLEAN UserVa)
{
    PAFD_MAPBUF Map = (PAFD_MAPBUF)(SendReq->BufferArray + SendReq->BufferCount);
    UINT i, Length, Copied = 0;
    PCHAR Source;

    for (i = 0; i < SendReq->BufferCount && SpaceAvail > 0; i++)
    {
        Length = SendReq->BufferArray[i].len;
        if (Offset >= Length)
        {
            Offset -= Length;
            continue;
        }

        if (UserVa)
        {
            Source = SendReq->BufferArray[i].buf;
        }
        else
        {
            if (!Map[i].Mdl)
                break;
            /* (the mapping stays until UnlockBuffers unlocks the pages) */
            Source = MmGetSystemAddressForMdlSafe(Map[i].Mdl, NormalPagePriority);
            if (!Source)
                break;
        }

        Length = MIN(Length - Offset, SpaceAvail);
        RtlCopyMemory(FCB->Send.Window + FCB->Send.BytesUsed, Source + Offset, Length);
        Offset = 0;

        FCB->Send.BytesUsed += Length;
        SpaceAvail -= Length;
        Copied += Length;
    }

    return Copied;
}

/* A queued send still has bytes that are not in the window: a new send must
 * wait behind it, or the stream would be reordered. (Sends that are all in
 * the window are completed, so any queued one waits) */
static BOOLEAN SendQueueWaiting(PAFD_FCB FCB)
{
    return !IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]);
}

static VOID CompleteSendIrp(PIRP Irp, PAFD_SEND_INFO SendReq, NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    (void)IoSetCancelRoutine(Irp, NULL);
    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
    if (Irp->MdlAddress) UnlockRequest(Irp, IoGetCurrentIrpStackLocation(Irp));
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

/* Copy queued sends into the free part of the send window, in order, and
 * complete those that are all in it */
static VOID FillSendWindow(PAFD_FCB FCB)
{
    PIRP NextIrp;
    PAFD_SEND_INFO SendReq;
    UINT SpaceAvail, Length, Remaining, Copied;

    while (!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]))
    {
        NextIrp = CONTAINING_RECORD(FCB->PendingIrpList[FUNCTION_SEND].Flink, IRP, Tail.Overlay.ListEntry);

        SpaceAvail = FCB->Send.Size - FCB->Send.BytesUsed;
        if (SpaceAvail == 0)
            break;

        SendReq = GetLockedData(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));
        Length = SendRequestLength(SendReq);
        Remaining = Length - (UINT)NextIrp->IoStatus.Information;

        /* A blocking send that fits in the window is copied all at once */
        if (NextIrp->IoStatus.Information == 0 && IsBlockingSend(FCB, SendReq) &&
            Length <= FCB->Send.Size && Length > SpaceAvail)
        {
            break;
        }

        Copied = CopySendData(FCB, SendReq, (UINT)NextIrp->IoStatus.Information, SpaceAvail, FALSE);
        NextIrp->IoStatus.Information += Copied;

        /* Blocking: the rest follows later. Non-blocking: it completes with
         * what was copied */
        if (Copied < Remaining && IsBlockingSend(FCB, SendReq))
        {
            SEND_TO_COPY(NextIrp) = (PVOID)(ULONG_PTR)(Remaining - Copied);
            break;
        }

        RemoveEntryList(&NextIrp->Tail.Overlay.ListEntry);
        CompleteSendIrp(NextIrp, SendReq, STATUS_SUCCESS);
    }
}

static IO_COMPLETION_ROUTINE SendComplete;

/* tcpip completes a send it could queue at once inside TdiSend, so sending
 * the next part of the window from SendComplete nests one more TdiSend ->
 * SendComplete level each time: a big blocking send would run out of kernel
 * stack. When the stack runs low, the next TdiSend is issued from a worker */
#define AFD_SEND_STACK_RESERVE (8 * 1024)
#define TAG_AFD_SEND_WORK 'wsfA'

typedef struct _AFD_SEND_WORK
{
    WORK_QUEUE_ITEM WorkItem;
    PFILE_OBJECT FileObject;
} AFD_SEND_WORK, *PAFD_SEND_WORK;

static VOID NTAPI DeferredSendWorker(PVOID Context)
{
    PAFD_SEND_WORK Work = Context;
    PAFD_FCB FCB = Work->FileObject->FsContext;

    if (SocketAcquireStateLock(FCB))
    {
        if (FCB->SharedData.State != SOCKET_STATE_CLOSED &&
            !FCB->SendIrp.InFlightRequest && FCB->Send.BytesUsed)
        {
            TdiSend(&FCB->SendIrp.InFlightRequest,
                    FCB->Connection.Object,
                    0,
                    FCB->Send.Window,
                    FCB->Send.BytesUsed,
                    SendComplete,
                    FCB);
        }
        SocketStateUnlock(FCB);
    }

    ObDereferenceObject(Work->FileObject);
    ExFreePoolWithTag(Work, TAG_AFD_SEND_WORK);
}

static BOOLEAN DeferSend(PAFD_FCB FCB)
{
    PAFD_SEND_WORK Work;

    if (IoGetRemainingStackSize() >= AFD_SEND_STACK_RESERVE)
        return FALSE;

    Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_AFD_SEND_WORK);
    if (!Work)
        return FALSE;

    ObReferenceObject(FCB->FileObject);
    Work->FileObject = FCB->FileObject;
    ExInitializeWorkItem(&Work->WorkItem, DeferredSendWorker, Work);
    ExQueueWorkItem(&Work->WorkItem, CriticalWorkQueue);
    return TRUE;
}

static NTSTATUS NTAPI SendComplete
( PDEVICE_OBJECT DeviceObject,
  PIRP Irp,
  PVOID Context ) {
    NTSTATUS Status = Irp->IoStatus.Status;
    PAFD_FCB FCB = (PAFD_FCB)Context;
    PLIST_ENTRY NextIrpEntry;
    PIRP NextIrp = NULL;
    PIO_STACK_LOCATION NextIrpSp;
    PAFD_SEND_INFO SendReq = NULL;

    UNREFERENCED_PARAMETER(DeviceObject);

    /*
     * The Irp parameter passed in is the IRP of the stream between AFD and
     * TDI driver. It's not very useful to us. We need the IRPs of the stream
     * between usermode and AFD. Those are chained from
     * FCB->PendingIrpList[FUNCTION_SEND] and you'll see them in the code
     * below as "NextIrp" ('cause they are the next usermode IRP to be
     * processed).
     */

    AFD_DbgPrint(MID_TRACE,("Called, status %x, %u bytes used\n",
                            Irp->IoStatus.Status,
                            Irp->IoStatus.Information));

    if( !SocketAcquireStateLock( FCB ) )
        return STATUS_FILE_CLOSED;

    ASSERT(FCB->SendIrp.InFlightRequest == Irp);
    FCB->SendIrp.InFlightRequest = NULL;
    /* Request is not in flight any longer */

    if( FCB->SharedData.State == SOCKET_STATE_CLOSED ) {
        /* Cleanup our IRP queue because the FCB is being destroyed */
        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_SEND] ) ) {
            NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
            NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
            NextIrpSp = IoGetCurrentIrpStackLocation( NextIrp );
            SendReq = GetLockedData(NextIrp, NextIrpSp);
            NextIrp->IoStatus.Status = STATUS_FILE_CLOSED;
            NextIrp->IoStatus.Information = 0;
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            if( NextIrp->MdlAddress ) UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
            (void)IoSetCancelRoutine(NextIrp, NULL);
            IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }

        FCB->Send.BytesUsed = 0;
        RetryDisconnectCompletion(FCB);

        SocketStateUnlock( FCB );
        return STATUS_FILE_CLOSED;
    }

    if( !NT_SUCCESS(Status) ) {
        /* Complete all following send IRPs with error */

        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_SEND] ) ) {
            NextIrpEntry =
                RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
            NextIrp =
                CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
            NextIrpSp = IoGetCurrentIrpStackLocation( NextIrp );
            SendReq = GetLockedData(NextIrp, NextIrpSp);

            UnlockBuffers( SendReq->BufferArray,
                           SendReq->BufferCount,
                           FALSE );

            NextIrp->IoStatus.Status = Status;
            NextIrp->IoStatus.Information = 0;

            if ( NextIrp->MdlAddress ) UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
            (void)IoSetCancelRoutine(NextIrp, NULL);
            IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }

        /* (what is in the window is lost) */
        FCB->Send.BytesUsed = 0;
        RetryDisconnectCompletion(FCB);

        SocketStateUnlock( FCB );

        return STATUS_SUCCESS;
    }

    /* The sends that this data came from are completed already */
    ASSERT(Irp->IoStatus.Information <= FCB->Send.BytesUsed);
    RtlMoveMemory( FCB->Send.Window,
                   FCB->Send.Window + Irp->IoStatus.Information,
                   FCB->Send.BytesUsed - Irp->IoStatus.Information );
    FCB->Send.BytesUsed -= Irp->IoStatus.Information;

    /* Refill the window from the queued sends */
    FillSendWindow(FCB);

    if (FCB->Send.Size - FCB->Send.BytesUsed != 0 && !FCB->SendClosed &&
        IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]))
    {
        FCB->PollState |= AFD_EVENT_SEND;
        FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
        PollReeval( FCB->DeviceExt, FCB->FileObject );
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
    }


    /* Some data is still waiting */
    if( FCB->Send.BytesUsed )
    {
        if (!DeferSend(FCB))
        {
            Status = TdiSend( &FCB->SendIrp.InFlightRequest,
                              FCB->Connection.Object,
                              0,
                              FCB->Send.Window,
                              FCB->Send.BytesUsed,
                              SendComplete,
                              FCB );
        }
    }
    else
    {
        /* Nothing is waiting so try to complete a pending disconnect */
        RetryDisconnectCompletion(FCB);
    }

    SocketStateUnlock( FCB );

    return STATUS_SUCCESS;
}

static IO_COMPLETION_ROUTINE PacketSocketSendComplete;
static NTSTATUS NTAPI PacketSocketSendComplete
( PDEVICE_OBJECT DeviceObject,
  PIRP Irp,
  PVOID Context ) {
    PAFD_FCB FCB = (PAFD_FCB)Context;
    PLIST_ENTRY NextIrpEntry;
    PIRP NextIrp;
    PAFD_SEND_INFO SendReq;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called, status %x, %u bytes used\n",
                            Irp->IoStatus.Status,
                            Irp->IoStatus.Information));

    if( !SocketAcquireStateLock( FCB ) )
        return STATUS_FILE_CLOSED;

    ASSERT(FCB->SendIrp.InFlightRequest == Irp);
    FCB->SendIrp.InFlightRequest = NULL;
    /* Request is not in flight any longer */

    if( FCB->SharedData.State == SOCKET_STATE_CLOSED ) {
        /* Cleanup our IRP queue because the FCB is being destroyed */
        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_SEND] ) ) {
            NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
            NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
            SendReq = GetLockedData(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));
            NextIrp->IoStatus.Status = STATUS_FILE_CLOSED;
            NextIrp->IoStatus.Information = 0;
            (void)IoSetCancelRoutine(NextIrp, NULL);
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
            IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }
        SocketStateUnlock( FCB );
        return STATUS_FILE_CLOSED;
    }

    ASSERT(!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]));

    /* TDI spec guarantees FIFO ordering on IRPs */
    NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
    NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);

    SendReq = GetLockedData(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));

    NextIrp->IoStatus.Status = Irp->IoStatus.Status;
    NextIrp->IoStatus.Information = Irp->IoStatus.Information;

    (void)IoSetCancelRoutine(NextIrp, NULL);

    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);

    UnlockRequest(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));

    IoCompleteRequest(NextIrp, IO_NETWORK_INCREMENT);

    FCB->PollState |= AFD_EVENT_SEND;
    FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
    PollReeval(FCB->DeviceExt, FCB->FileObject);

    SocketStateUnlock(FCB);

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
AfdConnectedSocketWriteData(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                            PIO_STACK_LOCATION IrpSp, BOOLEAN Short) {
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_SEND_INFO SendReq;
    UINT TotalBytesCopied = 0, SpaceAvail = 0, SendLength;
    BOOLEAN Blocking;
    KPROCESSOR_MODE LockMode;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Short);

    AFD_DbgPrint(MID_TRACE,("Called on %p\n", FCB));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    FCB->EventSelectDisabled &= ~AFD_EVENT_SEND;

    if( FCB->Flags & AFD_ENDPOINT_CONNECTIONLESS )
    {
        PAFD_SEND_INFO_UDP SendReq;
        PTDI_CONNECTION_INFORMATION TargetAddress;

        /* Check that the socket is bound */
        if( FCB->SharedData.State != SOCKET_STATE_BOUND || !FCB->RemoteAddress )
        {
            AFD_DbgPrint(MIN_TRACE,("Invalid parameter\n"));
            return UnlockAndMaybeComplete( FCB, STATUS_INVALID_PARAMETER, Irp,
                                           0 );
        }

        if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
            return UnlockAndMaybeComplete( FCB, STATUS_NO_MEMORY, Irp, 0 );

        /* Must lock buffers before handing off user data */
        SendReq->BufferArray = LockBuffers( SendReq->BufferArray,
                                            SendReq->BufferCount,
                                            NULL, NULL,
                                            FALSE, FALSE, LockMode );

        if( !SendReq->BufferArray ) {
            return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                           Irp, 0 );
        }

        Status = TdiBuildConnectionInfo( &TargetAddress, FCB->RemoteAddress );

        if( NT_SUCCESS(Status) ) {
            FCB->PollState &= ~AFD_EVENT_SEND;

            Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
            if (Status == STATUS_PENDING)
            {
                if (SendReq->BufferCount > 1)
                {
                    AFD_DbgPrint(MIN_TRACE,("WARN: More than one buffer %ld\n", SendReq->BufferCount));
                }
                Status = TdiSendDatagram(&FCB->SendIrp.InFlightRequest,
                                         FCB->AddressFile.Object,
                                         SendReq->BufferArray[0].buf,
                                         SendReq->BufferArray[0].len,
                                         TargetAddress,
                                         PacketSocketSendComplete,
                                         FCB);
                if (Status != STATUS_PENDING)
                {
                    NT_VERIFY(RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]) == &Irp->Tail.Overlay.ListEntry);
                    Irp->IoStatus.Status = Status;
                    Irp->IoStatus.Information = ((Status == STATUS_SUCCESS) ? SendReq->BufferArray[0].len : 0);
                    (void)IoSetCancelRoutine(Irp, NULL);
                    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
                    UnlockRequest(Irp, IoGetCurrentIrpStackLocation(Irp));
                    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
                }
            }

            ExFreePoolWithTag(TargetAddress, TAG_AFD_TDI_CONNECTION_INFORMATION);

            SocketStateUnlock(FCB);

            return STATUS_PENDING;
        }
        else
        {
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
        }
    }

    if (FCB->PollState & AFD_EVENT_CLOSE)
    {
        AFD_DbgPrint(MIN_TRACE,("Connection reset by remote peer\n"));

        /* This is an unexpected remote disconnect */
        return UnlockAndMaybeComplete(FCB, FCB->PollStatus[FD_CLOSE_BIT], Irp, 0);
    }

    if (FCB->PollState & AFD_EVENT_ABORT)
    {
        AFD_DbgPrint(MIN_TRACE,("Connection aborted\n"));

        /* This is an abortive socket closure on our side */
        return UnlockAndMaybeComplete(FCB, FCB->PollStatus[FD_CLOSE_BIT], Irp, 0);
    }

    if (FCB->SendClosed)
    {
        AFD_DbgPrint(MIN_TRACE,("No more sends\n"));

        /* This is a graceful send closure */
        return UnlockAndMaybeComplete(FCB, STATUS_FILE_CLOSED, Irp, 0);
    }

    if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
        return UnlockAndMaybeComplete
            ( FCB, STATUS_NO_MEMORY, Irp, 0 );

    SendReq->BufferArray = LockBuffers( SendReq->BufferArray,
                                        SendReq->BufferCount,
                                        NULL, NULL,
                                        FALSE, FALSE, LockMode );

    if( !SendReq->BufferArray ) {
        return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                       Irp, 0 );
    }

    AFD_DbgPrint(MID_TRACE,("Socket state %u\n", FCB->SharedData.State));

    if( FCB->SharedData.State != SOCKET_STATE_CONNECTED ) {
        AFD_DbgPrint(MID_TRACE,("Socket not connected\n"));
        UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
        return UnlockAndMaybeComplete( FCB, STATUS_INVALID_CONNECTION, Irp, 0 );
    }

    AFD_DbgPrint(MID_TRACE,("FCB->Send.BytesUsed = %u\n",
                            FCB->Send.BytesUsed));

    SpaceAvail = FCB->Send.Size - FCB->Send.BytesUsed;

    AFD_DbgPrint(MID_TRACE,("We can accept %u bytes\n",
                            SpaceAvail));

    /* Count the total transfer size */
    SendLength = SendRequestLength(SendReq);
    Blocking = IsBlockingSend(FCB, SendReq);

    Irp->IoStatus.Information = 0;
    SEND_TO_COPY(Irp) = NULL;

    if (SendLength == 0)
    {
        AFD_DbgPrint(MID_TRACE,("Empty send\n"));
        UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
        return UnlockAndMaybeComplete( FCB, STATUS_SUCCESS, Irp, 0 );
    }

    /* Make sure we've got the space (and no earlier send is still waiting for it) */
    if (SendLength > SpaceAvail || SendQueueWaiting(FCB))
    {
        /* Blocking sockets have to wait here (a send bigger than the window
         * is copied in pieces, from SendComplete if nothing fits now) */
        if (Blocking && (SendLength <= FCB->Send.Size || SpaceAvail == 0 || SendQueueWaiting(FCB)))
        {
            FCB->PollState &= ~AFD_EVENT_SEND;
            return LeaveIrpUntilLater(FCB, Irp, FUNCTION_SEND);
        }

        /* Check if we can send anything */
        if (SpaceAvail == 0 || SendQueueWaiting(FCB))
        {
            FCB->PollState &= ~AFD_EVENT_SEND;

            /* Non-overlapped sockets will fail if we can send nothing */
            if (!(SendReq->AfdFlags & AFD_OVERLAPPED))
            {
                UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
                return UnlockAndMaybeComplete( FCB, STATUS_CANT_WAIT, Irp, 0 );
            }
            else
            {
                /* Overlapped sockets just pend */
                return LeaveIrpUntilLater(FCB, Irp, FUNCTION_SEND);
            }
        }
    }

    TotalBytesCopied = CopySendData(FCB, SendReq, 0, SpaceAvail, TRUE);
    SpaceAvail -= TotalBytesCopied;

    Irp->IoStatus.Information = TotalBytesCopied;

    /* A blocking send bigger than the window: the rest follows as it drains */
    if (Blocking && TotalBytesCopied < SendLength)
        SEND_TO_COPY(Irp) = (PVOID)(ULONG_PTR)(SendLength - TotalBytesCopied);

    if (SpaceAvail)
    {
        FCB->PollState |= AFD_EVENT_SEND;
        FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
        PollReeval( FCB->DeviceExt, FCB->FileObject );
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
    }

    /* A blocking send bigger than the window waits for room for the rest
     * (queued before sending: tcpip may complete the send right away, and
     * SendComplete refills the window from the queue) */
    if (SEND_TO_COPY(Irp) != NULL)
    {
        Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
        if (!FCB->SendIrp.InFlightRequest)
        {
            TdiSend(&FCB->SendIrp.InFlightRequest,
                    FCB->Connection.Object,
                    0,
                    FCB->Send.Window,
                    FCB->Send.BytesUsed,
                    SendComplete,
                    FCB);
        }

        SocketStateUnlock(FCB);
        return Status;
    }

    /* All of it is in the window (or, non-blocking, what fitted): send the
     * window if nothing is on its way already, and complete */
    if (!FCB->SendIrp.InFlightRequest)
    {
        TdiSend(&FCB->SendIrp.InFlightRequest,
                FCB->Connection.Object,
                0,
                FCB->Send.Window,
                FCB->Send.BytesUsed,
                SendComplete,
                FCB);
    }

    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
    return UnlockAndMaybeComplete(FCB, STATUS_SUCCESS, Irp, (UINT)Irp->IoStatus.Information);
}

NTSTATUS NTAPI
AfdPacketSocketWriteData(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                         PIO_STACK_LOCATION IrpSp) {
    NTSTATUS Status = STATUS_SUCCESS;
    PTDI_CONNECTION_INFORMATION TargetAddress;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_SEND_INFO_UDP SendReq;
    KPROCESSOR_MODE LockMode;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called on %p\n", FCB));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    FCB->EventSelectDisabled &= ~AFD_EVENT_SEND;

    /* Check that the socket is bound */
    if( FCB->SharedData.State != SOCKET_STATE_BOUND &&
        FCB->SharedData.State != SOCKET_STATE_CREATED)
    {
        AFD_DbgPrint(MIN_TRACE,("Invalid socket state\n"));
        return UnlockAndMaybeComplete(FCB, STATUS_INVALID_PARAMETER, Irp, 0);
    }

    if (FCB->SendClosed)
    {
        AFD_DbgPrint(MIN_TRACE,("No more sends\n"));
        return UnlockAndMaybeComplete(FCB, STATUS_FILE_CLOSED, Irp, 0);
    }

    if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
        return UnlockAndMaybeComplete(FCB, STATUS_NO_MEMORY, Irp, 0);

    if (FCB->SharedData.State == SOCKET_STATE_CREATED)
    {
        if (FCB->LocalAddress)
        {
            ExFreePoolWithTag(FCB->LocalAddress, TAG_AFD_TRANSPORT_ADDRESS);
        }

        FCB->LocalAddress =
        TaBuildNullTransportAddress( ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                                      Address[0].AddressType );

        if( FCB->LocalAddress ) {
            Status = WarmSocketForBind( FCB, AFD_SHARE_WILDCARD );

            if( NT_SUCCESS(Status) )
                FCB->SharedData.State = SOCKET_STATE_BOUND;
            else
                return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
        } else
            return UnlockAndMaybeComplete
            ( FCB, STATUS_NO_MEMORY, Irp, 0 );
    }

    SendReq->BufferArray = LockBuffers( SendReq->BufferArray,
                                        SendReq->BufferCount,
                                        NULL, NULL,
                                        FALSE, FALSE, LockMode );

    if( !SendReq->BufferArray )
        return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                       Irp, 0 );

    AFD_DbgPrint
        (MID_TRACE,("RemoteAddress #%d Type %u\n",
                    ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                    TAAddressCount,
                    ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                    Address[0].AddressType));

    Status = TdiBuildConnectionInfo( &TargetAddress,
                            ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress) );

    /* Check the size of the Address given ... */

    if( NT_SUCCESS(Status) ) {
        FCB->PollState &= ~AFD_EVENT_SEND;

        Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
        if (Status == STATUS_PENDING)
        {
            if (SendReq->BufferCount > 1)
            {
                AFD_DbgPrint(MIN_TRACE,("WARN: More than one buffer %ld\n", SendReq->BufferCount));
            }
            Status = TdiSendDatagram(&FCB->SendIrp.InFlightRequest,
                                     FCB->AddressFile.Object,
                                     SendReq->BufferArray[0].buf,
                                     SendReq->BufferArray[0].len,
                                     TargetAddress,
                                     PacketSocketSendComplete,
                                     FCB);
            if (Status != STATUS_PENDING)
            {
                NT_VERIFY(RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]) == &Irp->Tail.Overlay.ListEntry);
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = ((Status == STATUS_SUCCESS) ? SendReq->BufferArray[0].len : 0);
                (void)IoSetCancelRoutine(Irp, NULL);
                UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
                UnlockRequest(Irp, IoGetCurrentIrpStackLocation(Irp));
                IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
            }
        }

        ExFreePoolWithTag(TargetAddress, TAG_AFD_TDI_CONNECTION_INFORMATION);

        SocketStateUnlock(FCB);

        return STATUS_PENDING;
    }
    else
    {
        UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
        return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
    }
}
