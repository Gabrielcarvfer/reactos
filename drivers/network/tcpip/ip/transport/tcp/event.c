/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/tcp/event.c
 * PURPOSE:     Transmission Control Protocol
 * PROGRAMMERS: Cameron Gutman (cameron.gutman@reactos.org)
 */

#include "precomp.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/api.h"
#include "lwip/tcpip.h"

#include <lwip_glue/lwip_glue.h>

extern NPAGED_LOOKASIDE_LIST TdiBucketLookasideList;

static
VOID
BucketCompletionWorker(PVOID Context)
{
    PTDI_BUCKET Bucket = (PTDI_BUCKET)Context;
    PTCP_COMPLETION_ROUTINE Complete;

    Complete = (PTCP_COMPLETION_ROUTINE)Bucket->Request.RequestNotifyObject;

    Complete(Bucket->Request.RequestContext, Bucket->Status, Bucket->Information);

    DereferenceObject(Bucket->AssociatedEndpoint);

    ExFreeToNPagedLookasideList(&TdiBucketLookasideList, Bucket);
}

/* Requests finished under lwIP's core lock, completed when it is released
 * (protected by the core lock) */
static LIST_ENTRY DeferredCompletions = { &DeferredCompletions, &DeferredCompletions };

/* Connections that got data: their receive requests are filled when the core
 * lock is released (or at the end of a batch of received packets), with all
 * the data that arrived meanwhile, instead of one completion per segment */
static LIST_ENTRY RecvNotifyList = { &RecvNotifyList, &RecvNotifyList };
static PKTHREAD ReceiveBatchThread;

VOID
TCPQueueRecvNotify(PCONNECTION_ENDPOINT Connection)
{
    ASSERT(sys_core_locked());

    if (Connection->RecvNotifyQueued)
        return;

    Connection->RecvNotifyQueued = TRUE;
    ReferenceObject(Connection);
    InsertTailList(&RecvNotifyList, &Connection->RecvNotifyEntry);
}

ULONG
TCPBeginReceiveBatch(VOID)
{
    ReceiveBatchThread = KeGetCurrentThread();
    LibTCPBeginDeferOutput();
    return TCPEnterCompletionContext();
}

VOID
TCPEndReceiveBatch(ULONG Context)
{
    ReceiveBatchThread = NULL;

    /* Releasing the lock fills the requests (and the readers take the data,
     * which opens the windows)... */
    LOCK_TCPIP_CORE();
    UNLOCK_TCPIP_CORE();

    /* ...then acknowledge the batch, with the new windows */
    LOCK_TCPIP_CORE();
    LibTCPEndDeferOutput();
    UNLOCK_TCPIP_CORE();

    TCPLeaveCompletionContext(Context);
}

VOID
LibTCPTakeDeferredCompletions(PLIST_ENTRY List)
{
    PCONNECTION_ENDPOINT Connection;

    ASSERT(sys_core_locked());

    if (ReceiveBatchThread != KeGetCurrentThread())
    {
        while (!IsListEmpty(&RecvNotifyList))
        {
            Connection = CONTAINING_RECORD(RemoveHeadList(&RecvNotifyList),
                                           CONNECTION_ENDPOINT, RecvNotifyEntry);
            Connection->RecvNotifyQueued = FALSE;
            TCPRecvEventHandler(Connection);
            DereferenceObject(Connection);
        }
    }

    if (IsListEmpty(&DeferredCompletions))
        return;

    /* Move the whole list over */
    List->Flink = DeferredCompletions.Flink;
    List->Blink = DeferredCompletions.Blink;
    List->Flink->Blink = List;
    List->Blink->Flink = List;
    InitializeListHead(&DeferredCompletions);
}

/* Threads that hold no lock of the layers above (AFD's): the receive workers
 * and the lwIP thread. Completion routines run inline only there. Elsewhere
 * (a request from AFD, which holds its socket's lock while it calls us) a
 * completion routine taking another socket's lock could deadlock with a
 * thread doing the same the other way round */
#define COMPLETION_CONTEXT_SLOTS 8
static PKTHREAD CompletionContextThreads[COMPLETION_CONTEXT_SLOTS];
static PKTHREAD LwipThread;

ULONG
TCPEnterCompletionContext(VOID)
{
    ULONG Slot;

    for (Slot = 0; Slot < COMPLETION_CONTEXT_SLOTS; Slot++)
    {
        if (!InterlockedCompareExchangePointer((PVOID *)&CompletionContextThreads[Slot],
                                               KeGetCurrentThread(),
                                               NULL))
        {
            return Slot;
        }
    }

    /* (no slot: completions go to a work item) */
    return MAXULONG;
}

