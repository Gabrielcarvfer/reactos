/*
 * PROJECT:     ReactOS Win32k subsystem
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tell display drivers what GDI drew (GCAPS2_SYNCFLUSH)
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * A display driver can let GDI draw into a copy of the screen in system
 * memory (a shadow surface, reads from video memory are uncached and very
 * slow) and copy what changed to the screen. It says so with
 * GCAPS2_SYNCFLUSH and HOOK_SYNCHRONIZE: GDI collects the rectangles drawn
 * on the surface and passes them to DrvSynchronizeSurface(DSS_FLUSH_EVENT)
 * shortly after, from a system thread (many small drawing calls make one
 * copy), at once when the mouse pointer moves, and on GdiFlush.
 */

#include <win32k.h>
#define NDEBUG
#include <debug.h>

/* How long drawing may wait before it is shown (100 ns units) */
#define SYNCFLUSH_DELAY (-50000LL)

#define SYNCFLUSH_MAX_PDEVS 8

static KSPIN_LOCK gFlushLock;
static KTIMER gFlushTimer;
static BOOLEAN gbFlushTimerSet;
static PPDEVOBJ gappdevFlush[SYNCFLUSH_MAX_PDEVS];
static LONG glFlushThread; /* 0: not started, 2: starting, 1: running, -1: failed */

static
BOOL
PDEVOBJ_bSyncFlush(
    _In_ PPDEVOBJ ppdev)
{
    return ppdev->pSurface &&
           (ppdev->pSurface->flags & HOOK_SYNCHRONIZE) &&
           (ppdev->devinfo.flGraphicsCaps2 & GCAPS2_SYNCFLUSH) &&
           (ppdev->pfn.SynchronizeSurface || ppdev->pfn.Synchronize);
}

static
LONGLONG
RectArea(
    _In_ const RECTL *prcl)
{
    return (LONGLONG)(prcl->right - prcl->left) * (prcl->bottom - prcl->top);
}

static
VOID
RectUnion(
    _Inout_ RECTL *prclDst,
    _In_ const RECTL *prcl)
{
    prclDst->left = min(prclDst->left, prcl->left);
    prclDst->top = min(prclDst->top, prcl->top);
    prclDst->right = max(prclDst->right, prcl->right);
    prclDst->bottom = max(prclDst->bottom, prcl->bottom);
}

/* Show what was drawn. Call with the device lock held. */
_Requires_lock_held_(*ppdev->hsemDevLock)
VOID
NTAPI
PDEVOBJ_vFlushDirty(
    _Inout_ PPDEVOBJ ppdev)
{
    ULONG i, cDirty = ppdev->cDirty;

    ppdev->cDirty = 0;

    if (!cDirty || !PDEVOBJ_bSyncFlush(ppdev))
        return;

    for (i = 0; i < cDirty; i++)
    {
        RECTL *prcl = &ppdev->arclDirty[i];

        /* The surface may have changed size since (mode change) */
        prcl->right = min(prcl->right, ppdev->pSurface->SurfObj.sizlBitmap.cx);
        prcl->bottom = min(prcl->bottom, ppdev->pSurface->SurfObj.sizlBitmap.cy);
        if (prcl->left >= prcl->right || prcl->top >= prcl->bottom)
            continue;

        if (ppdev->pfn.SynchronizeSurface)
            ppdev->pfn.SynchronizeSurface(&ppdev->pSurface->SurfObj, prcl, DSS_FLUSH_EVENT);
        else
            ppdev->pfn.Synchronize(ppdev->dhpdev, prcl);
    }
}

/* Flush every PDEV that waits for it */
VOID
NTAPI
GreFlushDisplays(VOID)
{
    PPDEVOBJ appdev[SYNCFLUSH_MAX_PDEVS];
    ULONG i, c = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&gFlushLock, &OldIrql);
    for (i = 0; i < SYNCFLUSH_MAX_PDEVS; i++)
    {
        if (gappdevFlush[i])
        {
            appdev[c++] = gappdevFlush[i];
            gappdevFlush[i] = NULL;
        }
    }
    KeReleaseSpinLock(&gFlushLock, OldIrql);

    for (i = 0; i < c; i++)
    {
        EngAcquireSemaphore(appdev[i]->hsemDevLock);
        appdev[i]->bFlushQueued = FALSE;
        PDEVOBJ_vFlushDirty(appdev[i]);
        EngReleaseSemaphore(appdev[i]->hsemDevLock);

        /* The queue held a reference */
        PDEVOBJ_vRelease(appdev[i]);
    }
}

static
VOID
NTAPI
SyncFlushThread(
    _In_ PVOID Context)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Context);

    /* Run before the threads that draw, like the raw input thread does */
    KeSetPriorityThread(&PsGetCurrentThread()->Tcb, LOW_REALTIME_PRIORITY + 2);

    for (;;)
    {
        KeWaitForSingleObject(&gFlushTimer, Executive, KernelMode, FALSE, NULL);

        KeAcquireSpinLock(&gFlushLock, &OldIrql);
        gbFlushTimerSet = FALSE;
        KeReleaseSpinLock(&gFlushLock, OldIrql);

        GreFlushDisplays();
    }
}

