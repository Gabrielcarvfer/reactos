/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for LockFileEx with SQLite's byte-range lock pattern
 *
 * SQLite locks PENDING (0x40000000), RESERVED (0x40000001) and the SHARED
 * range (0x40000002, 510 bytes). Chromium's profile databases failed with a
 * lock conflict on RESERVED. Locks must be released by UnlockFileEx and when
 * the handle is closed, and conflict only across handles.
 */

#include "precomp.h"

#define PENDING_BYTE  0x40000000
#define RESERVED_BYTE 0x40000001
#define SHARED_FIRST  0x40000002
#define SHARED_SIZE   510

#define EX (LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)
#define SH LOCKFILE_FAIL_IMMEDIATELY

static
BOOL
Lock(HANDLE File, DWORD Flags, DWORD Offset, DWORD Length)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = Offset;
    return LockFileEx(File, Flags, 0, Length, 0, &Overlapped);
}

static
BOOL
Unlock(HANDLE File, DWORD Offset, DWORD Length)
{
    OVERLAPPED Overlapped;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = Offset;
    return UnlockFileEx(File, 0, Length, 0, &Overlapped);
}

static
HANDLE
Open(PCWSTR Path, DWORD Disposition)
{
    return CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, Disposition, 0, NULL);
}

START_TEST(LockFileEx)
{
    WCHAR TempPath[MAX_PATH], Path[MAX_PATH];
    HANDLE A, B;

    GetTempPathW(_countof(TempPath), TempPath);
    StringCchPrintfW(Path, _countof(Path), L"%lslockfileex_%lu.db", TempPath, GetCurrentProcessId());

    A = Open(Path, CREATE_ALWAYS);
    ok(A != INVALID_HANDLE_VALUE, "Creating %ls failed with %lu\n", Path, GetLastError());
    if (A == INVALID_HANDLE_VALUE)
        return;
    B = Open(Path, OPEN_EXISTING);
    ok(B != INVALID_HANDLE_VALUE, "Opening %ls again failed with %lu\n", Path, GetLastError());
    if (B == INVALID_HANDLE_VALUE)
    {
        CloseHandle(A);
        DeleteFileW(Path);
        return;
    }

    /* SQLite gets SHARED: PENDING exclusive, SHARED range shared, release PENDING */
    ok(Lock(A, EX, PENDING_BYTE, 1), "A: PENDING failed with %lu\n", GetLastError());
    ok(Lock(A, SH, SHARED_FIRST, SHARED_SIZE), "A: SHARED range failed with %lu\n", GetLastError());
    ok(Unlock(A, PENDING_BYTE, 1), "A: releasing PENDING failed with %lu\n", GetLastError());
    ok(Lock(B, EX, PENDING_BYTE, 1), "B: PENDING failed with %lu\n", GetLastError());
    ok(Lock(B, SH, SHARED_FIRST, SHARED_SIZE), "B: SHARED range (shared + shared) failed with %lu\n", GetLastError());
    ok(Unlock(B, PENDING_BYTE, 1), "B: releasing PENDING failed with %lu\n", GetLastError());

    /* RESERVED by A: B conflicts; once A unlocks, B gets it */
    ok(Lock(A, EX, RESERVED_BYTE, 1), "A: RESERVED failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!Lock(B, EX, RESERVED_BYTE, 1), "B: RESERVED succeeded while A holds it\n");
    ok(GetLastError() == ERROR_LOCK_VIOLATION, "Error %lu, expected ERROR_LOCK_VIOLATION\n", GetLastError());
    ok(Unlock(A, RESERVED_BYTE, 1), "A: releasing RESERVED failed with %lu\n", GetLastError());
    ok(Lock(B, EX, RESERVED_BYTE, 1), "B: RESERVED after A released it failed with %lu\n", GetLastError());
    ok(Unlock(B, RESERVED_BYTE, 1), "B: releasing RESERVED failed with %lu\n", GetLastError());

    /* A lock held at close goes away with the handle */
    ok(Lock(A, EX, RESERVED_BYTE, 1), "A: RESERVED again failed with %lu\n", GetLastError());
    CloseHandle(A);
    ok(Lock(B, EX, RESERVED_BYTE, 1), "B: RESERVED after A's handle was closed failed with %lu\n", GetLastError());
    ok(Unlock(B, RESERVED_BYTE, 1), "B: releasing RESERVED failed with %lu\n", GetLastError());
    ok(Unlock(B, SHARED_FIRST, SHARED_SIZE), "B: releasing the SHARED range failed with %lu\n", GetLastError());
    CloseHandle(B);

    /* A new handle after both were closed: everything is free */
    A = Open(Path, OPEN_EXISTING);
    ok(A != INVALID_HANDLE_VALUE, "Opening %ls failed with %lu\n", Path, GetLastError());
    if (A != INVALID_HANDLE_VALUE)
    {
        ok(Lock(A, EX, SHARED_FIRST, SHARED_SIZE),
           "New handle: exclusive SHARED range failed with %lu (old locks remain)\n", GetLastError());
        ok(Lock(A, EX, RESERVED_BYTE, 1), "New handle: RESERVED failed with %lu\n", GetLastError());
        CloseHandle(A);
    }
    ok(DeleteFileW(Path), "Deleting %ls failed with %lu\n", Path, GetLastError());
}
