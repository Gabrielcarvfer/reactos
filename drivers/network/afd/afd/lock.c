/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/net/afd/afd/lock.c
 * PURPOSE:          Ancillary functions driver
 * PROGRAMMER:       Art Yerkes (ayerkes@speakeasy.net)
 * UPDATE HISTORY:
 * 20040708 Created
 */

#include "afd.h"

PVOID GetLockedData(PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    ASSERT(Irp->MdlAddress);
    ASSERT(Irp->Tail.Overlay.DriverContext[0]);

    UNREFERENCED_PARAMETER(IrpSp);

    return Irp->Tail.Overlay.DriverContext[0];
}

#ifdef _WIN64
BOOLEAN AfdIs32bitRequest( PIRP Irp )
{
    return Irp->RequestorMode == UserMode && IoIs32bitProcess(Irp);
}

/* A 32-bit program sent this request: replace the locked copy of its
 * structure by the native layout (the pointers and handles widened) */
static BOOLEAN WidenRequest32( PIRP Irp, PIO_STACK_LOCATION IrpSp )
{
    PVOID In = Irp->Tail.Overlay.DriverContext[0], Out;
    ULONG InLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG InSize, OutSize;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_AFD_RECV:
        case IOCTL_AFD_SEND:
            InSize = sizeof(AFD_RECV_INFO32); OutSize = sizeof(AFD_RECV_INFO); break;
        case IOCTL_AFD_RECV_DATAGRAM:
            InSize = sizeof(AFD_RECV_INFO_UDP32); OutSize = sizeof(AFD_RECV_INFO_UDP); break;
        case IOCTL_AFD_SEND_DATAGRAM:
            InSize = sizeof(AFD_SEND_INFO_UDP32); OutSize = sizeof(AFD_SEND_INFO_UDP); break;
        case IOCTL_AFD_EVENT_SELECT:
            InSize = sizeof(AFD_EVENT_SELECT_INFO32); OutSize = sizeof(AFD_EVENT_SELECT_INFO); break;
        case IOCTL_AFD_ENUM_NETWORK_EVENTS:
            InSize = sizeof(AFD_ENUM_NETWORK_EVENTS_INFO32); OutSize = sizeof(AFD_ENUM_NETWORK_EVENTS_INFO); break;
        case IOCTL_AFD_SUPER_ACCEPT:
            InSize = sizeof(AFD_SUPER_ACCEPT_INFO32); OutSize = sizeof(AFD_SUPER_ACCEPT_INFO); break;
        default:
            return TRUE; /* the same in both layouts */
    }

    if (InLength < InSize) return FALSE;
    Out = ExAllocatePoolWithTag(NonPagedPool, OutSize, TAG_AFD_DATA_BUFFER);
    if (!Out) return FALSE;
    RtlZeroMemory(Out, OutSize);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_AFD_RECV:
        case IOCTL_AFD_SEND:
        {
            PAFD_RECV_INFO32 From = In;
            PAFD_RECV_INFO To = Out;
            To->BufferArray = ULongToPtr(From->BufferArray);
            To->BufferCount = From->BufferCount;
            To->AfdFlags = From->AfdFlags;
            To->TdiFlags = From->TdiFlags;
            break;
        }
        case IOCTL_AFD_RECV_DATAGRAM:
        {
            PAFD_RECV_INFO_UDP32 From = In;
            PAFD_RECV_INFO_UDP To = Out;
            To->BufferArray = ULongToPtr(From->BufferArray);
            To->BufferCount = From->BufferCount;
            To->AfdFlags = From->AfdFlags;
            To->TdiFlags = From->TdiFlags;
            To->Address = ULongToPtr(From->Address);
            To->AddressLength = ULongToPtr(From->AddressLength);
            break;
        }
        case IOCTL_AFD_SEND_DATAGRAM:
        {
            PAFD_SEND_INFO_UDP32 From = In;
            PAFD_SEND_INFO_UDP To = Out;
            To->BufferArray = ULongToPtr(From->BufferArray);
            To->BufferCount = From->BufferCount;
            To->AfdFlags = From->AfdFlags;
            To->TdiRequest.Request.Handle.AddressHandle = AfdHandle32(From->TdiRequest.Handle);
            To->TdiRequest.Request.RequestNotifyObject = ULongToPtr(From->TdiRequest.RequestNotifyObject);
            To->TdiRequest.Request.RequestContext = ULongToPtr(From->TdiRequest.RequestContext);
            To->TdiRequest.Request.TdiStatus = From->TdiRequest.TdiStatus;
            To->TdiRequest.SendDatagramInformation = ULongToPtr(From->TdiRequest.SendDatagramInformation);
            To->TdiConnection.UserDataLength = From->TdiConnection.UserDataLength;
            To->TdiConnection.UserData = ULongToPtr(From->TdiConnection.UserData);
            To->TdiConnection.OptionsLength = From->TdiConnection.OptionsLength;
            To->TdiConnection.Options = ULongToPtr(From->TdiConnection.Options);
            To->TdiConnection.RemoteAddressLength = From->TdiConnection.RemoteAddressLength;
            To->TdiConnection.RemoteAddress = ULongToPtr(From->TdiConnection.RemoteAddress);
            break;
        }
        case IOCTL_AFD_EVENT_SELECT:
        {
            PAFD_EVENT_SELECT_INFO32 From = In;
            PAFD_EVENT_SELECT_INFO To = Out;
            To->EventObject = AfdHandle32(From->EventObject);
            To->Events = From->Events;
            break;
        }
        case IOCTL_AFD_ENUM_NETWORK_EVENTS:
        {
            PAFD_ENUM_NETWORK_EVENTS_INFO32 From = In;
            PAFD_ENUM_NETWORK_EVENTS_INFO To = Out;
            To->Event = AfdHandle32(From->Event);
            To->PollEvents = From->PollEvents;
            RtlCopyMemory(To->EventStatus, From->EventStatus, sizeof(To->EventStatus));
            break;
        }
        case IOCTL_AFD_SUPER_ACCEPT:
        {
            PAFD_SUPER_ACCEPT_INFO32 From = In;
            PAFD_SUPER_ACCEPT_INFO To = Out;
            To->SanActive = From->SanActive;
            To->FixAddressAlignment = From->FixAddressAlignment;
            To->AcceptHandle = AfdHandle32(From->AcceptHandle);
            To->ReceiveDataLength = From->ReceiveDataLength;
            To->LocalAddressLength = From->LocalAddressLength;
            To->RemoteAddressLength = From->RemoteAddressLength;
            break;
        }
    }

    ExFreePoolWithTag(In, TAG_AFD_DATA_BUFFER);
    Irp->Tail.Overlay.DriverContext[0] = Out;
    return TRUE;
}

