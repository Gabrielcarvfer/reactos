/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test that reads and writes through another handle respect byte range locks
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

#include "precomp.h"

/*
 * ReactOS's FsRtl never set FILE_LOCK::FastIoIsQuestionable, so file systems
 * (FastFAT: FatIsFastIoPossible) kept allowing fast I/O while byte range locks
 * were held, and a cached read or write through another handle went through
 * FsRtlCopyRead/FsRtlCopyWrite without checking them: it ignored the lock.
 * (Found by KCSAN, a race on FCB_HEADER::IsFastIoPossible.) The second access
 * below is the cached one, which takes the fast path.
 */

static void TestAccess(HANDLE File, LONG Offset, BOOL Write, BOOL ExpectDenied, int Line)
{
    CHAR Buffer[512] = "written through the second handle";
    DWORD Done = 0;
    BOOL Ret;

    SetFilePointer(File, Offset, NULL, FILE_BEGIN);
    SetLastError(0xdeadbeef);
    Ret = Write ? WriteFile(File, Buffer, sizeof(Buffer), &Done, NULL)
                : ReadFile(File, Buffer, sizeof(Buffer), &Done, NULL);
    if (ExpectDenied)
    {
        ok_(__FILE__, Line)(!Ret, "%s at %ld succeeded (%lu bytes) despite the lock\n",
                            Write ? "WriteFile" : "ReadFile", Offset, Done);
        ok_(__FILE__, Line)(GetLastError() == ERROR_LOCK_VIOLATION, "%s error %lu\n",
                            Write ? "WriteFile" : "ReadFile", GetLastError());
    }
    else
    {
        ok_(__FILE__, Line)(Ret && Done == sizeof(Buffer), "%s at %ld failed: %lu\n",
                            Write ? "WriteFile" : "ReadFile", Offset, GetLastError());
    }
}

START_TEST(LockedReadWrite)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH];
    CHAR Data[8192];
    HANDLE A, B;
    DWORD Done;
    OVERLAPPED Ov = { 0 };
    int i;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    GetTempFileNameW(Dir, L"lrw", 0, Path);
    A = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(A != INVALID_HANDLE_VALUE, "CreateFile A failed: %lu\n", GetLastError());
    if (A == INVALID_HANDLE_VALUE)
        return;
    memset(Data, 'x', sizeof(Data));
    ok(WriteFile(A, Data, sizeof(Data), &Done, NULL), "WriteFile failed: %lu\n", GetLastError());
    FlushFileBuffers(A);

    B = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(B != INVALID_HANDLE_VALUE, "CreateFile B failed: %lu\n", GetLastError());
    if (B == INVALID_HANDLE_VALUE)
    {
        CloseHandle(A);
        DeleteFileW(Path);
        return;
    }

    /* Read through B first so the data is cached and fast I/O is possible */
    TestAccess(B, 0, FALSE, FALSE, __LINE__);
    TestAccess(B, 0, FALSE, FALSE, __LINE__);

    /* Lock bytes 0..4095 exclusively through A */
    Ov.Offset = 0;
    ok(LockFileEx(A, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 4096, 0, &Ov),
       "LockFileEx failed: %lu\n", GetLastError());

    for (i = 0; i < 2; i++)
    {
        TestAccess(B, 0, FALSE, TRUE, __LINE__);
        TestAccess(B, 1024, TRUE, TRUE, __LINE__);
    }
    /* Outside the locked range B still reads and writes */
    TestAccess(B, 4096, FALSE, FALSE, __LINE__);
    TestAccess(B, 6144, TRUE, FALSE, __LINE__);

    ok(UnlockFileEx(A, 0, 4096, 0, &Ov), "UnlockFileEx failed: %lu\n", GetLastError());
    TestAccess(B, 0, FALSE, FALSE, __LINE__);
    TestAccess(B, 1024, TRUE, FALSE, __LINE__);

    CloseHandle(B);
    CloseHandle(A);
    DeleteFileW(Path);
}
