/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmnotify.c
 * PURPOSE:         Configuration Manager - Registry change notifications
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/*
 * Every key body that was ever watched (NtNotifyChangeKey) owns one notify
 * block, which stays on the global notify list until the key body goes away.
 * Each NtNotifyChangeKey call adds one post block to it. A change reported
 * by CmpReportNotify completes (and frees) all the posts of every matching
 * notify block; a change that happens while a notify block has no post is
 * remembered (NotifyPending) and completes the next post right away, so a
 * caller re-arming after a notification doesn't miss what changed meanwhile.
 *
 * Locking: CmpNotifyLock protects the notify list, the notify blocks and
 * their posts, and KeyBody->NotifyBlock. It's always acquired last (after
 * the registry lock and the KCB locks). Posts are completed after releasing
 * it: their status block is written in the caller's process, their event is
 * signalled and their APC queued.
 *
 * Posts are not tied to the calling thread: a thread exiting doesn't cancel
 * its notifications (as if REG_NOTIFY_THREAD_AGNOSTIC was always given).
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

#define TAG_CM_NOTIFY 'tNmC'

#if defined(_WIN64) && defined(BUILD_WOW64_ENABLED)
/* Marker of the 64-bit status block WoW64 passes for a 32-bit one (see irp.c) */
#define CMP_WOW64_IOSB_NOT_WRITTEN ((ULONG_PTR)0xbadb10cbadb10c00ull)
#endif

typedef struct _CM_POST_BLOCK
{
    LIST_ENTRY PostListEntry;
    LONG RefCount;
    BOOLEAN Synchronous;
    BOOLEAN Completed;
    KPROCESSOR_MODE Mode;
    NTSTATUS Status;
    KEVENT SyncEvent;
    PKEVENT UserEvent;
    PIO_STATUS_BLOCK IoStatusBlock;
    PVOID IoStatusBlock32;
    PEPROCESS Process;
    PETHREAD Thread;
    PKAPC Apc;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
} CM_POST_BLOCK, *PCM_POST_BLOCK;

/* GLOBALS *******************************************************************/

static KGUARDED_MUTEX CmpNotifyLock;
static LIST_ENTRY CmpNotifyListHead;

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitializeNotify(VOID)
{
    KeInitializeGuardedMutex(&CmpNotifyLock);
    InitializeListHead(&CmpNotifyListHead);
}

static
VOID
CmpDereferencePost(IN PCM_POST_BLOCK Post)
{
    if (InterlockedDecrement(&Post->RefCount)) return;

    if (Post->UserEvent) ObDereferenceObjectDeferDelete(Post->UserEvent);
    if (Post->Apc) ExFreePoolWithTag(Post->Apc, TAG_CM_NOTIFY);
    if (Post->Thread) ObDereferenceObjectDeferDelete(Post->Thread);
    if (Post->Process) ObDereferenceObjectDeferDelete(Post->Process);
    ExFreePoolWithTag(Post, TAG_CM_NOTIFY);
}

static
VOID
NTAPI
CmpPostApcKernelRoutine(IN PKAPC Apc,
                        IN OUT PKNORMAL_ROUTINE *NormalRoutine,
                        IN OUT PVOID *NormalContext,
                        IN OUT PVOID *SystemArgument1,
                        IN OUT PVOID *SystemArgument2)
{
    /* The APC owns a reference on its thread */
    ObDereferenceObject(CONTAINING_RECORD(Apc->Thread, ETHREAD, Tcb));
    ExFreePoolWithTag(Apc, TAG_CM_NOTIFY);
}

static
VOID
NTAPI
CmpPostApcRundownRoutine(IN PKAPC Apc)
{
    ObDereferenceObject(CONTAINING_RECORD(Apc->Thread, ETHREAD, Tcb));
    ExFreePoolWithTag(Apc, TAG_CM_NOTIFY);
}