/* select() from a 32-bit program: give AFD a native copy of the poll
 * request in place of the buffered one */
NTSTATUS AfdWidenPollRequest( PIRP Irp, PIO_STACK_LOCATION IrpSp )
{
    PAFD_POLL_INFO32 From = Irp->AssociatedIrp.SystemBuffer;
    ULONG InLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    PAFD_POLL_INFO To;
    ULONG Count, i;

    if (!From || InLength < FIELD_OFFSET(AFD_POLL_INFO32, Handles))
        return STATUS_INVALID_PARAMETER;
    Count = From->HandleCount;
    if (Count > (InLength - FIELD_OFFSET(AFD_POLL_INFO32, Handles)) / sizeof(AFD_HANDLE32) ||
        OutLength < FIELD_OFFSET(AFD_POLL_INFO32, Handles) + Count * sizeof(AFD_HANDLE32))
        return STATUS_INVALID_PARAMETER;

    /* The I/O manager frees the system buffer with its own tag */
    To = ExAllocatePoolWithTag(NonPagedPool,
                               FIELD_OFFSET(AFD_POLL_INFO, Handles) + Count * sizeof(AFD_HANDLE),
                               'UBOI');
    if (!To) return STATUS_NO_MEMORY;

    To->Timeout = From->Timeout;
    To->HandleCount = Count;
    To->Exclusive = From->Exclusive;
    for (i = 0; i < Count; i++)
    {
        To->Handles[i].Handle = (SOCKET)AfdHandle32(From->Handles[i].Handle);
        To->Handles[i].Events = From->Handles[i].Events;
        To->Handles[i].Status = From->Handles[i].Status;
    }

    ExFreePoolWithTag(From, 'UBOI');
    Irp->AssociatedIrp.SystemBuffer = To;
    return STATUS_SUCCESS;
}