static
BOOL
SyncFlushStartThread(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE hThread;
    NTSTATUS Status;
    LONG lState = InterlockedCompareExchange(&glFlushThread, 2, 0);

    /* Running, failed, or another thread starts it (flush right away meanwhile) */
    if (lState != 0)
        return lState == 1;

    KeInitializeSpinLock(&gFlushLock);
    KeInitializeTimerEx(&gFlushTimer, SynchronizationTimer);

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&hThread,
                                  THREAD_ALL_ACCESS,
                                  &ObjectAttributes,
                                  NULL,
                                  NULL,
                                  SyncFlushThread,
                                  NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PsCreateSystemThread failed: 0x%lx\n", Status);
        InterlockedExchange(&glFlushThread, -1);
        return FALSE;
    }
    ZwClose(hThread);
    InterlockedExchange(&glFlushThread, 1);
    return TRUE;
}

/* Queue the PDEV for the flush thread. Call with the device lock held. */
static
BOOL
PDEVOBJ_bQueueFlush(
    _Inout_ PPDEVOBJ ppdev)
{
    KIRQL OldIrql;
    ULONG i;
    BOOL bQueued = FALSE;
    LARGE_INTEGER DueTime;

    if (!SyncFlushStartThread())
        return FALSE;

    PDEVOBJ_vReference(ppdev);

    KeAcquireSpinLock(&gFlushLock, &OldIrql);
    for (i = 0; i < SYNCFLUSH_MAX_PDEVS; i++)
    {
        if (!gappdevFlush[i])
        {
            gappdevFlush[i] = ppdev;
            bQueued = TRUE;
            break;
        }
    }
    if (bQueued && !gbFlushTimerSet)
    {
        DueTime.QuadPart = SYNCFLUSH_DELAY;
        KeSetTimer(&gFlushTimer, DueTime, NULL);
        gbFlushTimerSet = TRUE;
    }
    KeReleaseSpinLock(&gFlushLock, OldIrql);

    if (!bQueued)
        PDEVOBJ_vRelease(ppdev);

    return bQueued;
}

/*
 * GDI drew into *prcl (surface coordinates, may be unordered) of the PDEV's
 * surface. Call with the device lock held.
 */
_Requires_lock_held_(*ppdev->hsemDevLock)
VOID
NTAPI
PDEVOBJ_vAddDirtyRect(
    _Inout_ PPDEVOBJ ppdev,
    _In_ const RECTL *prcl,
    _In_ LONG lMargin)
{
    RECTL rcl;
    ULONG i, iBest = 0;
    LONGLONG cBestGrowth = MAXLONGLONG;

    if (!PDEVOBJ_bSyncFlush(ppdev))
        return;

    rcl.left = min(prcl->left, prcl->right) - lMargin;
    rcl.right = max(prcl->left, prcl->right) + lMargin;
    rcl.top = min(prcl->top, prcl->bottom) - lMargin;
    rcl.bottom = max(prcl->top, prcl->bottom) + lMargin;

    rcl.left = max(rcl.left, 0);
    rcl.top = max(rcl.top, 0);
    rcl.right = min(rcl.right, ppdev->pSurface->SurfObj.sizlBitmap.cx);
    rcl.bottom = min(rcl.bottom, ppdev->pSurface->SurfObj.sizlBitmap.cy);
    if (rcl.left >= rcl.right || rcl.top >= rcl.bottom)
        return;

    /* Merge with a rectangle when that doesn't copy much more */
    for (i = 0; i < ppdev->cDirty; i++)
    {
        RECTL rclUnion = ppdev->arclDirty[i];
        LONGLONG cGrowth;

        /* (negative when they overlap) */
        RectUnion(&rclUnion, &rcl);
        cGrowth = RectArea(&rclUnion) - RectArea(&ppdev->arclDirty[i]) - RectArea(&rcl);
        if (cGrowth <= 4096)
        {
            ppdev->arclDirty[i] = rclUnion;
            goto Queue;
        }
        if (cGrowth < cBestGrowth)
        {
            cBestGrowth = cGrowth;
            iBest = i;
        }
    }

    if (ppdev->cDirty < RTL_NUMBER_OF(ppdev->arclDirty))
        ppdev->arclDirty[ppdev->cDirty++] = rcl;
    else
        RectUnion(&ppdev->arclDirty[iBest], &rcl);

Queue:
    if (!ppdev->bFlushQueued)
    {
        if (PDEVOBJ_bQueueFlush(ppdev))
            ppdev->bFlushQueued = TRUE;
        else
            PDEVOBJ_vFlushDirty(ppdev);
    }
}

/* EOF */