/* Complete a post that has been removed from its notify block */
static
VOID
CmpCompletePost(IN PCM_POST_BLOCK Post)
{
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;
    PVOID UserIosb;

    ASSERT(Post->Completed);

    if (Post->Synchronous)
    {
        /* The waiting thread fills its own status block */
        if (Post->UserEvent) KeSetEvent(Post->UserEvent, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Post->SyncEvent, IO_NO_INCREMENT, FALSE);
        CmpDereferencePost(Post);
        return;
    }

    /* Write the status block, in the context of the process it belongs to */
    UserIosb = Post->IoStatusBlock32 ? Post->IoStatusBlock32 : Post->IoStatusBlock;
    if (UserIosb)
    {
        if (Post->Process && Post->Process != PsGetCurrentProcess())
        {
            KeStackAttachProcess(&Post->Process->Pcb, &ApcState);
            Attached = TRUE;
        }

        _SEH2_TRY
        {
#if defined(_WIN64) && defined(BUILD_WOW64_ENABLED)
            if (Post->IoStatusBlock32)
            {
                ((PIO_STATUS_BLOCK32)Post->IoStatusBlock32)->Status = Post->Status;
                ((PIO_STATUS_BLOCK32)Post->IoStatusBlock32)->Information = 0;
            }
            else
#endif
            {
                Post->IoStatusBlock->Status = Post->Status;
                Post->IoStatusBlock->Information = 0;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* The caller's status block went away, ignore */
        }
        _SEH2_END;

        if (Attached) KeUnstackDetachProcess(&ApcState);
    }

    /* Signal the event */
    if (Post->UserEvent) KeSetEvent(Post->UserEvent, IO_NO_INCREMENT, FALSE);

    if (Post->Apc)
    {
        /* Queue the user APC; it owns itself and its thread reference now */
        if (KeInsertQueueApc(Post->Apc, UserIosb, NULL, IO_NO_INCREMENT))
        {
            Post->Apc = NULL;
            Post->Thread = NULL;
        }
    }
    else if (Post->Mode == KernelMode && Post->ApcRoutine)
    {
        /* Kernel mode callers pass a work item and its queue type */
        ExQueueWorkItem((PWORK_QUEUE_ITEM)Post->ApcRoutine,
                        (WORK_QUEUE_TYPE)(ULONG_PTR)Post->ApcContext);
    }

    CmpDereferencePost(Post);
}

static
VOID
CmpCompletePostList(IN PLIST_ENTRY ListHead)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(ListHead))
    {
        Entry = RemoveHeadList(ListHead);
        CmpCompletePost(CONTAINING_RECORD(Entry, CM_POST_BLOCK, PostListEntry));
    }
}

/* Move all posts of a notify block to a completion list. Notify lock held. */
static
VOID
CmpTakePosts(IN PCM_NOTIFY_BLOCK NotifyBlock,
             IN PLIST_ENTRY CompleteList,
             IN NTSTATUS Status)
{
    PLIST_ENTRY Entry;
    PCM_POST_BLOCK Post;

    while (!IsListEmpty(&NotifyBlock->PostList))
    {
        Entry = RemoveHeadList(&NotifyBlock->PostList);
        Post = CONTAINING_RECORD(Entry, CM_POST_BLOCK, PostListEntry);
        Post->Status = Status;
        Post->Completed = TRUE;
        InsertTailList(CompleteList, Entry);
    }
}

static
BOOLEAN
CmpNotifyBlockMatches(IN PCM_NOTIFY_BLOCK NotifyBlock,
                      IN PCM_KEY_CONTROL_BLOCK Kcb)
{
    if (NotifyBlock->KeyControlBlock == Kcb) return TRUE;
    if (!NotifyBlock->WatchTree) return FALSE;

    /* A subtree watch also sees the changes of every descendant */
    for (Kcb = Kcb->ParentKcb; Kcb; Kcb = Kcb->ParentKcb)
    {
        if (NotifyBlock->KeyControlBlock == Kcb) return TRUE;
    }

    return FALSE;
}

VOID
NTAPI
CmpReportNotify(IN PCM_KEY_CONTROL_BLOCK Kcb,
                IN PHHIVE Hive,
                IN HCELL_INDEX Cell,
                IN ULONG Filter)
{
    LIST_ENTRY CompleteList;
    PLIST_ENTRY Entry;
    PCM_NOTIFY_BLOCK NotifyBlock;

    UNREFERENCED_PARAMETER(Hive);
    UNREFERENCED_PARAMETER(Cell);

    /*
     * Name changes are reported with the KCB of the key created or deleted,
     * which is a change of its parent's subkey list.
     */
    if (Filter & REG_NOTIFY_CHANGE_NAME)
    {
        Kcb = Kcb->ParentKcb;
        if (!Kcb) return;
    }

    InitializeListHead(&CompleteList);

    KeAcquireGuardedMutex(&CmpNotifyLock);
    for (Entry = CmpNotifyListHead.Flink;
         Entry != &CmpNotifyListHead;
         Entry = Entry->Flink)
    {
        NotifyBlock = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);

        if (!(NotifyBlock->Filter & Filter)) continue;
        if (!CmpNotifyBlockMatches(NotifyBlock, Kcb)) continue;

        if (IsListEmpty(&NotifyBlock->PostList))
        {
            /* Nobody is waiting, remember it for the next request */
            NotifyBlock->NotifyPending = TRUE;
        }
        else
        {
            CmpTakePosts(NotifyBlock, &CompleteList, STATUS_NOTIFY_ENUM_DIR);
        }
    }
    KeReleaseGuardedMutex(&CmpNotifyLock);

    CmpCompletePostList(&CompleteList);
}