/* Back to the 32-bit layout, in place, before the I/O manager copies it out */
VOID AfdNarrowPollRequest( PIRP Irp )
{
    PAFD_POLL_INFO From = Irp->AssociatedIrp.SystemBuffer;
    PAFD_POLL_INFO32 To = Irp->AssociatedIrp.SystemBuffer;
    ULONG Count = From->HandleCount, i;
    AFD_HANDLE32 Handle;

    To->HandleCount = Count;
    To->Exclusive = 0;
    for (i = 0; i < Count; i++)
    {
        /* Each 32-bit entry is at or before the native one it comes from */
        Handle.Handle = (ULONG)From->Handles[i].Handle;
        Handle.Events = From->Handles[i].Events;
        Handle.Status = From->Handles[i].Status;
        To->Handles[i] = Handle;
    }
    Irp->IoStatus.Information = FIELD_OFFSET(AFD_POLL_INFO32, Handles) + Count * sizeof(AFD_HANDLE32);
}
#endif

/* Lock a method_neither request so it'll be available from DISPATCH_LEVEL */
PVOID LockRequest( PIRP Irp,
                   PIO_STACK_LOCATION IrpSp,
                   BOOLEAN Output,
                   KPROCESSOR_MODE *LockMode) {
    BOOLEAN LockFailed = FALSE;

    ASSERT(!Irp->MdlAddress);

    switch (IrpSp->MajorFunction)
    {
        case IRP_MJ_DEVICE_CONTROL:
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            ASSERT(IrpSp->Parameters.DeviceIoControl.Type3InputBuffer);
            ASSERT(IrpSp->Parameters.DeviceIoControl.InputBufferLength);


            Irp->MdlAddress =
            IoAllocateMdl( IrpSp->Parameters.DeviceIoControl.Type3InputBuffer,
                          IrpSp->Parameters.DeviceIoControl.InputBufferLength,
                          FALSE,
                          FALSE,
                          NULL );
            if( Irp->MdlAddress ) {
                _SEH2_TRY {
                    MmProbeAndLockPages( Irp->MdlAddress, Irp->RequestorMode, IoModifyAccess );
                } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                    LockFailed = TRUE;
                } _SEH2_END;

                if( LockFailed ) {
                    AFD_DbgPrint(MIN_TRACE,("Failed to lock pages\n"));
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }

                /* The mapped address goes in index 1 */
                Irp->Tail.Overlay.DriverContext[1] = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
                if (!Irp->Tail.Overlay.DriverContext[1])
                {
                    AFD_DbgPrint(MIN_TRACE,("Failed to get mapped address\n"));
                    MmUnlockPages(Irp->MdlAddress);
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }

                /* The allocated address goes in index 0 */
                Irp->Tail.Overlay.DriverContext[0] = ExAllocatePoolWithTag(NonPagedPool,
                                                                           MmGetMdlByteCount(Irp->MdlAddress),
                                                                           TAG_AFD_DATA_BUFFER);

                if (!Irp->Tail.Overlay.DriverContext[0])
                {
                    AFD_DbgPrint(MIN_TRACE,("Failed to allocate memory\n"));
                    MmUnlockPages(Irp->MdlAddress);
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }

                RtlCopyMemory(Irp->Tail.Overlay.DriverContext[0],
                              Irp->Tail.Overlay.DriverContext[1],
                              MmGetMdlByteCount(Irp->MdlAddress));

#ifdef _WIN64
                if (AfdIs32bitRequest(Irp) && !WidenRequest32(Irp, IrpSp))
                {
                    AFD_DbgPrint(MIN_TRACE,("Failed to convert a 32-bit request\n"));
                    ExFreePoolWithTag(Irp->Tail.Overlay.DriverContext[0], TAG_AFD_DATA_BUFFER);
                    MmUnlockPages(Irp->MdlAddress);
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }
#endif

                /* If we don't want a copy back, we zero the mapped address pointer */
                if (!Output)
                {
                    Irp->Tail.Overlay.DriverContext[1] = NULL;
                }

                /* We're using a user-mode buffer directly */
                if (LockMode != NULL)
                {
                    *LockMode = UserMode;
                }
            }
            else return NULL;
            break;

        case IRP_MJ_READ:
        case IRP_MJ_WRITE:
            ASSERT(Irp->UserBuffer);

            Irp->MdlAddress =
            IoAllocateMdl(Irp->UserBuffer,
                          (IrpSp->MajorFunction == IRP_MJ_READ) ?
                                IrpSp->Parameters.Read.Length : IrpSp->Parameters.Write.Length,
                          FALSE,
                          FALSE,
                          NULL );
            if( Irp->MdlAddress ) {
                PAFD_RECV_INFO AfdInfo;

                _SEH2_TRY {
                    MmProbeAndLockPages( Irp->MdlAddress, Irp->RequestorMode, IoModifyAccess );
                } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                    LockFailed = TRUE;
                } _SEH2_END;

                if( LockFailed ) {
                    AFD_DbgPrint(MIN_TRACE,("Failed to lock pages\n"));
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }

                /* We need to create the info struct that AFD expects for all send/recv requests */
                AfdInfo = ExAllocatePoolWithTag(NonPagedPool,
                                                sizeof(AFD_RECV_INFO) + sizeof(AFD_WSABUF),
                                                TAG_AFD_DATA_BUFFER);

                if (!AfdInfo)
                {
                    AFD_DbgPrint(MIN_TRACE,("Failed to allocate memory\n"));
                    MmUnlockPages(Irp->MdlAddress);
                    IoFreeMdl( Irp->MdlAddress );
                    Irp->MdlAddress = NULL;
                    return NULL;
                }

                /* We'll append the buffer array to this struct */
                AfdInfo->BufferArray = (PAFD_WSABUF)(AfdInfo + 1);
                AfdInfo->BufferCount = 1;

                /* Setup the default flags values */
                AfdInfo->AfdFlags = 0;
                AfdInfo->TdiFlags = 0;

                /* Now build the buffer array */
                AfdInfo->BufferArray[0].buf = MmGetSystemAddressForMdl(Irp->MdlAddress);
                AfdInfo->BufferArray[0].len = MmGetMdlByteCount(Irp->MdlAddress);

                /* Store the struct where AFD expects */
                Irp->Tail.Overlay.DriverContext[0] = AfdInfo;

                /* Don't copy anything out */
                Irp->Tail.Overlay.DriverContext[1] = NULL;

                /* We're using a placeholder buffer that we allocated */
                if (LockMode != NULL)
                {
                    *LockMode = KernelMode;
                }
            }
            else return NULL;
            break;

        default:
            ASSERT(FALSE);
            return NULL;
    }

    return GetLockedData(Irp, IrpSp);
}

