/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test for process/thread attribute lists (STARTUPINFOEX, Vista+)
 *
 * Chromium starts its helper processes with an attribute list (the handles to
 * inherit): without these functions it could not start them.
 */

#include "precomp.h"

typedef BOOL (WINAPI *PINIT)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, PSIZE_T);
typedef BOOL (WINAPI *PUPDATE)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T);
typedef VOID (WINAPI *PDELETE)(LPPROC_THREAD_ATTRIBUTE_LIST);

#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#endif
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

START_TEST(ProcThreadAttributeList)
{
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    PINIT pInit = (PINIT)GetProcAddress(hKernel32, "InitializeProcThreadAttributeList");
    PUPDATE pUpdate = (PUPDATE)GetProcAddress(hKernel32, "UpdateProcThreadAttribute");
    PDELETE pDelete = (PDELETE)GetProcAddress(hKernel32, "DeleteProcThreadAttributeList");
    SIZE_T Size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST List;
    STARTUPINFOEXW StartupInfo = { { sizeof(StartupInfo) } };
    PROCESS_INFORMATION ProcessInfo;
    SECURITY_ATTRIBUTES Inheritable = { sizeof(Inheritable), NULL, TRUE };
    WCHAR CommandLine[MAX_PATH + 16];
    HANDLE Event;
    BOOL Ret;

    if (!pInit || !pUpdate || !pDelete)
    {
        skip("Attribute lists are not exported (Vista+)\n");
        return;
    }

    /* First call: the size */
    SetLastError(0xdeadbeef);
    Ret = pInit(NULL, 1, 0, &Size);
    ok(!Ret, "Size query succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Error %lu\n", GetLastError());
    ok(Size != 0, "Size is 0\n");

    List = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(pInit(List, 1, 0, &Size), "Initialize failed: %lu\n", GetLastError());

    /* A handle list, then the same attribute again (refused) */
    Event = CreateEventW(&Inheritable, TRUE, FALSE, NULL);
    ok(pUpdate(List, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &Event, sizeof(Event), NULL, NULL),
       "Update failed: %lu\n", GetLastError());
    ok(!pUpdate(List, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &Event, sizeof(Event), NULL, NULL),
       "Second update of the same attribute succeeded\n");

    /* CreateProcess with it */
    GetSystemDirectoryW(CommandLine, MAX_PATH);
    wcscat(CommandLine, L"\\cmd.exe /c exit");
    StartupInfo.lpAttributeList = List;
    Ret = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                         NULL, NULL, &StartupInfo.StartupInfo, &ProcessInfo);
    ok(Ret, "CreateProcess with an attribute list failed: %lu\n", GetLastError());
    if (Ret)
    {
        WaitForSingleObject(ProcessInfo.hProcess, 10000);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
    }

    pDelete(List);
    HeapFree(GetProcessHeap(), 0, List);
    CloseHandle(Event);
}