VOID
TCPLeaveCompletionContext(ULONG Slot)
{
    if (Slot < COMPLETION_CONTEXT_SLOTS)
        InterlockedExchangePointer((PVOID *)&CompletionContextThreads[Slot], NULL);
}

VOID
LibTCPSetLwipThread(VOID)
{
    LwipThread = KeGetCurrentThread();
}

static
BOOLEAN
InCompletionContext(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG Slot;

    if (Thread == LwipThread)
        return TRUE;

    for (Slot = 0; Slot < COMPLETION_CONTEXT_SLOTS; Slot++)
    {
        if (CompletionContextThreads[Slot] == Thread)
            return TRUE;
    }

    return FALSE;
}

typedef struct _DEFERRED_COMPLETIONS
{
    LIST_ENTRY List;
} DEFERRED_COMPLETIONS, *PDEFERRED_COMPLETIONS;

static
VOID
RunCompletionList(PLIST_ENTRY List)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(List))
    {
        Entry = RemoveHeadList(List);
        BucketCompletionWorker(CONTAINING_RECORD(Entry, TDI_BUCKET, Entry));
    }
}

static
VOID
DeferredCompletionsWorker(PVOID Context)
{
    PDEFERRED_COMPLETIONS Deferred = Context;

    RunCompletionList(&Deferred->List);
    ExFreePoolWithTag(Deferred, TDI_BUCKET_TAG);
}

static
VOID
MoveList(PLIST_ENTRY To, PLIST_ENTRY From)
{
    To->Flink = From->Flink;
    To->Blink = From->Blink;
    To->Flink->Blink = To;
    To->Blink->Flink = To;
    InitializeListHead(From);
}

VOID
LibTCPRunDeferredCompletions(PLIST_ENTRY List)
{
    PDEFERRED_COMPLETIONS Deferred;
    PLIST_ENTRY Entry;

    if (InCompletionContext())
    {
        RunCompletionList(List);
        return;
    }

    /* All of them in one work item */
    Deferred = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Deferred), TDI_BUCKET_TAG);
    if (Deferred)
    {
        MoveList(&Deferred->List, List);
        if (ChewCreate(DeferredCompletionsWorker, Deferred))
            return;
        MoveList(List, &Deferred->List);
        ExFreePoolWithTag(Deferred, TDI_BUCKET_TAG);
    }

    while (!IsListEmpty(List))
    {
        Entry = RemoveHeadList(List);
        ChewCreate(BucketCompletionWorker, CONTAINING_RECORD(Entry, TDI_BUCKET, Entry));
    }
}

VOID
CompleteBucket(PCONNECTION_ENDPOINT Connection, PTDI_BUCKET Bucket, const BOOLEAN Synchronous)
{
    ReferenceObject(Connection);
    Bucket->AssociatedEndpoint = Connection;
    if (Synchronous)
    {
        BucketCompletionWorker(Bucket);
    }
    else if (sys_core_locked())
    {
        /* In lwIP (a received packet, a timer): complete when the core lock
         * is released, in this thread, instead of from a work item */
        InsertTailList(&DeferredCompletions, &Bucket->Entry);
    }
    else
    {
        ChewCreate(BucketCompletionWorker, Bucket);
    }
}

VOID
FlushReceiveQueue(PCONNECTION_ENDPOINT Connection, const NTSTATUS Status)
{
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    while (!IsListEmpty(&Connection->ReceiveRequest))
    {
        Entry = RemoveHeadList(&Connection->ReceiveRequest);

        Bucket = CONTAINING_RECORD(Entry, TDI_BUCKET, Entry);

        Bucket->Information = 0;
        Bucket->Status = Status;

        CompleteBucket(Connection, Bucket, FALSE);
    }
}

VOID
FlushSendQueue(PCONNECTION_ENDPOINT Connection, const NTSTATUS Status)
{
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    while (!IsListEmpty(&Connection->SendRequest))
    {
        Entry = RemoveHeadList(&Connection->SendRequest);

        Bucket = CONTAINING_RECORD(Entry, TDI_BUCKET, Entry);

        Bucket->Information = 0;
        Bucket->Status = Status;

        CompleteBucket(Connection, Bucket, FALSE);
    }
}

VOID
FlushShutdownQueue(PCONNECTION_ENDPOINT Connection, const NTSTATUS Status)
{
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    while (!IsListEmpty(&Connection->ShutdownRequest))
    {
        Entry = RemoveHeadList(&Connection->ShutdownRequest);

        Bucket = CONTAINING_RECORD(Entry, TDI_BUCKET, Entry);

        Bucket->Information = 0;
        Bucket->Status = Status;

        CompleteBucket(Connection, Bucket, FALSE);
    }
}