VOID UnlockRequest( PIRP Irp, PIO_STACK_LOCATION IrpSp )
{
    ASSERT(Irp->MdlAddress);
    ASSERT(Irp->Tail.Overlay.DriverContext[0]);

    UNREFERENCED_PARAMETER(IrpSp);

    /* Check if we need to copy stuff back */
    if (Irp->Tail.Overlay.DriverContext[1] != NULL)
    {
#ifdef _WIN64
        IrpSp = IoGetCurrentIrpStackLocation(Irp);
        if (IrpSp->MajorFunction == IRP_MJ_DEVICE_CONTROL &&
            IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_AFD_ENUM_NETWORK_EVENTS &&
            AfdIs32bitRequest(Irp))
        {
            PAFD_ENUM_NETWORK_EVENTS_INFO From = Irp->Tail.Overlay.DriverContext[0];
            PAFD_ENUM_NETWORK_EVENTS_INFO32 To = Irp->Tail.Overlay.DriverContext[1];
            To->PollEvents = From->PollEvents;
            RtlCopyMemory(To->EventStatus, From->EventStatus, sizeof(To->EventStatus));
        }
        else
#endif
        RtlCopyMemory(Irp->Tail.Overlay.DriverContext[1],
                      Irp->Tail.Overlay.DriverContext[0],
                      MmGetMdlByteCount(Irp->MdlAddress));
    }

    ExFreePoolWithTag(Irp->Tail.Overlay.DriverContext[0], TAG_AFD_DATA_BUFFER);
    MmUnlockPages( Irp->MdlAddress );
    IoFreeMdl( Irp->MdlAddress );
    Irp->MdlAddress = NULL;
}

/* Note: We add an extra buffer if LockAddress is true.  This allows us to
 * treat the address buffer as an ordinary client buffer.  It's only used
 * for datagrams. */