VOID
NTAPI
CmpFlushNotifyWithStatus(IN PCM_KEY_BODY KeyBody,
                         IN NTSTATUS Status)
{
    LIST_ENTRY CompleteList;
    PCM_NOTIFY_BLOCK NotifyBlock;

    InitializeListHead(&CompleteList);

    KeAcquireGuardedMutex(&CmpNotifyLock);
    NotifyBlock = KeyBody->NotifyBlock;
    if (NotifyBlock)
    {
        KeyBody->NotifyBlock = NULL;
        RemoveEntryList(&NotifyBlock->HiveList);
        CmpTakePosts(NotifyBlock, &CompleteList, Status);
    }
    KeReleaseGuardedMutex(&CmpNotifyLock);

    CmpCompletePostList(&CompleteList);
    if (NotifyBlock) ExFreePoolWithTag(NotifyBlock, TAG_CM_NOTIFY);
}

VOID
NTAPI
CmpFlushNotify(IN PCM_KEY_BODY KeyBody,
               IN BOOLEAN LockHeld)
{
    UNREFERENCED_PARAMETER(LockHeld);

    /* The key is being deleted (or its hive unloaded) */
    CmpFlushNotifyWithStatus(KeyBody, STATUS_KEY_DELETED);
}

/*
 * Arm one notification on KeyBody. Consumes the caller's references on
 * KeyBody and Event.
 */
