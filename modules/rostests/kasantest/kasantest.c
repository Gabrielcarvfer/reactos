/*
 * PROJECT:     ReactOS Kernel AddressSanitizer test
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pool overflow, underflow and use after free, which KASAN
 *              (boot option KASAN; this driver in KASAN_MODULES) must report
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include <ntddk.h>

#define TAG 'tsaK'

static VOID
TestPool(POOL_TYPE PoolType, SIZE_T Size, PCSTR Name)
{
    volatile CHAR *p = ExAllocatePoolWithTag(PoolType, Size, TAG);
    CHAR Value;
    SIZE_T i;

    if (!p)
        return;
    DbgPrint("KASANTEST: %s block of %Iu bytes at %p\n", Name, Size, p);
    for (i = 0; i < Size; i++)
        p[i] = (CHAR)i;             /* fine */
    p[Size] = 1;                    /* overflow */
    Value = p[-1];                  /* underflow */
    ExFreePoolWithTag((PVOID)p, TAG);
    Value = p[3];                   /* use after free */
    (VOID)Value;
}

/* (the same for paged pool, in its own code: KASAN reports each place in
 * the code once) */
static VOID
TestPagedPool(SIZE_T Size)
{
    volatile CHAR *p = ExAllocatePoolWithTag(PagedPool, Size, TAG);
    CHAR Value;

    if (!p)
        return;
    DbgPrint("KASANTEST: paged block of %Iu bytes at %p\n", Size, p);
    p[Size] = 1;                    /* overflow */
    Value = p[-1];                  /* underflow */
    ExFreePoolWithTag((PVOID)p, TAG);
    Value = p[3];                   /* use after free */
    (VOID)Value;
}

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    ULONG i;

    DbgPrint("KASANTEST: start\n");
    TestPool(NonPagedPool, 10, "nonpaged");
    TestPagedPool(24);

    /* blocks of all sizes, used properly: no reports */
    for (i = 1; i < 1000; i++)
    {
        volatile UCHAR *p = ExAllocatePoolWithTag((i & 1) ? NonPagedPool : PagedPool, i, TAG);
        if (p)
        {
            p[0] = 1;
            p[i - 1] = 2;
            ExFreePoolWithTag((PVOID)p, TAG);
        }
    }
    DbgPrint("KASANTEST: done\n");

    /* Nothing to keep loaded */
    return STATUS_UNSUCCESSFUL;
}