PAFD_WSABUF LockBuffers( PAFD_WSABUF Buf, UINT Count,
                         PVOID AddressBuf, PINT AddressLen,
                         BOOLEAN Write, BOOLEAN LockAddress,
                         KPROCESSOR_MODE LockMode) {
    UINT i;
    /* Copy the buffer array so we don't lose it */
    UINT Lock = LockAddress ? 2 : 0;
    UINT Size = (sizeof(AFD_WSABUF) + sizeof(AFD_MAPBUF)) * (Count + Lock);
    PAFD_WSABUF NewBuf = ExAllocatePoolWithTag(PagedPool, Size, TAG_AFD_WSA_BUFFER);
    BOOLEAN LockFailed = FALSE;
    PAFD_MAPBUF MapBuf;

    AFD_DbgPrint(MID_TRACE,("Called(%p)\n", NewBuf));

    if( NewBuf ) {
        RtlZeroMemory(NewBuf, Size);

        MapBuf = (PAFD_MAPBUF)(NewBuf + Count + Lock);

        _SEH2_TRY {
#ifdef _WIN64
            /* A 32-bit program's buffer array */
            if (LockMode == UserMode && IoIs32bitProcess(NULL))
            {
                PAFD_WSABUF32 Buf32 = (PAFD_WSABUF32)Buf;
                for (i = 0; i < Count; i++)
                {
                    NewBuf[i].len = Buf32[i].len;
                    NewBuf[i].buf = ULongToPtr(Buf32[i].buf);
                }
            }
            else
#endif
            RtlCopyMemory( NewBuf, Buf, sizeof(AFD_WSABUF) * Count );
            if( LockAddress ) {
                if (AddressBuf && AddressLen) {
                    NewBuf[Count].buf = AddressBuf;
                    NewBuf[Count].len = *AddressLen;
                    NewBuf[Count + 1].buf = (PVOID)AddressLen;
                    NewBuf[Count + 1].len = sizeof(*AddressLen);
                }
                Count += 2;
            }
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            AFD_DbgPrint(MIN_TRACE,("Access violation copying buffer info "
                                    "from userland (%p %p)\n",
                                    Buf, AddressLen));
            ExFreePoolWithTag(NewBuf, TAG_AFD_WSA_BUFFER);
            _SEH2_YIELD(return NULL);
        } _SEH2_END;

        for( i = 0; i < Count; i++ ) {
            AFD_DbgPrint(MID_TRACE,("Locking buffer %u (%p:%u)\n",
                                    i, NewBuf[i].buf, NewBuf[i].len));

            if( NewBuf[i].buf && NewBuf[i].len ) {
                MapBuf[i].Mdl = IoAllocateMdl( NewBuf[i].buf,
                                               NewBuf[i].len,
                                               FALSE,
                                               FALSE,
                                               NULL );
            } else {
                MapBuf[i].Mdl = NULL;
                continue;
            }

            AFD_DbgPrint(MID_TRACE,("NewMdl @ %p\n", MapBuf[i].Mdl));

            if( MapBuf[i].Mdl ) {
                AFD_DbgPrint(MID_TRACE,("Probe and lock pages\n"));
                _SEH2_TRY {
                    MmProbeAndLockPages( MapBuf[i].Mdl, LockMode,
                                         Write ? IoModifyAccess : IoReadAccess );
                } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                    LockFailed = TRUE;
                } _SEH2_END;
                AFD_DbgPrint(MID_TRACE,("MmProbeAndLock finished\n"));

                if( LockFailed ) {
            AFD_DbgPrint(MIN_TRACE,("Failed to lock pages\n"));
                    IoFreeMdl( MapBuf[i].Mdl );
                    MapBuf[i].Mdl = NULL;
                    ExFreePoolWithTag(NewBuf, TAG_AFD_WSA_BUFFER);
                    return NULL;
                }
            } else {
                ExFreePoolWithTag(NewBuf, TAG_AFD_WSA_BUFFER);
                return NULL;
            }
        }
    }

    AFD_DbgPrint(MID_TRACE,("Leaving %p\n", NewBuf));

    return NewBuf;
}

VOID UnlockBuffers( PAFD_WSABUF Buf, UINT Count, BOOL Address ) {
    UINT Lock = Address ? 2 : 0;
    PAFD_MAPBUF Map = (PAFD_MAPBUF)(Buf + Count + Lock);
    UINT i;

    if( !Buf ) return;

    for( i = 0; i < Count + Lock; i++ ) {
        if( Map[i].Mdl ) {
            MmUnlockPages( Map[i].Mdl );
            IoFreeMdl( Map[i].Mdl );
            Map[i].Mdl = NULL;
        }
    }

    ExFreePoolWithTag(Buf, TAG_AFD_WSA_BUFFER);
    Buf = NULL;
}

/* Produce a kernel-land handle array with handles replaced by object
 * pointers.  This will allow the system to do proper alerting */