VOID
FlushConnectQueue(PCONNECTION_ENDPOINT Connection, const NTSTATUS Status)
{
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    while (!IsListEmpty(&Connection->ConnectRequest))
    {
        Entry = RemoveHeadList(&Connection->ConnectRequest);
        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Bucket->Status = Status;
        Bucket->Information = 0;

        CompleteBucket(Connection, Bucket, FALSE);
    }
}

VOID
FlushListenQueue(PCONNECTION_ENDPOINT Connection, const NTSTATUS Status)
{
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    while (!IsListEmpty(&Connection->ListenRequest))
    {
        Entry = RemoveHeadList(&Connection->ListenRequest);
        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Bucket->Status = Status;
        Bucket->Information = 0;

        DereferenceObject(Bucket->AssociatedEndpoint);
        CompleteBucket(Connection, Bucket, FALSE);
    }
}

VOID
FlushAllQueues(PCONNECTION_ENDPOINT Connection, NTSTATUS Status)
{
    // flush receive queue
    FlushReceiveQueue(Connection, Status);

    /* We completed the reads successfully but we need to return failure now */
    if (Status == STATUS_SUCCESS)
    {
        Status = STATUS_FILE_CLOSED;
    }

    // flush listen queue
    FlushListenQueue(Connection, Status);

    // flush send queue
    FlushSendQueue(Connection, Status);

    // flush connect queue
    FlushConnectQueue(Connection, Status);

    // flush shutdown queue
    FlushShutdownQueue(Connection, Status);
}

VOID
TCPFinEventHandler(void *arg, const err_t err)
{
   PCONNECTION_ENDPOINT Connection = (PCONNECTION_ENDPOINT)arg, LastConnection;
   const NTSTATUS Status = TCPTranslateError(err);

   ASSERT(Connection->SocketContext == NULL);
   ASSERT(Connection->AddressFile);
   ASSERT(err != ERR_OK);

   LockObject(Connection);

   /* Complete all outstanding requests now */
   FlushAllQueues(Connection, Status);

   LockObject(Connection->AddressFile);

   /* Unlink this connection from the address file */
   if (Connection->AddressFile->Connection == Connection)
   {
       Connection->AddressFile->Connection = Connection->Next;
       DereferenceObject(Connection);
   }
   else if (Connection->AddressFile->Listener == Connection)
   {
       Connection->AddressFile->Listener = NULL;
       DereferenceObject(Connection);
   }
   else
   {
       LastConnection = Connection->AddressFile->Connection;
       while (LastConnection->Next != Connection && LastConnection->Next != NULL)
           LastConnection = LastConnection->Next;
       if (LastConnection->Next == Connection)
       {
           LastConnection->Next = Connection->Next;
           DereferenceObject(Connection);
       }
   }

   UnlockObject(Connection->AddressFile);

   /* Remove the address file from this connection */
   DereferenceObject(Connection->AddressFile);
   Connection->AddressFile = NULL;

   UnlockObject(Connection);
}

VOID
TCPAcceptEventHandler(void *arg, PTCP_PCB newpcb)
{
    PCONNECTION_ENDPOINT Connection = (PCONNECTION_ENDPOINT)arg;
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;
    PIRP Irp;
    NTSTATUS Status;

    LockObject(Connection);

    while (!IsListEmpty(&Connection->ListenRequest))
    {
        PIO_STACK_LOCATION IrpSp;

        Entry = RemoveHeadList(&Connection->ListenRequest);

        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Irp = Bucket->Request.RequestContext;
        IrpSp = IoGetCurrentIrpStackLocation( Irp );

        TI_DbgPrint(DEBUG_TCP,("[IP, TCPAcceptEventHandler] Getting the socket\n"));

        Status = TCPCheckPeerForAccept(newpcb,
                                       (PTDI_REQUEST_KERNEL)&IrpSp->Parameters);

        TI_DbgPrint(DEBUG_TCP,("Socket: Status: %x\n", Status));

        Bucket->Status = Status;
        Bucket->Information = 0;

        if (Status == STATUS_SUCCESS)
        {
            LockObject(Bucket->AssociatedEndpoint);

            /* sanity assert...this should never be in anything else but a CLOSED state */
            ASSERT( ((PTCP_PCB)Bucket->AssociatedEndpoint->SocketContext)->state == CLOSED );

            /*  free socket context created in FileOpenConnection, as we're using a new one */
            LibTCPClose(Bucket->AssociatedEndpoint, TRUE, FALSE);

            /* free previously created socket context (we don't use it, we use newpcb) */
            Bucket->AssociatedEndpoint->SocketContext = newpcb;

            UnlockObject(Bucket->AssociatedEndpoint);

            LibTCPAccept(newpcb, (PTCP_PCB)Connection->SocketContext, Bucket->AssociatedEndpoint);
        }

        DereferenceObject(Bucket->AssociatedEndpoint);

        CompleteBucket(Connection, Bucket, FALSE);

        if (Status == STATUS_SUCCESS)
        {
            break;
        }
    }

    UnlockObject(Connection);
}

