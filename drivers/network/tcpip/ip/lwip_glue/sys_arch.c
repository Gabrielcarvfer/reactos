#include <debug.h>
#include <lwip/sys.h>
#include <lwip/tcpip.h>

#include "lwip_glue.h"

static LIST_ENTRY ThreadListHead;
static KSPIN_LOCK ThreadListLock;

/* SYS_ARCH_PROTECT: short, non-blocking sections (pbuf reference counts) that
 * any thread may enter: a spin lock the owner may take again */
static KSPIN_LOCK ProtectLock;
static PKTHREAD ProtectOwner;
static ULONG ProtectDepth;

/* (ms) */
#define LWIP_MAX_TIMER_SLEEP 50

KEVENT TerminationEvent;
NPAGED_LOOKASIDE_LIST MessageLookasideList;
NPAGED_LOOKASIDE_LIST QueueEntryLookasideList;
static NPAGED_LOOKASIDE_LIST MboxContainerLookasideList;

static LARGE_INTEGER StartTime;

typedef struct _thread_t
{
    HANDLE Handle;
    void (* ThreadFunction)(void *arg);
    void *ThreadContext;
    LIST_ENTRY ListEntry;
} *thread_t;

/* Milliseconds since sys_init. The interrupt time, not the system time:
 * the system time jumps when the clock is set (at boot, from the RTC and the
 * time zone), and a jump backwards stopped all of lwIP's timers -
 * retransmissions, persist probes - for as long as the jump */
u32_t sys_now(void)
{
    LARGE_INTEGER CurrentTime;

    CurrentTime.QuadPart = KeQueryInterruptTime();

    return (CurrentTime.QuadPart - StartTime.QuadPart) / 10000;
}

void
sys_arch_protect(sys_prot_t *lev)
{
    KIRQL OldIrql;

    /* (at DISPATCH_LEVEL nothing else runs on this processor, so an owner
     * that is the current thread is this very code path, nested) */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    if (ProtectOwner != KeGetCurrentThread())
    {
        KeAcquireSpinLockAtDpcLevel(&ProtectLock);
        ProtectOwner = KeGetCurrentThread();
    }
    ProtectDepth++;
    *lev = OldIrql;
}

void
sys_arch_unprotect(sys_prot_t lev)
{
    ASSERT(ProtectOwner == KeGetCurrentThread() && ProtectDepth > 0);
    if (--ProtectDepth == 0)
    {
        ProtectOwner = NULL;
        KeReleaseSpinLockFromDpcLevel(&ProtectLock);
    }
    KeLowerIrql((KIRQL)lev);
}

err_t
sys_mutex_new(sys_mutex_t *mutex)
{
    KeInitializeEvent(&mutex->Event, SynchronizationEvent, TRUE);
    mutex->Owner = NULL;
    mutex->Recursion = 0;
    mutex->Valid = 1;
    return ERR_OK;
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (mutex->Owner == KeGetCurrentThread())
    {
        mutex->Recursion++;
        return;
    }

    /* No suspending (APCs) while holding the lock everybody needs */
    KeEnterCriticalRegion();
    KeWaitForSingleObject(&mutex->Event, Executive, KernelMode, FALSE, NULL);
    mutex->Owner = KeGetCurrentThread();
    mutex->Recursion = 1;
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
    LIST_ENTRY Completions;
    BOOLEAN Core = (mutex == &lock_tcpip_core);

    ASSERT(mutex->Owner == KeGetCurrentThread() && mutex->Recursion > 0);

    /* Requests that lwIP's callbacks finished while the core lock was held
     * complete now, in this thread, after the lock is free: their completion
     * routines (AFD) take their own locks and call back into tcpip. (Taken
     * while still owning the lock once: filling receive requests may take
     * it again) */
    InitializeListHead(&Completions);
    if (Core && mutex->Recursion == 1)
        LibTCPTakeDeferredCompletions(&Completions);

    if (--mutex->Recursion)
    {
        ASSERT(IsListEmpty(&Completions));
        return;
    }

    mutex->Owner = NULL;
    KeSetEvent(&mutex->Event, IO_NO_INCREMENT, FALSE);
    KeLeaveCriticalRegion();

    if (Core && !IsListEmpty(&Completions))
        LibTCPRunDeferredCompletions(&Completions);
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
    mutex->Valid = 0;
}

