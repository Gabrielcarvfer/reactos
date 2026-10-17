/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite MmSecureVirtualMemory test
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 *
 * A driver secures user memory to read or write it without copying it first:
 * as long as it is secured, the process can't free, decommit or protect it
 * out of the driver's reach.
 */

#include <kmt_test.h>

static PVOID
AllocUser(SIZE_T Size, ULONG Type)
{
    PVOID Base = NULL;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size, Type, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Base : NULL;
}

static NTSTATUS
FreeUser(PVOID Base, SIZE_T Size, ULONG Type)
{
    return ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, Type);
}

/* (ZwProtectVirtualMemory isn't exported by every kernel) */
typedef NTSTATUS (NTAPI *PFN_ZW_PROTECT)(HANDLE, PVOID *, PSIZE_T, ULONG, PULONG);
static PFN_ZW_PROTECT pZwProtectVirtualMemory;

static NTSTATUS
ProtectUser(PVOID Base, SIZE_T Size, ULONG Protect)
{
    ULONG OldProtect;

    if (!pZwProtectVirtualMemory)
        return STATUS_NOT_IMPLEMENTED;
    return pZwProtectVirtualMemory(ZwCurrentProcess(), &Base, &Size, Protect, &OldProtect);
}

/* Expected status of a protection change: skipped without ZwProtectVirtualMemory */
#define ok_protect(Call, Expected) \
    do { \
        NTSTATUS _st = (Call); \
        if (pZwProtectVirtualMemory) ok_eq_hex(_st, (Expected)); \
    } while (0)

