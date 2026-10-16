/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for ReadDirectoryChangesW on an I/O completion port
 *
 * Chromium's FilePathWatcher (Spotify, VS Code) puts a directory handle on a
 * completion port and issues ReadDirectoryChangesW with an OVERLAPPED and no
 * event, often from a pool thread that exits afterwards. Every completion
 * must carry its OVERLAPPED and key (Chromium CHECKs this), and a failed one
 * must be ERROR_ACCESS_DENIED.
 */

#include "precomp.h"

#define KEY_DIR 7
#define KEY_DIR2 20

static HANDLE Port;
static HANDLE Dir2;
static OVERLAPPED Overlapped2;
static DWORD Buffer2[1024];

static
HANDLE
OpenDirectory(PCWSTR Path)
{
    return CreateFileW(Path,
                       FILE_LIST_DIRECTORY,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                       NULL);
}

static
VOID
TouchFile(PCWSTR Directory, PCWSTR Name)
{
    WCHAR Path[MAX_PATH];
    HANDLE File;
    DWORD Written;

    StringCchPrintfW(Path, _countof(Path), L"%ls\\%ls", Directory, Name);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%ls) failed with %lu\n", Name, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        WriteFile(File, "x", 1, &Written, NULL);
        CloseHandle(File);
    }
}

static
VOID
DeleteInDirectory(PCWSTR Directory, PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    StringCchPrintfW(Path, _countof(Path), L"%ls\\%ls", Directory, Name);
    DeleteFileW(Path);
}

/* Dequeues one packet; returns FALSE on timeout */
static
BOOL
GetPacket(DWORD Timeout, ULONG_PTR *Key, LPOVERLAPPED *Overlapped, DWORD *Bytes, DWORD *Error)
{
    BOOL Ret;

    *Key = 0;
    *Overlapped = NULL;
    *Bytes = 0;
    Ret = GetQueuedCompletionStatus(Port, Bytes, Key, Overlapped, Timeout);
    *Error = Ret ? ERROR_SUCCESS : GetLastError();
    if (!Ret && *Overlapped == NULL && *Error == WAIT_TIMEOUT)
        return FALSE;
    ok(Ret || *Error == ERROR_ACCESS_DENIED,
       "A failed completion has error %lu, expected ERROR_ACCESS_DENIED\n", *Error);
    ok(*Overlapped != NULL, "Completion without an OVERLAPPED (key %Iu, error %lu)\n", *Key, *Error);
    return TRUE;
}

static
DWORD
WINAPI
IssuerThread(LPVOID Parameter)
{
    BOOL Ret;

    /* Issue the watch, then exit (Chromium's pool threads do this) */
    Ret = ReadDirectoryChangesW(Dir2, Buffer2, sizeof(Buffer2), FALSE,
                                FILE_NOTIFY_CHANGE_FILE_NAME, NULL, &Overlapped2, NULL);
    ok(Ret || GetLastError() == ERROR_IO_PENDING,
       "ReadDirectoryChangesW from a thread failed with %lu\n", GetLastError());
    return 0;
}