int
sys_mutex_valid(sys_mutex_t *mutex)
{
    return mutex->Valid;
}

void
sys_mutex_set_invalid(sys_mutex_t *mutex)
{
    mutex->Valid = 0;
}

int
sys_core_locked(void)
{
    /* (lwip_init runs before the lock exists, in the only thread using lwIP) */
    return !lock_tcpip_core.Valid || lock_tcpip_core.Owner == KeGetCurrentThread();
}

err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
    ASSERT(count == 0 || count == 1);

    /* It seems lwIP uses the semaphore implementation as either a completion event or a lock
     * so I optimize for this case by using a synchronization event and setting its initial state
     * to signalled for a lock and non-signalled for a completion event */

    KeInitializeEvent(&sem->Event, SynchronizationEvent, count);

    sem->Valid = 1;

    return ERR_OK;
}

int sys_sem_valid(sys_sem_t *sem)
{
    return sem->Valid;
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
    sem->Valid = 0;
}

void
sys_sem_free(sys_sem_t* sem)
{
    /* No op (allocated in stack) */

    sys_sem_set_invalid(sem);
}

void
sys_sem_signal(sys_sem_t* sem)
{
    KeSetEvent(&sem->Event, IO_NO_INCREMENT, FALSE);
}

u32_t
sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout)
{
    LARGE_INTEGER LargeTimeout, PreWaitTime, PostWaitTime;
    UINT64 TimeDiff;
    NTSTATUS Status;
    PVOID WaitObjects[] = {&sem->Event, &TerminationEvent};

    LargeTimeout.QuadPart = Int32x32To64(timeout, -10000);

    PreWaitTime.QuadPart = KeQueryInterruptTime();

    Status = KeWaitForMultipleObjects(2,
                                      WaitObjects,
                                      WaitAny,
                                      Executive,
                                      KernelMode,
                                      FALSE,
                                      timeout != 0 ? &LargeTimeout : NULL,
                                      NULL);
    if (Status == STATUS_WAIT_0)
    {
        PostWaitTime.QuadPart = KeQueryInterruptTime();
        TimeDiff = PostWaitTime.QuadPart - PreWaitTime.QuadPart;
        TimeDiff /= 10000;

        return TimeDiff;
    }
    else if (Status == STATUS_WAIT_1)
    {
        /* DON'T remove ourselves from the thread list! */
        PsTerminateSystemThread(STATUS_SUCCESS);

        /* We should never get here! */
        ASSERT(FALSE);

        return 0;
    }

    return SYS_ARCH_TIMEOUT;
}

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
    KeInitializeSpinLock(&mbox->Lock);

    InitializeListHead(&mbox->ListHead);

    KeInitializeSemaphore(&mbox->Semaphore, 0, MAXLONG);

    mbox->Valid = 1;

    return ERR_OK;
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
    return mbox->Valid;
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
    mbox->Valid = 0;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
    ASSERT(IsListEmpty(&mbox->ListHead));

    sys_mbox_set_invalid(mbox);
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    PLWIP_MESSAGE_CONTAINER Container;

    Container = ExAllocateFromNPagedLookasideList(&MboxContainerLookasideList);
    ASSERT(Container);

    Container->Message = msg;

    ExInterlockedInsertTailList(&mbox->ListHead,
                                &Container->ListEntry,
                                &mbox->Lock);

    KeReleaseSemaphore(&mbox->Semaphore, IO_NO_INCREMENT, 1, FALSE);
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    LARGE_INTEGER LargeTimeout, PreWaitTime, PostWaitTime;
    UINT64 TimeDiff;
    NTSTATUS Status;
    PVOID Message;
    PLWIP_MESSAGE_CONTAINER Container;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    PVOID WaitObjects[] = {&mbox->Semaphore, &TerminationEvent};

    /* The lwIP thread sleeps here until its next timer is due. Other threads
     * run lwIP under the core lock now and start timers (TCP's retransmission
     * timer, tcp_timer_needed) without waking it: never sleep longer than
     * LWIP_MAX_TIMER_SLEEP, or those timers would wait for the next message,
     * and lost segments would never be sent again */
    if (timeout == 0 || timeout > LWIP_MAX_TIMER_SLEEP)
        timeout = LWIP_MAX_TIMER_SLEEP;

    LargeTimeout.QuadPart = Int32x32To64(timeout, -10000);

    PreWaitTime.QuadPart = KeQueryInterruptTime();

    Status = KeWaitForMultipleObjects(2,
                                      WaitObjects,
                                      WaitAny,
                                      Executive,
                                      KernelMode,
                                      FALSE,
                                      &LargeTimeout,
                                      NULL);

    if (Status == STATUS_WAIT_0)
    {
        KeAcquireSpinLock(&mbox->Lock, &OldIrql);
        ASSERT(!IsListEmpty(&mbox->ListHead));
        Entry = RemoveHeadList(&mbox->ListHead);
        KeReleaseSpinLock(&mbox->Lock, OldIrql);

        Container = CONTAINING_RECORD(Entry, LWIP_MESSAGE_CONTAINER, ListEntry);
        Message = Container->Message;
        ExFreeToNPagedLookasideList(&MboxContainerLookasideList, Container);

        if (msg)
            *msg = Message;

        PostWaitTime.QuadPart = KeQueryInterruptTime();
        TimeDiff = PostWaitTime.QuadPart - PreWaitTime.QuadPart;
        TimeDiff /= 10000;

        return TimeDiff;
    }
    else if (Status == STATUS_WAIT_1)
    {
        /* DON'T remove ourselves from the thread list! */
        PsTerminateSystemThread(STATUS_SUCCESS);

        /* We should never get here! */
        ASSERT(FALSE);

        return 0;
    }

    return SYS_ARCH_TIMEOUT;
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    if (sys_arch_mbox_fetch(mbox, msg, 1) != SYS_ARCH_TIMEOUT)
        return 0;
    else
        return SYS_MBOX_EMPTY;
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    sys_mbox_post(mbox, msg);

    return ERR_OK;
}