static
VOID
TestReadWrite(VOID)
{
    PUCHAR Base;
    HANDLE hSecure;
    NTSTATUS Status;

    Base = AllocUser(4 * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT);
    if (!Base)
        return;

    /* Pages 1 and 2 (not page aligned) */
    hSecure = MmSecureVirtualMemory(Base + PAGE_SIZE + 16, PAGE_SIZE, PAGE_READWRITE);
    ok(hSecure != NULL, "MmSecureVirtualMemory failed\n");
    if (!hSecure)
    {
        FreeUser(Base, 0, MEM_RELEASE);
        return;
    }

    /* Usable */
    Base[PAGE_SIZE + 16] = 0x5a;
    ok_eq_uint(Base[PAGE_SIZE + 16], 0x5a);

    /* Can't be freed, decommitted or made read-only / inaccessible */
    Status = FreeUser(Base, 0, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_INVALID_PAGE_PROTECTION);
    Status = FreeUser(Base + 2 * PAGE_SIZE, PAGE_SIZE, MEM_DECOMMIT);
    ok_eq_hex(Status, STATUS_INVALID_PAGE_PROTECTION);
    ok_protect(ProtectUser(Base + PAGE_SIZE, PAGE_SIZE, PAGE_NOACCESS), STATUS_INVALID_PAGE_PROTECTION);
    ok_protect(ProtectUser(Base + PAGE_SIZE, PAGE_SIZE, PAGE_READONLY), STATUS_INVALID_PAGE_PROTECTION);
    ok_protect(ProtectUser(Base + PAGE_SIZE, PAGE_SIZE, PAGE_READWRITE | PAGE_GUARD), STATUS_INVALID_PAGE_PROTECTION);

    /* Pages outside of the range are not affected */
    Status = FreeUser(Base + 3 * PAGE_SIZE, PAGE_SIZE, MEM_DECOMMIT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_protect(ProtectUser(Base, PAGE_SIZE, PAGE_NOACCESS), STATUS_SUCCESS);

    /* Still usable */
    Base[2 * PAGE_SIZE + 15] = 0xa5;
    ok_eq_uint(Base[2 * PAGE_SIZE + 15], 0xa5);

    MmUnsecureVirtualMemory(hSecure);

    /* Now it can go */
    ok_protect(ProtectUser(Base + PAGE_SIZE, PAGE_SIZE, PAGE_READONLY), STATUS_SUCCESS);
    Status = FreeUser(Base, 0, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestReadOnly(VOID)
{
    PUCHAR Base;
    HANDLE hSecure;
    NTSTATUS Status;

    Base = AllocUser(2 * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT);
    if (!Base)
        return;

    hSecure = MmSecureVirtualMemory(Base, 2 * PAGE_SIZE, PAGE_READONLY);
    ok(hSecure != NULL, "MmSecureVirtualMemory failed\n");
    if (hSecure)
    {
        /* Read-only secured memory may become read-only, but not inaccessible */
        ok_protect(ProtectUser(Base, PAGE_SIZE, PAGE_READONLY), STATUS_SUCCESS);
        ok_protect(ProtectUser(Base, PAGE_SIZE, PAGE_NOACCESS), STATUS_INVALID_PAGE_PROTECTION);
        Status = FreeUser(Base, 0, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_INVALID_PAGE_PROTECTION);

        MmUnsecureVirtualMemory(hSecure);
    }

    Status = FreeUser(Base, 0, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* A read-only page can't be secured for writing */
    {
        SIZE_T Size = PAGE_SIZE;
        Base = NULL;
        Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), (PVOID *)&Base, 0, &Size,
                                         MEM_RESERVE | MEM_COMMIT, PAGE_READONLY);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            hSecure = MmSecureVirtualMemory(Base, PAGE_SIZE, PAGE_READWRITE);
            ok(hSecure == NULL, "Secured a read-only page for writing\n");
            if (hSecure) MmUnsecureVirtualMemory(hSecure);

            hSecure = MmSecureVirtualMemory(Base, PAGE_SIZE, PAGE_READONLY);
            ok(hSecure != NULL, "Could not secure a read-only page for reading\n");
            if (hSecure) MmUnsecureVirtualMemory(hSecure);

            Status = FreeUser(Base, 0, MEM_RELEASE);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
    }
}

static
VOID
TestInvalid(VOID)
{
    PUCHAR Base;
    HANDLE hSecure;

    /* Reserved, but not committed */
    Base = AllocUser(2 * PAGE_SIZE, MEM_RESERVE);
    if (Base)
    {
        hSecure = MmSecureVirtualMemory(Base, PAGE_SIZE, PAGE_READONLY);
        ok(hSecure == NULL, "Secured reserved memory\n");
        if (hSecure) MmUnsecureVirtualMemory(hSecure);
        FreeUser(Base, 0, MEM_RELEASE);
    }

    /* Partly committed */
    Base = AllocUser(2 * PAGE_SIZE, MEM_RESERVE);
    if (Base)
    {
        SIZE_T Size = PAGE_SIZE;
        PVOID Commit = Base;
        ok_eq_hex(ZwAllocateVirtualMemory(ZwCurrentProcess(), &Commit, 0, &Size, MEM_COMMIT, PAGE_READWRITE),
                  STATUS_SUCCESS);
        hSecure = MmSecureVirtualMemory(Base, 2 * PAGE_SIZE, PAGE_READONLY);
        ok(hSecure == NULL, "Secured partly committed memory\n");
        if (hSecure) MmUnsecureVirtualMemory(hSecure);
        FreeUser(Base, 0, MEM_RELEASE);
    }

    /* Kernel memory and an empty range */
    Base = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'tSmM');
    if (Base)
    {
        hSecure = MmSecureVirtualMemory(Base, PAGE_SIZE, PAGE_READONLY);
        ok(hSecure == NULL, "Secured kernel memory\n");
        if (hSecure) MmUnsecureVirtualMemory(hSecure);
        ExFreePoolWithTag(Base, 'tSmM');
    }

    Base = AllocUser(PAGE_SIZE, MEM_RESERVE | MEM_COMMIT);
    if (Base)
    {
        hSecure = MmSecureVirtualMemory(Base, 0, PAGE_READONLY);
        ok(hSecure == NULL, "Secured an empty range\n");
        if (hSecure) MmUnsecureVirtualMemory(hSecure);
        FreeUser(Base, 0, MEM_RELEASE);
    }
}

START_TEST(MmSecureVirtualMemory)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"ZwProtectVirtualMemory");

    PAGED_CODE();

    pZwProtectVirtualMemory = (PFN_ZW_PROTECT)MmGetSystemRoutineAddress(&Name);
    if (!pZwProtectVirtualMemory)
        skip(0, "ZwProtectVirtualMemory is not exported: protection changes untested\n");

    TestReadWrite();
    TestReadOnly();
    TestInvalid();
}