START_TEST(ReadDirectoryChanges)
{
    WCHAR TempPath[MAX_PATH], DirPath[MAX_PATH], Dir2Path[MAX_PATH];
    DWORD Buffer[1024];
    DWORD BigBuffer[16384];
    OVERLAPPED Overlapped, BigOverlapped;
    LPOVERLAPPED Done;
    ULONG_PTR Key;
    DWORD Bytes, Error;
    HANDLE Dir, Big, Thread;
    BOOL Ret;

    GetTempPathW(_countof(TempPath), TempPath);
    StringCchPrintfW(DirPath, _countof(DirPath), L"%lsrdc_test_%lu", TempPath, GetCurrentProcessId());
    StringCchPrintfW(Dir2Path, _countof(Dir2Path), L"%lsrdc_test2_%lu", TempPath, GetCurrentProcessId());
    ok(CreateDirectoryW(DirPath, NULL), "CreateDirectory failed with %lu\n", GetLastError());
    ok(CreateDirectoryW(Dir2Path, NULL), "CreateDirectory failed with %lu\n", GetLastError());

    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ok(Port != NULL, "CreateIoCompletionPort failed with %lu\n", GetLastError());
    if (!Port)
        goto Cleanup;

    Dir = OpenDirectory(DirPath);
    ok(Dir != INVALID_HANDLE_VALUE, "Opening the directory failed with %lu\n", GetLastError());
    if (Dir == INVALID_HANDLE_VALUE)
        goto Cleanup;
    ok(CreateIoCompletionPort(Dir, Port, KEY_DIR, 1) == Port,
       "Associating the directory failed with %lu\n", GetLastError());

    /* One request, no event */
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Ret = ReadDirectoryChangesW(Dir, Buffer, sizeof(Buffer), FALSE,
                                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                NULL, &Overlapped, NULL);
    ok(Ret || GetLastError() == ERROR_IO_PENDING, "ReadDirectoryChangesW failed with %lu\n", GetLastError());

    ok(!GetPacket(500, &Key, &Done, &Bytes, &Error), "Got a completion while nothing changed (key %Iu)\n", Key);

    /* A change completes it, with our OVERLAPPED and key */
    TouchFile(DirPath, L"a.txt");
    Ret = GetPacket(5000, &Key, &Done, &Bytes, &Error);
    ok(Ret, "No completion for the change\n");
    if (Ret)
    {
        ok(Done == &Overlapped, "Completion has OVERLAPPED %p, expected %p\n", Done, &Overlapped);
        ok(Key == KEY_DIR, "Completion has key %Iu, expected %u\n", Key, KEY_DIR);
        ok(Error == ERROR_SUCCESS, "Completion has error %lu\n", Error);
        ok(Bytes > 0, "Completion has no data\n");
    }
    ok(!GetPacket(200, &Key, &Done, &Bytes, &Error), "Got a second completion for one change (key %Iu)\n", Key);

    /* Closing the handle completes the pending request */
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Ret = ReadDirectoryChangesW(Dir, Buffer, sizeof(Buffer), FALSE,
                                FILE_NOTIFY_CHANGE_FILE_NAME, NULL, &Overlapped, NULL);
    ok(Ret || GetLastError() == ERROR_IO_PENDING, "ReadDirectoryChangesW failed with %lu\n", GetLastError());
    CloseHandle(Dir);
    Ret = GetPacket(5000, &Key, &Done, &Bytes, &Error);
    ok(Ret, "No completion for the request cancelled by CloseHandle\n");
    if (Ret)
    {
        ok(Done == &Overlapped, "Completion has OVERLAPPED %p, expected %p\n", Done, &Overlapped);
        ok(Key == KEY_DIR, "Completion has key %Iu, expected %u\n", Key, KEY_DIR);
    }

    /* As Chromium: recursive, its filter, a 64 KiB buffer */
    Big = OpenDirectory(TempPath);
    ok(Big != INVALID_HANDLE_VALUE, "Opening the temp directory failed with %lu\n", GetLastError());
    if (Big != INVALID_HANDLE_VALUE)
    {
        ok(CreateIoCompletionPort(Big, Port, 10, 1) == Port,
           "Associating the temp directory failed with %lu\n", GetLastError());
        ZeroMemory(&BigOverlapped, sizeof(BigOverlapped));
        Ret = ReadDirectoryChangesW(Big, BigBuffer, sizeof(BigBuffer), TRUE,
                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE |
                                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_DIR_NAME |
                                    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY,
                                    NULL, &BigOverlapped, NULL);
        ok(Ret || GetLastError() == ERROR_IO_PENDING,
           "Recursive ReadDirectoryChangesW with 64 KiB failed with %lu\n", GetLastError());
        TouchFile(DirPath, L"b.txt");
        Ret = GetPacket(5000, &Key, &Done, &Bytes, &Error);
        ok(Ret, "No completion for a change in a subfolder\n");
        if (Ret)
        {
            ok(Done == &BigOverlapped, "Completion has OVERLAPPED %p, expected %p\n", Done, &BigOverlapped);
            ok(Key == 10, "Completion has key %Iu, expected 10\n", Key);
        }
        CloseHandle(Big);
        /* Nothing else may be queued */
        while (GetPacket(200, &Key, &Done, &Bytes, &Error))
            ok(Done == &BigOverlapped, "Unexpected completion with OVERLAPPED %p\n", Done);
    }

    /* Thread-agnostic I/O: the issuing thread exits before the change */
    Dir2 = OpenDirectory(Dir2Path);
    ok(Dir2 != INVALID_HANDLE_VALUE, "Opening the directory failed with %lu\n", GetLastError());
    if (Dir2 != INVALID_HANDLE_VALUE)
    {
        ok(CreateIoCompletionPort(Dir2, Port, KEY_DIR2, 1) == Port,
           "Associating the directory failed with %lu\n", GetLastError());
        Thread = CreateThread(NULL, 0, IssuerThread, NULL, 0, NULL);
        ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Thread)
        {
            ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "The issuer thread did not exit\n");
            CloseHandle(Thread);
        }
        ok(!GetPacket(500, &Key, &Done, &Bytes, &Error),
           "Got a completion when the issuing thread exited (key %Iu, error %lu)\n", Key, Error);
        TouchFile(Dir2Path, L"c.txt");
        Ret = GetPacket(5000, &Key, &Done, &Bytes, &Error);
        ok(Ret, "No completion for the change after the issuing thread exited\n");
        if (Ret)
        {
            ok(Done == &Overlapped2, "Completion has OVERLAPPED %p, expected %p\n", Done, &Overlapped2);
            ok(Key == KEY_DIR2, "Completion has key %Iu, expected %u\n", Key, KEY_DIR2);
            ok(Error == ERROR_SUCCESS, "Completion has error %lu\n", Error);
        }
        CloseHandle(Dir2);
        while (GetPacket(200, &Key, &Done, &Bytes, &Error))
            ok(Done == &Overlapped2, "Unexpected completion with OVERLAPPED %p\n", Done);
    }

Cleanup:
    if (Port)
        CloseHandle(Port);
    DeleteInDirectory(DirPath, L"a.txt");
    DeleteInDirectory(DirPath, L"b.txt");
    DeleteInDirectory(Dir2Path, L"c.txt");
    RemoveDirectoryW(DirPath);
    RemoveDirectoryW(Dir2Path);
}
