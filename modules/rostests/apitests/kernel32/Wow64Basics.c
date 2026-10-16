/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Basic process checks, written for 32-bit programs on 64-bit ReactOS (WoW64)
 *
 * Threads, vectored exception handlers (a raised exception and an access
 * violation, both continued), a child process and its exit code, and file
 * I/O whose byte counts come back through the IO_STATUS_BLOCK (which the
 * kernel fills in its 32-bit form for a WoW64 process). They all run in any
 * process; built for SysWOW64 they check the WoW64 layer.
 */

#include "precomp.h"

#define TEST_EXCEPTION_CODE 0xE0000001
#define CHILD_EXIT_CODE 0x33

static volatile LONG AccessViolationSeen;

static LONG CALLBACK
Handler(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    if (Pointers->ExceptionRecord->ExceptionCode == TEST_EXCEPTION_CODE)
        return EXCEPTION_CONTINUE_EXECUTION;
#if defined(__GNUC__) && (defined(_M_IX86) || defined(_M_AMD64))
    if (Pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        /* Skip the faulting 6-byte "mov eax, [eax + 0]" below */
#ifdef _M_IX86
        Pointers->ContextRecord->Eip += 6;
#else
        Pointers->ContextRecord->Rip += 6;
#endif
        InterlockedIncrement(&AccessViolationSeen);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI
ThreadProc(
    _In_ PVOID Parameter)
{
    return (DWORD)(ULONG_PTR)Parameter * 2;
}

static VOID
TestFileIo(
    _In_ DWORD Flags,
    _In_ PCSTR What)
{
    static BYTE Data[4096];
    PBYTE Buffer;
    CHAR Path[MAX_PATH];
    DWORD Done, i;
    HANDLE hFile;
    BOOL Ret;

    for (i = 0; i < sizeof(Data); i++)
        Data[i] = (BYTE)i;
    /* Page-aligned, as FILE_FLAG_NO_BUFFERING needs */
    Buffer = VirtualAlloc(NULL, 8192, MEM_COMMIT, PAGE_READWRITE);
    ok(Buffer != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Buffer)
        return;
    GetTempPathA(sizeof(Path), Path);
    StringCbCatA(Path, sizeof(Path), "kernel32_apitest_wow64io.tmp");
    hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, Flags, NULL);
    ok(hFile != INVALID_HANDLE_VALUE, "%s: CreateFile failed with %lu\n", What, GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
    {
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return;
    }

    memcpy(Buffer, Data, sizeof(Data));
    Done = 0xdeadbeef;
    Ret = WriteFile(hFile, Buffer, sizeof(Data), &Done, NULL);
    ok(Ret && Done == sizeof(Data), "%s: WriteFile: %d, %lu bytes, error %lu\n", What, Ret, Done, GetLastError());
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    memset(Buffer, 0, 8192);
    Done = 0xdeadbeef;
    Ret = ReadFile(hFile, Buffer, sizeof(Data), &Done, NULL);
    ok(Ret && Done == sizeof(Data), "%s: ReadFile: %d, %lu bytes, error %lu\n", What, Ret, Done, GetLastError());
    ok(!memcmp(Buffer, Data, sizeof(Data)), "%s: the data read back differs\n", What);
    ok(GetFileSize(hFile, NULL) == sizeof(Data), "%s: GetFileSize %lu, expected %u\n",
       What, GetFileSize(hFile, NULL), (unsigned)sizeof(Data));
    CloseHandle(hFile);
    ok(DeleteFileA(Path), "%s: DeleteFile failed with %lu\n", What, GetLastError());
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(Wow64Basics)
{
    WCHAR FileName[MAX_PATH];
    WCHAR CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    HANDLE hThread;
    DWORD Code = 0, Id, Wait;
    BOOL IsWow64 = FALSE;
    PVOID Veh;
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc >= 3 && !strcmp(argv[2], "child"))
        ExitProcess(CHILD_EXIT_CODE);

    ok(IsWow64Process(GetCurrentProcess(), &IsWow64), "IsWow64Process failed with %lu\n", GetLastError());
#ifdef _WIN64
    ok(!IsWow64, "A 64-bit process is reported as WoW64\n");
#endif
    trace("%u-bit process, WoW64 %d\n", (unsigned)sizeof(PVOID) * 8, IsWow64);

    /* A thread and its exit code */
    hThread = CreateThread(NULL, 0, ThreadProc, (PVOID)21, 0, &Id);
    ok(hThread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (hThread)
    {
        ok(WaitForSingleObject(hThread, 10000) == WAIT_OBJECT_0, "The thread did not end\n");
        GetExitCodeThread(hThread, &Code);
        ok(Code == 42, "Thread exit code %lu, expected 42\n", Code);
        CloseHandle(hThread);
    }

    /* Vectored exception handlers */
    Veh = AddVectoredExceptionHandler(1, Handler);
    ok(Veh != NULL, "AddVectoredExceptionHandler failed\n");
    if (Veh)
    {
        RaiseException(TEST_EXCEPTION_CODE, 0, 0, NULL);
        ok(1, "RaiseException continued by the vectored handler\n");
#if defined(__GNUC__) && (defined(_M_IX86) || defined(_M_AMD64))
        /* An access violation, skipped by the handler: 6 bytes */
        __asm__ __volatile__("movl $0, %%eax\n\t.byte 0x8b, 0x80, 0, 0, 0, 0\n\t" ::: "eax", "memory");
        ok(AccessViolationSeen == 1, "Access violation seen %ld times, expected 1\n", AccessViolationSeen);
#endif
        RemoveVectoredExceptionHandler(Veh);
    }

    /* A child process (this program) and its exit code */
    GetModuleFileNameW(NULL, FileName, _countof(FileName));
    StringCbPrintfW(CommandLine, sizeof(CommandLine), L"\"%ls\" Wow64Basics child", FileName);
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    if (CreateProcessW(FileName, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        Wait = WaitForSingleObject(ProcessInfo.hProcess, 20000);
        ok(Wait == WAIT_OBJECT_0, "The child did not end (wait %lu)\n", Wait);
        if (Wait != WAIT_OBJECT_0)
        {
            TerminateProcess(ProcessInfo.hProcess, 1);
            WaitForSingleObject(ProcessInfo.hProcess, 5000);
        }
        GetExitCodeProcess(ProcessInfo.hProcess, &Code);
        ok(Code == CHILD_EXIT_CODE, "Child exit code 0x%lx, expected 0x%x\n", Code, CHILD_EXIT_CODE);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }
    else
    {
        ok(0, "CreateProcess failed with %lu\n", GetLastError());
    }

    TestFileIo(FILE_ATTRIBUTE_NORMAL, "cached");
    TestFileIo(FILE_FLAG_NO_BUFFERING, "no buffering");
}