VOID
NTAPI
LwipThreadMain(PVOID Context)
{
    thread_t Container = (thread_t)Context;
    KIRQL OldIrql;

    ExInterlockedInsertHeadList(&ThreadListHead, &Container->ListEntry, &ThreadListLock);

    Container->ThreadFunction(Container->ThreadContext);

    KeAcquireSpinLock(&ThreadListLock, &OldIrql);
    RemoveEntryList(&Container->ListEntry);
    KeReleaseSpinLock(&ThreadListLock, OldIrql);

    ExFreePool(Container);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
    thread_t Container;
    NTSTATUS Status;

    Container = ExAllocatePool(NonPagedPool, sizeof(*Container));
    if (!Container)
        return 0;

    Container->ThreadFunction = thread;
    Container->ThreadContext = arg;

    Status = PsCreateSystemThread(&Container->Handle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  LwipThreadMain,
                                  Container);

    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Container);
        return 0;
    }

    return 0;
}

void
sys_init(void)
{
    KeInitializeSpinLock(&ThreadListLock);
    InitializeListHead(&ThreadListHead);
    KeInitializeSpinLock(&ProtectLock);

    StartTime.QuadPart = KeQueryInterruptTime();

    KeInitializeEvent(&TerminationEvent, NotificationEvent, FALSE);

    ExInitializeNPagedLookasideList(&MessageLookasideList,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof(struct lwip_callback_msg),
                                    LWIP_MESSAGE_TAG,
                                    0);

    ExInitializeNPagedLookasideList(&QueueEntryLookasideList,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof(QUEUE_ENTRY),
                                    LWIP_QUEUE_TAG,
                                    0);

    ExInitializeNPagedLookasideList(&MboxContainerLookasideList,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof(LWIP_MESSAGE_CONTAINER),
                                    LWIP_MESSAGE_TAG,
                                    0);
}

void
sys_shutdown(void)
{
    PLIST_ENTRY CurrentEntry;
    thread_t Container;

    /* Set the termination event */
    KeSetEvent(&TerminationEvent, IO_NO_INCREMENT, FALSE);

    /* Loop through the thread list and wait for each to die */
    while ((CurrentEntry = ExInterlockedRemoveHeadList(&ThreadListHead, &ThreadListLock)))
    {
        Container = CONTAINING_RECORD(CurrentEntry, struct _thread_t, ListEntry);

        if (Container->ThreadFunction)
        {
            KeWaitForSingleObject(Container->Handle,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);

            ZwClose(Container->Handle);
        }
    }

    ExDeleteNPagedLookasideList(&MessageLookasideList);
    ExDeleteNPagedLookasideList(&QueueEntryLookasideList);
    ExDeleteNPagedLookasideList(&MboxContainerLookasideList);
}