VOID
TCPSendEventHandler(void *arg, const u16_t space)
{
    PCONNECTION_ENDPOINT Connection = (PCONNECTION_ENDPOINT)arg;
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;
    PIRP Irp;
    NTSTATUS Status;
    PMDL Mdl;
    ULONG BytesSent;

    ReferenceObject(Connection);
    LockObject(Connection);

    while (!IsListEmpty(&Connection->SendRequest))
    {
        UINT SendLen = 0;
        PVOID SendBuffer = 0;

        Entry = RemoveHeadList(&Connection->SendRequest);

        UnlockObject(Connection);

        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Irp = Bucket->Request.RequestContext;
        Mdl = Irp->MdlAddress;

        TI_DbgPrint(DEBUG_TCP,
                    ("Getting the user buffer from %x\n", Mdl));

        NdisQueryBuffer( Mdl, &SendBuffer, &SendLen );

        TI_DbgPrint(DEBUG_TCP,
                    ("Writing %d bytes to %x\n", SendLen, SendBuffer));

        TI_DbgPrint(DEBUG_TCP, ("Connection: %x\n", Connection));
        TI_DbgPrint
        (DEBUG_TCP,
         ("Connection->SocketContext: %x\n",
          Connection->SocketContext));

        Status = TCPTranslateError(LibTCPSend(Connection,
                                              SendBuffer,
                                              SendLen, &BytesSent, TRUE));

        TI_DbgPrint(DEBUG_TCP,("TCP Bytes: %d\n", BytesSent));

        if( Status == STATUS_PENDING )
        {
            LockObject(Connection);
            InsertHeadList(&Connection->SendRequest, &Bucket->Entry);
            break;
        }
        else
        {
            TI_DbgPrint(DEBUG_TCP,
                        ("Completing Send request: %x %x\n",
                         Bucket->Request, Status));

            Bucket->Status = Status;
            Bucket->Information = (Bucket->Status == STATUS_SUCCESS) ? BytesSent : 0;

            CompleteBucket(Connection, Bucket, FALSE);
        }

        LockObject(Connection);
    }

    //  If we completed all outstanding send requests then finish all pending shutdown requests,
    //  cancel the timer and dereference the connection
    if (IsListEmpty(&Connection->SendRequest))
    {
        FlushShutdownQueue(Connection, STATUS_SUCCESS);

        if (KeCancelTimer(&Connection->DisconnectTimer))
        {
            DereferenceObject(Connection);
        }
    }

    UnlockObject(Connection);

    DereferenceObject(Connection);
}

VOID
TCPRecvEventHandler(void *arg)
{
    PCONNECTION_ENDPOINT Connection = (PCONNECTION_ENDPOINT)arg;
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;
    PIRP Irp;
    PMDL Mdl;
    UINT Received;
    UINT RecvLen;
    PUCHAR RecvBuffer;
    NTSTATUS Status;

    ULONG Consumed = 0;

    /* (called in lwIP, under its core lock) */
    LockObject(Connection);

    while(!IsListEmpty(&Connection->ReceiveRequest))
    {
        Entry = RemoveHeadList(&Connection->ReceiveRequest);
        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Irp = Bucket->Request.RequestContext;
        Mdl = Irp->MdlAddress;

        NdisQueryBuffer( Mdl, &RecvBuffer, &RecvLen );

        Status = LibTCPGetDataFromConnectionQueue(Connection, RecvBuffer, RecvLen, &Received);
        if (Status == STATUS_PENDING)
        {
            InsertHeadList(&Connection->ReceiveRequest, &Bucket->Entry);
            break;
        }

        Bucket->Status = Status;
        Bucket->Information = Received;
        if (Status == STATUS_SUCCESS)
            Consumed += Received;

        CompleteBucket(Connection, Bucket, FALSE);
    }
    UnlockObject(Connection);

    LibTCPRecved(Connection, Consumed);
}

VOID
TCPConnectEventHandler(void *arg, const err_t err)
{
    PCONNECTION_ENDPOINT Connection = (PCONNECTION_ENDPOINT)arg;
    PTDI_BUCKET Bucket;
    PLIST_ENTRY Entry;

    LockObject(Connection);

    while (!IsListEmpty(&Connection->ConnectRequest))
    {
        Entry = RemoveHeadList(&Connection->ConnectRequest);

        Bucket = CONTAINING_RECORD( Entry, TDI_BUCKET, Entry );

        Bucket->Status = TCPTranslateError(err);
        Bucket->Information = 0;

        CompleteBucket(Connection, Bucket, FALSE);
    }

    UnlockObject(Connection);
}