PAFD_HANDLE LockHandles( PAFD_HANDLE HandleArray, UINT HandleCount ) {
    UINT i;
    NTSTATUS Status = STATUS_SUCCESS;

    PAFD_HANDLE FileObjects = ExAllocatePoolWithTag(NonPagedPool,
                                                    HandleCount * sizeof(AFD_HANDLE),
                                                    TAG_AFD_POLL_HANDLE);

    for( i = 0; FileObjects && i < HandleCount; i++ ) {
        FileObjects[i].Status = 0;
        FileObjects[i].Events = HandleArray[i].Events;
        FileObjects[i].Handle = 0;
        if( !HandleArray[i].Handle ) continue;
        if( NT_SUCCESS(Status) ) {
                Status = ObReferenceObjectByHandle
                    ( (PVOID)HandleArray[i].Handle,
                      FILE_ALL_ACCESS,
                      NULL,
                       KernelMode,
                       (PVOID*)&FileObjects[i].Handle,
                       NULL );
        }

        if( !NT_SUCCESS(Status) )
        {
            AFD_DbgPrint(MIN_TRACE,("Failed to reference handles (0x%x)\n", Status));
            FileObjects[i].Handle = 0;
        }
    }

    if( !NT_SUCCESS(Status) ) {
        UnlockHandles( FileObjects, HandleCount );
        return NULL;
    }

    return FileObjects;
}

VOID UnlockHandles( PAFD_HANDLE HandleArray, UINT HandleCount ) {
    UINT i;

    for( i = 0; i < HandleCount; i++ ) {
        if( HandleArray[i].Handle )
            ObDereferenceObject( (PVOID)HandleArray[i].Handle );
    }

    ExFreePoolWithTag(HandleArray, TAG_AFD_POLL_HANDLE);
    HandleArray = NULL;
}

BOOLEAN SocketAcquireStateLock( PAFD_FCB FCB ) {
    if( !FCB ) return FALSE;

    return !KeWaitForMutexObject(&FCB->Mutex,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 NULL);
}

VOID SocketStateUnlock( PAFD_FCB FCB ) {
    KeReleaseMutex(&FCB->Mutex, FALSE);
}

NTSTATUS NTAPI UnlockAndMaybeComplete
( PAFD_FCB FCB, NTSTATUS Status, PIRP Irp,
  UINT Information ) {
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    if ( Irp->MdlAddress ) UnlockRequest( Irp, IoGetCurrentIrpStackLocation( Irp ) );
    (void)IoSetCancelRoutine(Irp, NULL);
    SocketStateUnlock( FCB );
    IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
    return Status;
}


NTSTATUS LostSocket( PIRP Irp ) {
    NTSTATUS Status = STATUS_FILE_CLOSED;
    AFD_DbgPrint(MIN_TRACE,("Called.\n"));
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    if ( Irp->MdlAddress ) UnlockRequest( Irp, IoGetCurrentIrpStackLocation( Irp ) );
    IoCompleteRequest( Irp, IO_NO_INCREMENT );
    return Status;
}

NTSTATUS QueueUserModeIrp(PAFD_FCB FCB, PIRP Irp, UINT Function)
{
    NTSTATUS Status;

    /* Add the IRP to the queue in all cases (so AfdCancelHandler will work properly) */
    InsertTailList( &FCB->PendingIrpList[Function],
                   &Irp->Tail.Overlay.ListEntry );

    /* Acquire the cancel spin lock and check the cancel bit */
    IoAcquireCancelSpinLock(&Irp->CancelIrql);
    if (!Irp->Cancel)
    {
        /* We are not cancelled; we're good to go so
         * set the cancel routine, release the cancel spin lock,
         * mark the IRP as pending, and
         * return STATUS_PENDING to the caller
         */
        (void)IoSetCancelRoutine(Irp, AfdCancelHandler);
        IoReleaseCancelSpinLock(Irp->CancelIrql);
        IoMarkIrpPending(Irp);
        Status = STATUS_PENDING;
    }
    else
    {
        /* We were already cancelled before we were able to register our cancel routine
         * so we are to call the cancel routine ourselves right here to cancel the IRP
         * (which handles all the stuff we do above) and return STATUS_CANCELLED to the caller
         */
        AfdCancelHandler(IoGetCurrentIrpStackLocation(Irp)->DeviceObject,
                         Irp);
        Status = STATUS_CANCELLED;
    }

    return Status;
}

NTSTATUS LeaveIrpUntilLater( PAFD_FCB FCB, PIRP Irp, UINT Function ) {
    NTSTATUS Status;

    Status = QueueUserModeIrp(FCB, Irp, Function);

    SocketStateUnlock( FCB );

    return Status;
}