NTSTATUS
NTAPI
CmpNotifyChangeKey(IN PCM_KEY_BODY KeyBody,
                   IN PKEVENT Event OPTIONAL,
                   IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
                   IN PVOID ApcContext OPTIONAL,
                   IN PIO_STATUS_BLOCK IoStatusBlock,
                   IN ULONG CompletionFilter,
                   IN BOOLEAN WatchTree,
                   IN BOOLEAN Asynchronous,
                   IN KPROCESSOR_MODE PreviousMode)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PCM_NOTIFY_BLOCK NotifyBlock, NewBlock;
    PCM_POST_BLOCK Post;
    BOOLEAN CompleteNow = FALSE;
    NTSTATUS Status;
    PAGED_CODE();

    /* Allocate the post and a notify block, in case the key has none yet */
    Post = ExAllocatePoolZero(NonPagedPool, sizeof(*Post), TAG_CM_NOTIFY);
    NewBlock = ExAllocatePoolZero(PagedPool, sizeof(*NewBlock), TAG_CM_NOTIFY);
    if (!Post || !NewBlock)
    {
        if (Post) ExFreePoolWithTag(Post, TAG_CM_NOTIFY);
        if (NewBlock) ExFreePoolWithTag(NewBlock, TAG_CM_NOTIFY);
        if (Event) ObDereferenceObject(Event);
        ObDereferenceObject(KeyBody);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* One reference for the notify block, one for a synchronous waiter */
    Post->RefCount = Asynchronous ? 1 : 2;
    Post->Synchronous = !Asynchronous;
    Post->Mode = PreviousMode;
    Post->UserEvent = Event;
    Post->IoStatusBlock = IoStatusBlock;
    Post->ApcRoutine = ApcRoutine;
    Post->ApcContext = ApcContext;
    KeInitializeEvent(&Post->SyncEvent, NotificationEvent, FALSE);

    if (PreviousMode != KernelMode)
    {
        Post->Process = PsGetCurrentProcess();
        ObReferenceObject(Post->Process);

        if (Asynchronous && ApcRoutine)
        {
            Post->Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), TAG_CM_NOTIFY);
            if (!Post->Apc)
            {
                ExFreePoolWithTag(NewBlock, TAG_CM_NOTIFY);
                Post->RefCount = 1;
                CmpDereferencePost(Post);
                ObDereferenceObject(KeyBody);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Post->Thread = PsGetCurrentThread();
            ObReferenceObject(Post->Thread);
            KeInitializeApc(Post->Apc,
                            &Post->Thread->Tcb,
                            OriginalApcEnvironment,
                            CmpPostApcKernelRoutine,
                            CmpPostApcRundownRoutine,
                            (PKNORMAL_ROUTINE)ApcRoutine,
                            UserMode,
                            ApcContext);
        }

#if defined(_WIN64) && defined(BUILD_WOW64_ENABLED)
        /*
         * WoW64 passes a 64-bit status block on its own stack, pointing to
         * the program's 32-bit one. That block is gone when an asynchronous
         * request completes, so complete into the 32-bit one instead.
         */
        if (Asynchronous && IoIs32bitProcess(NULL))
        {
            _SEH2_TRY
            {
                if (IoStatusBlock->Information == CMP_WOW64_IOSB_NOT_WRITTEN &&
                    IoStatusBlock->Pointer &&
                    (ULONG_PTR)IoStatusBlock->Pointer <= MAXULONG)
                {
                    Post->IoStatusBlock32 = IoStatusBlock->Pointer;
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                /* Leave it alone */
            }
            _SEH2_END;
        }
#endif
    }

    /* Like an I/O request, reset the event until the request completes */
    if (Event) KeClearEvent(Event);

    /* Lock the registry and the KCB, a deleted key can't be watched */
    CmpLockRegistry();
    CmpAcquireKcbLockShared(Kcb);
    if (Kcb->Delete)
    {
        Status = STATUS_KEY_DELETED;
    }
    else
    {
        KeAcquireGuardedMutex(&CmpNotifyLock);

        NotifyBlock = KeyBody->NotifyBlock;
        if (!NotifyBlock)
        {
            /* First request on this key body, set up its notify block */
            NotifyBlock = NewBlock;
            NewBlock = NULL;
            InitializeListHead(&NotifyBlock->PostList);
            NotifyBlock->KeyControlBlock = Kcb;
            NotifyBlock->KeyBody = KeyBody;
            InsertTailList(&CmpNotifyListHead, &NotifyBlock->HiveList);
            KeyBody->NotifyBlock = NotifyBlock;
        }

        NotifyBlock->Filter = CompletionFilter;
        NotifyBlock->WatchTree = WatchTree;

        if (NotifyBlock->NotifyPending)
        {
            /* Something changed since the last request completed */
            NotifyBlock->NotifyPending = FALSE;
            Post->Status = STATUS_NOTIFY_ENUM_DIR;
            Post->Completed = TRUE;
            CompleteNow = TRUE;
        }
        else
        {
            InsertTailList(&NotifyBlock->PostList, &Post->PostListEntry);
        }

        KeReleaseGuardedMutex(&CmpNotifyLock);
        Status = STATUS_PENDING;
    }
    CmpReleaseKcbLock(Kcb);
    CmpUnlockRegistry();

    if (NewBlock) ExFreePoolWithTag(NewBlock, TAG_CM_NOTIFY);

    /*
     * The key body can go now: closing it completes the request, the
     * notify block doesn't reference it.
     */
    ObDereferenceObject(KeyBody);

    if (Status != STATUS_PENDING)
    {
        /* Not armed */
        Post->RefCount = 1;
        CmpDereferencePost(Post);
        return Status;
    }

    if (CompleteNow) CmpCompletePost(Post);

    /* An asynchronous request completes later on */
    if (Asynchronous) return STATUS_PENDING;

    /* Wait for the change */
    Status = KeWaitForSingleObject(&Post->SyncEvent,
                                   Executive,
                                   PreviousMode,
                                   TRUE,
                                   NULL);

    KeAcquireGuardedMutex(&CmpNotifyLock);
    if (Post->Completed)
    {
        /* Completed, maybe while we were woken up for an APC */
        Status = Post->Status;
    }
    else
    {
        /* Woken up for an APC or an alert: cancel the request */
        RemoveEntryList(&Post->PostListEntry);
        Post->Completed = TRUE;
        InterlockedDecrement(&Post->RefCount);
    }
    KeReleaseGuardedMutex(&CmpNotifyLock);

    if (Status != STATUS_USER_APC && Status != STATUS_ALERTED)
    {
        _SEH2_TRY
        {
            IoStatusBlock->Status = Status;
            IoStatusBlock->Information = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Nothing to do */
        }
        _SEH2_END;
    }

    CmpDereferencePost(Post);
    return Status;
}
